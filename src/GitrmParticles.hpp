#ifndef GITRM_PARTICLES_HPP
#define GITRM_PARTICLES_HPP

#include "pumipic_adjacency.hpp"
#include "pumipic_kktypes.hpp"

#include <psTypes.h>
#include <SellCSigma.h>
#include <SCS_Macros.h>
#include <Distribute.h>
#include <Kokkos_Core.hpp>
#include "pumipic_library.hpp"



using particle_structs::fp_t;
using particle_structs::lid_t;
using particle_structs::Vector3d;
using particle_structs::SellCSigma;
using particle_structs::MemberTypes;
using particle_structs::distribute_particles;
using particle_structs::distribute_name;
using particle_structs::elemCoords;

namespace o = Omega_h;
namespace p = pumipic;

// TODO: initialize these to its default values: ids =-1, reals=0
typedef MemberTypes < Vector3d, Vector3d, int,  Vector3d, int, int, Vector3d, 
       Vector3d, Vector3d> Particle;

// 'Particle' definition retrieval positions. 
enum {PTCL_POS_PREV, PTCL_POS, PTCL_ID, XPOINT, XPOINT_FACE, PTCL_BDRY_FACEID, 
     PTCL_BDRY_CLOSEPT, PTCL_EFIELD_PREV, PTCL_VEL};
typedef SellCSigma<Particle> SCS;

enum {BDRY_DATA_SIZE=7 }; //TODO

class GitrmParticles {
public:
  GitrmParticles(o::Mesh &m, const int np);
  ~GitrmParticles();
  GitrmParticles(GitrmParticles const&) = delete;
  void operator=(GitrmParticles const&) = delete;

  void defineParticles(const int elId, const int numPtcls=100);
  void findInitialBdryElemId(o::Real theta, o::Real phi, const o::Real r,
     o::LO &initEl, o::Write<o::LO> &elemAndFace, 
     const o::LO maxLoops = 100, const o::Real outer=2);
  void setImpurityPtclInitCoords(o::Write<o::LO> &);
  void initImpurityPtcls(o::Real, o::LO numPtcls,o::Real theta, o::Real phi, 
    o::Real r, o::LO maxLoops = 100, o::Real outer=2);
  void setInitialTargetCoords(o::Real dTime);

  SCS* scs;
  o::Mesh &mesh;
};

inline void setPtclIds(SCS* scs, bool verb=false) {
  if(verb)
    fprintf(stderr, " %s\n", __func__);
  auto pid_d = scs->template get<2>();
  PS_PARALLEL_FOR_ELEMENTS(scs, thread, e, {
    (void)e;
    PS_PARALLEL_FOR_PARTICLES(scs, thread, pid, {
      pid_d(pid) = pid;
    });
  });
}

//Note, elem_ids are index by pid=indexes, not scs member. Don't rebuild after search_mesh 
inline void applySurfaceModel(o::Mesh& mesh, SCS* scs, o::Write<o::LO>& elem_ids) {
  o::LO verbose = 4;
  o::Real pi = 3.14159265358979323; //TODO
  const auto coords = mesh.coords();
  const auto face_verts = mesh.ask_verts_of(2);
  const auto f2r_ptr = mesh.ask_up(o::FACE, o::REGION).a2ab;
  const auto f2r_elem = mesh.ask_up(o::FACE, o::REGION).ab2b;
  const auto side_is_exposed = mark_exposed_sides(&mesh);
  const auto mesh2verts = mesh.ask_elem_verts();
  const auto down_r2fs = mesh.ask_down(3, 2).ab2b;

  o::LO dof = BDRY_DATA_SIZE; //7
  auto pid_scs_d = scs->get<PTCL_ID>();
  auto pos_scs_d = scs->get<PTCL_POS>();
  auto xpt_scs_d = scs->get<XPOINT>();
  auto xface_scs_d = scs->get<XPOINT_FACE>();
  auto vel_scs_d = scs->get<PTCL_VEL>();
  //get mesh tag for boundary data id,xpt,vel
  auto xtag_r = mesh.get_array<o::Real>(o::FACE, "bdryData");
  
  o::Write<o::Real> xtag_d(xtag_r.size());
  auto convert = OMEGA_H_LAMBDA(int i) {
    for(int j=0; j<dof; ++j)
      xtag_d[i*dof+j] = xtag_r[i*dof+j];
  };
  o::parallel_for(mesh.nfaces(), convert);

  std::srand(time(NULL));
  auto lamb = SCS_LAMBDA(const int& e, const int& pid, const int& mask) {
    if(mask >0 && elem_ids[pid]==-1) {
    //mask is set for origin element, if moved it is not set for exiting element
      auto elemId = e;
      auto fid = xface_scs_d(pid);
      auto ptcl = pid_scs_d(pid);

      if(fid >= 0) {
        OMEGA_H_CHECK(side_is_exposed[fid]);
        auto vel = p::makeVector3(pid, vel_scs_d );
        auto xpt = p::makeVector3(pid, xpt_scs_d);
        auto pelem = p::elem_of_bdry_face(fid, f2r_ptr, f2r_elem);
        if(elemId != pelem)
          elemId = pelem;
        auto fnorm = p::get_face_normal(fid, elemId, coords, mesh2verts, 
          face_verts, down_r2fs);
        auto angle = p::angle_between(fnorm, vel);
        double rn = (double)std::rand()/RAND_MAX;
        // reflection
        if(angle > pi/6.0 && rn >0.5) {
          // R = D- 2(D.N)N;
          auto rvel = vel - 2* p::osh_dot(vel, fnorm) * fnorm;
          for(o::LO i=0; i<3; ++i)
            vel_scs_d(pid, i) = rvel[i];

          // move a bit inwards
          auto pos = xpt - 0.000001*fnorm;
          for(o::LO i=0; i<3; ++i)
            pos_scs_d(pid, i) = pos[i];
          
          elem_ids[pid] = elemId;
          if(verbose >3) {
            printf("ptclID %d reflected => elem %d pos: %g %g %g \n\t prevvel:%g %g %g vel:%g %g %g\n", 
                ptcl, elem_ids[pid], xpt[0], xpt[1],xpt[2],vel[0],vel[1],vel[2],
                rvel[0],rvel[1],rvel[2]);
          }
        } else {
        /*
       printf("\n****DOF=BDRY_DATA_SIZE: %d \n", dof);
          xtag_d[pid*dof] = static_cast<o::Real>(pid);
          for(int i=1; i<4; ++i) {
            xtag_d[pid*dof+i] = xpt[i];  
          }
          for(int i=5; i<7; ++i) {
            xtag_d[pid*dof+i] = vel[i];
          } */
        }
      }
    } //mask
  };
  scs->parallel_for(lamb);
  o::Reals tag(xtag_d);
  mesh.set_tag(o::FACE, "bdryData", tag);
}

inline void storeData(o::Mesh& mesh, SCS* scs, o::LO iter=0, bool print=false,
    o::Write<o::Real> *data_d = nullptr) {
  const auto coords = mesh.coords();
  const auto face_verts = mesh.ask_verts_of(2);
  const auto f2r_ptr = mesh.ask_up(o::FACE, o::REGION).a2ab;
  const auto f2r_elem = mesh.ask_up(o::FACE, o::REGION).ab2b;
  const auto side_is_exposed = mark_exposed_sides(&mesh);

  if(!data_d) {
    int width = (int)(2.5*100);
    int height = (int)(2.5*100);
    int around = (int)(2*o::PI*100);
    data_d = new o::Write<o::Real>(around*width*height, 0);
  }

  auto pos_scs_d = scs->get<PTCL_POS>();
  auto ppos_scs_d = scs->get<PTCL_POS_PREV>();
  auto vel_scs_d = scs->get<PTCL_VEL>();
  auto pid_d = scs->get<PTCL_ID>();
  auto lamb = SCS_LAMBDA(const int& e, const int& pid, const int& mask) {
    if(mask >0) {
      auto vel = p::makeVector3(pid, vel_scs_d);
      auto pos = p::makeVector3(pid, pos_scs_d);

    // find grid index
      /*
      data_d[pid*dof] = static_cast<double>(pid);
      int s = 1;
      for(int i=s; i<s+3; ++i)
        data_d[pid*dof+i] = pos[i-s];
      s = 4;
      for(int i=s; i<s+3; ++i)
        data_d[pid*dof+i] = vel[i-s];
      */
      if(print)
        printf("ptclID %d iter %d pos %.5f %.5f %.5f prev %.5f %.5f %.5f\n", 
           pid_d(pid), iter, pos_scs_d(pid,0), pos_scs_d(pid,1), pos_scs_d(pid,2),
           ppos_scs_d(pid,0), ppos_scs_d(pid,1), ppos_scs_d(pid,2) ); 
    }
  };
  scs->parallel_for(lamb);
}

#endif//define
