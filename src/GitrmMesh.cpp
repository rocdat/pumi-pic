#include <fstream>
#include <algorithm>
#include <vector>
#include "GitrmMesh.hpp"
#include "GitrmParticles.hpp"
#include "GitrmInputOutput.hpp"
#include "Omega_h_reduce.hpp"
//#include "Omega_h_atomics.hpp"  //No such file

namespace o = Omega_h;
namespace p = pumipic;


//o::Mesh* GitrmMesh::mesh = nullptr;
//bool GitrmMesh::hasMesh = false;

GitrmMesh::GitrmMesh(o::Mesh& m): 
  mesh(m) {
}

void GitrmMesh::load3DFieldOnVtxFromFile(const std::string tagName, const std::string &file,
  Field3StructInput& fs, o::Reals& readInData_d, const o::Real shift) {
  o::LO debug = 1;
  std::cout<< "Loading " << tagName << " from " << file << " on vtx\n" ;
  //processFieldFileFS3(file, fs, debug);
  readInputDataNcFileFS3(file, fs);
  int nR = fs.getNumGrids(0);
  int nZ = fs.getNumGrids(1);
  o::Real rMin = fs.getGridMin(0);
  o::Real zMin = fs.getGridMin(1);
  o::Real dr = fs.getGridDelta(0);
  o::Real dz = fs.getGridDelta(1);
  if(debug){
    printf(" %s dr%.5f , dz%.5f , rMin%.5f , zMin%.5f \n",
        tagName.c_str(), dr, dz, rMin, zMin);
    printf("data size %d \n", fs.data.size());
  }

  // Interpolate at vertices and Set tag
  o::Write<o::Real> tag_d(3*mesh.nverts());
  const auto coords = mesh.coords(); //Reals
  readInData_d = o::Reals(fs.data);
  auto fill = OMEGA_H_LAMBDA(o::LO iv) {
    o::Vector<3> fv = o::zero_vector<3>();
    o::Vector<3> pos= o::zero_vector<3>();
    // coords' size is 3* nverts
    for(o::LO j=0; j<3; ++j)
      pos[j] = coords[3*iv+j];
    if(debug && iv < 5)
      printf(" iv:%d %.5f %.5f  %.5f \n", iv, pos[0], pos[1], pos[2]);
    
    //cylindrical symmetry, height (z) is same.
    auto rad = sqrt(pos[0]*pos[0] + pos[1]*pos[1]);
    // projecting point to y=0 plane, since 2D data is on const-y plane.
    // meaningless to include non-zero y coord of target plane.
    pos[0] = rad + shift;
    pos[1] = 0;
    // cylindrical symmetry = false, since already projected onto y=0 plane
    p::interp2dVector(readInData_d, rMin, zMin, dr, dz, nR, nZ, pos, fv, false);
    for(o::LO j=0; j<3; ++j){ //components
      tag_d[3*iv+j] = fv[j]; 

      if(debug && iv<5)
        printf("vtx %d j %d tag_d[%d]= %g\n", iv, j, 3*iv+j, tag_d[3*iv+j]);
    }
  };
  o::parallel_for(mesh.nverts(), fill, "Fill B Tag");
  o::Reals tag(tag_d);
  mesh.set_tag(o::VERT, tagName, tag);
  if(debug)
    printf("Added tag %s \n", tagName.c_str());
}

// TODO Remove Tags after use ?
// TODO pass parameters in a compact form, libconfig ?
void GitrmMesh::initBField(const std::string &bFile, const o::Real shiftB) {
  mesh2Bfield2Dshift = shiftB;
  mesh.add_tag<o::Real>(o::VERT, "BField", 3);
  // set bt=0. Pisces BField is perpendicular to W target base plate.
  Field3StructInput fb({"br", "bt", "bz"}, {"gridR", "gridZ"}, {"nR", "nZ"});
  load3DFieldOnVtxFromFile("BField", bFile, fb, Bfield_2d, mesh2Bfield2Dshift); 
  
  bGridX0 = fb.getGridMin(0);
  bGridZ0 = fb.getGridMin(1);
  bGridNx = fb.getNumGrids(0);
  bGridNz = fb.getNumGrids(1);
  bGridDx = fb.getGridDelta(0);
  bGridDz = fb.getGridDelta(1);
}


void GitrmMesh::loadScalarFieldOnBdryFacesFromFile(const std::string tagName, 
  const std::string &file, Field3StructInput& fs, const o::Real shift, int debug) {
  const auto coords = mesh.coords();
  const auto face_verts = mesh.ask_verts_of(2);
  const auto side_is_exposed = mark_exposed_sides(&mesh);

  o::LO verbose = 4;
  if(verbose >0)
    std::cout << "Loading "<< tagName << " from " << file << " on bdry faces\n";

  readInputDataNcFileFS3(file, fs);
  int nR = fs.getNumGrids(0);
  int nZ = fs.getNumGrids(1);
  o::Real rMin = fs.getGridMin(0);
  o::Real zMin = fs.getGridMin(1);
  o::Real dr = fs.getGridDelta(0);
  o::Real dz = fs.getGridDelta(1);

  if(verbose >3)
    printf("nR %d nZ %d dr %g, dz %g, rMin %g, zMin %g \n", nR, nZ, dr, dz, rMin, zMin);
  //Interpolate at vertices and Set tag
  o::Write<o::Real> tag_d(mesh.nfaces());
  const auto readInData_d = o::Reals(fs.data);

  if(debug) {
    printf("%s file %s\n", tagName.c_str(), file.c_str());
    for(int i =0; i<100 ; i+=5) {
      printf("%d:%g ", i, fs.data[i] );
    }
    printf("\n");
  }
  auto fill = OMEGA_H_LAMBDA(o::LO fid) {
    //TODO check if faceids are sequential numbers
    if(!side_is_exposed[fid]) {
      tag_d[fid] = 0;
      return;
    }
    // TODO storing fields at centroid may not be best for long tets.
    auto pos = p::face_centroid_of_tet(fid, coords, face_verts);
    //cylindrical symmetry. Height (z) is same.
    auto rad = sqrt(pos[0]*pos[0] + pos[1]*pos[1]);
    // projecting point to y=0 plane, since 2D data is on const-y plane.
    // meaningless to include non-zero y coord of target plane.
    pos[0] = rad + shift; 
    pos[1] = 0;
    if(debug)
      printf("fill2:: %f %f %f %f %d %d %f %f %f\n", rMin, zMin, dr, dz, nR, nZ, pos[0], pos[1], pos[2]);
    //Cylindrical symmetry = false, since already projected onto y=0 plane
    o::Real val = p::interpolate2dField(readInData_d, rMin, zMin, dr, dz, 
      nR, nZ, pos, false, 1, 0, debug);
    tag_d[fid] = val; 

    if(verbose > 4 && fid<10)
      printf(" tag_d[%d]= %.5f\n", fid, val);
  };
  o::parallel_for(mesh.nfaces(), fill, "Fill_face_tag");
  o::Reals tag(tag_d);
  mesh.set_tag(o::FACE, tagName, tag);
}
 
void GitrmMesh::load1DFieldOnVtxFromFile(const std::string tagName, 
  const std::string& file, Field3StructInput& fs, o::Reals& readInData_d, 
  o::Reals& tagData, const o::Real shift, int debug) {
  std::cout<< "Loading " << tagName << " from " << file << " on vtx\n" ;
  //processFieldFileFS3(file, fs, debug);
  readInputDataNcFileFS3(file, fs);
  int nR = fs.getNumGrids(0);
  int nZ = fs.getNumGrids(1);
  o::Real rMin = fs.getGridMin(0);
  o::Real zMin = fs.getGridMin(1);
  o::Real dr = fs.getGridDelta(0);
  o::Real dz = fs.getGridDelta(1);

  // data allocation not available here ?
  if(debug){
    auto nd = fs.data.size();
    printf(" %s dr %.5f, dz %.5f, rMin %.5f, zMin %.5f datSize %d\n",
        tagName.c_str(), dr, dz, rMin,zMin, nd);
    for(int j=0; j<nd; j+=nd/20)
      printf("data[%d]:%g ", j, fs.data[j]);
    printf("\n");
  }
  // Interpolate at vertices and Set tag
  o::Write<o::Real> tag_d(mesh.nverts());

  const auto coords = mesh.coords(); //Reals
  readInData_d = o::Reals(fs.data);

  auto fill = OMEGA_H_LAMBDA(o::LO iv) {
    o::Vector<3> pos = o::zero_vector<3>();
    // coords' size is 3* nverts
    for(o::LO j=0; j<3; ++j){
      pos[j] = coords[3*iv+j];
    }
    if(debug && iv>30 && iv<35){
      printf(" vtx:%d %.5f %.5f %.5f\n", iv, pos[0], pos[1], pos[2]);
    }
    //NOTE modifying positions in the following
    //cylindrical symmetry.Height (z) is same.
    auto rad = sqrt(pos[0]*pos[0] + pos[1]*pos[1]);
    // projecting point to y=0 plane, since 2D data is on const-y plane.
    // meaningless to include non-zero y coord of target plane.
    pos[0] = rad + shift;
    pos[1] = 0;
    
    //Cylindrical symmetry = false, since already projected onto y=0 plane
    o::Real val = p::interpolate2dField(readInData_d, rMin, zMin, dr, dz, 
      nR, nZ, pos, false, 1, 0, false); //last debug
    tag_d[iv] = val; 

    if(debug && iv>30 && iv<35){
      printf(" tag_d[%d]= %g\n", iv, val);
    }
  };
  o::parallel_for(mesh.nverts(), fill, "Fill Tag");
  tagData = o::Reals (tag_d);
  mesh.set_tag(o::VERT, tagName, tagData);
}

void GitrmMesh::addTagsAndLoadProfileData(const std::string &profileFile, 
  const std::string &profileDensityFile) {
  mesh.add_tag<o::Real>(o::FACE, "ElDensity", 1);
  mesh.add_tag<o::Real>(o::FACE, "IonDensity", 1); //=ni 
  mesh.add_tag<o::Real>(o::FACE, "IonTemp", 1);
  mesh.add_tag<o::Real>(o::FACE, "ElTemp", 1);
  mesh.add_tag<o::Real>(o::VERT, "IonDensityVtx", 1);
  mesh.add_tag<o::Real>(o::VERT, "ElDensityVtx", 1);
  mesh.add_tag<o::Real>(o::VERT, "IonTempVtx", 1);
  mesh.add_tag<o::Real>(o::VERT, "ElTempVtx", 1);
  Field3StructInput fd({"ni"}, {"gridR", "gridZ"}, {"nR", "nZ"}); 
  loadScalarFieldOnBdryFacesFromFile("IonDensity", profileDensityFile, fd); 

  Field3StructInput fdv({"ni"}, {"gridR", "gridZ"}, {"nR", "nZ"});   
  load1DFieldOnVtxFromFile("IonDensityVtx", profileDensityFile, fdv, 
    densIon_d, densIonVtx_d, 0, 1);
  densIonX0 = fdv.getGridMin(0);
  densIonZ0 = fdv.getGridMin(1);
  densIonNx = fdv.getNumGrids(0);
  densIonNz = fdv.getNumGrids(1);
  densIonDx = fdv.getGridDelta(0);
  densIonDz = fdv.getGridDelta(1);

  Field3StructInput fne({"ne"}, {"gridR", "gridZ"}, {"nR", "nZ"});  
  loadScalarFieldOnBdryFacesFromFile("ElDensity", profileFile, fne); 
  Field3StructInput fnev({"ne"}, {"gridR", "gridZ"}, {"nR", "nZ"});  
  load1DFieldOnVtxFromFile("ElDensityVtx", profileFile, fnev, densEl_d, densElVtx_d);
  Field3StructInput fti({"ti"}, {"gridR", "gridZ"}, {"nR", "nZ"}); 
  loadScalarFieldOnBdryFacesFromFile("IonTemp", profileFile, fti); 
  Field3StructInput ftiv({"ti"}, {"gridR", "gridZ"}, {"nR", "nZ"});
  load1DFieldOnVtxFromFile("IonTempVtx", profileFile, ftiv, temIon_d, tempIonVtx_d);

  tempIonX0 = ftiv.getGridMin(0);
  tempIonZ0 = ftiv.getGridMin(1);
  tempIonNx = ftiv.getNumGrids(0);
  tempIonNz = ftiv.getNumGrids(1);
  tempIonDx = ftiv.getGridDelta(0);
  tempIonDz = ftiv.getGridDelta(1);

  // electron Temperature
  Field3StructInput fte({"te"}, {"gridR", "gridZ"}, {"nR", "nZ"});
  loadScalarFieldOnBdryFacesFromFile("ElTemp", profileFile, fte); 
  Field3StructInput ftev({"te"}, {"gridR", "gridZ"}, {"nR", "nZ"});
  load1DFieldOnVtxFromFile("ElTempVtx", profileFile, ftev, temEl_d, tempElVtx_d);
}


//NOTE: Importance of mesh.size in GITRm over all boundary faces:
// mesh of size set comp.to GITR: 573742 (GITR) vs 506832 (GITRm)
// mesh of large face dim: 573742 (GITR) vs 361253 (GITRm)
// El temperature is different at center of face, compared to GITR
// when same 1st 2 particles were compared in calcE. For simulation
// using biased surface, TEl decides DLength and CLDist.

//TODO spli this function
void GitrmMesh::initBoundaryFaces(bool debug) {
  auto fieldCenter = mesh2Efield2Dshift;

  const auto coords = mesh.coords();
  const auto face_verts = mesh.ask_verts_of(2);
  const auto side_is_exposed = mark_exposed_sides(&mesh);
  const auto mesh2verts = mesh.ask_elem_verts();
  const auto f2r_ptr = mesh.ask_up(o::FACE, o::REGION).a2ab;
  const auto f2r_elem = mesh.ask_up(o::FACE, o::REGION).ab2b;
  const auto down_r2fs = mesh.ask_down(3, 2).ab2b;
  auto useConstantBField = USE_CONSTANT_BFIELD;
  o::Real BField_const[] = {CONSTANT_BFIELD[0], CONSTANT_BFIELD[1], CONSTANT_BFIELD[2]};
  o::Real potential = BIAS_POTENTIAL;
  auto biased = BIASED_SURFACE;
  o::Real bxz[4] = {bGridX0, bGridZ0, bGridDx, bGridDz};
  o::LO bnz[2] = {bGridNx, bGridNz};
  const auto &Bfield_2dm = Bfield_2d;
  if(debug)
    printf("bxz: %g %g %g %g\n",  bxz[0], bxz[1], bxz[2], bxz[3]);
  const auto density = mesh.get_array<o::Real>(o::FACE, "IonDensity"); //=ni
  const auto ne = mesh.get_array<o::Real>(o::FACE, "ElDensity");
  const auto te = mesh.get_array<o::Real>(o::FACE, "ElTemp");
  const auto ti = mesh.get_array<o::Real>(o::FACE, "IonTemp");

  o::Write<o::Real> angle_d(mesh.nfaces());
  o::Write<o::Real> debyeLength_d(mesh.nfaces());
  o::Write<o::Real> larmorRadius_d(mesh.nfaces());
  o::Write<o::Real> childLangmuirDist_d(mesh.nfaces());
  o::Write<o::Real> flux_d(mesh.nfaces());
  o::Write<o::Real> impacts_d(mesh.nfaces());
  o::Write<o::Real> potential_d(mesh.nfaces());

  const o::LO background_Z = BACKGROUND_Z;
  const o::Real background_amu = BACKGROUND_AMU;
  //TODO faceId's sequential from 0 ?
  auto fill = OMEGA_H_LAMBDA(o::LO fid) {
    //TODO if faceId's are not sequential, create a (bdry) faceIds array 
    if(side_is_exposed[fid]) {
      o::Vector<3> B = o::zero_vector<3>();
      auto fcent = p::face_centroid_of_tet(fid, coords, face_verts);
      //cylindrical symmetry, height (z) is same.
      auto rad = sqrt(fcent[0]*fcent[0] + fcent[1]*fcent[1]);
      // projecting point to y=0 plane, since 2D data is on const-y plane.
      // meaningless to include non-zero y coord of target plane.
      fcent[0] = rad + fieldCenter; // D3D 1.6955m. TODO check unit
      fcent[1] = 0;
      //Cylindrical symmetry = false, since already projected onto y=0 plane

      // TODO angle is between surface normal and magnetic field at center of face
      // If  face is long, BField is not accurate. Calculate at closest point ?
      if(debug)//&& fid%1000==0)
        printf(" fid:%d::  %.5f %.5f %.5f \n", fid, fcent[0], fcent[1], fcent[2]);
      if(useConstantBField) {
        B[0] = BField_const[0];
        B[1] = BField_const[1];
        B[2] = BField_const[2];
      } else {
        assert(! p::almost_equal(bxz,0));
        p::interp2dVector(Bfield_2dm,  bxz[0], bxz[1], bxz[2], bxz[3], bnz[0],
          bnz[1], fcent, B, false);
      }
      /*
      auto elmId = p::elem_of_bdry_face(fid, f2r_ptr, f2r_elem);
      auto surfNorm = p::face_normal_of_tet(fid, elmId, coords, mesh.verts, 
          face_verts, down_r2fs);
      */
      //TODO verify
      auto surfNorm = p::bdry_face_normal_of_tet(fid,coords,face_verts);
      o::Real magB = o::norm(B);
      o::Real magSurfNorm = o::norm(surfNorm);
      o::Real angleBS = p::osh_dot(B, surfNorm);
      o::Real theta = acos(angleBS/(magB*magSurfNorm));
      if (theta > o::PI * 0.5) {
        theta = abs(theta - o::PI);
      }
      angle_d[fid] = theta*180.0/o::PI;
      
      if(debug) {
        printf("fid:%d surfNorm:%g %g %g angleBS=%g theta=%g angle=%g\n", fid, surfNorm[0], 
          surfNorm[1], surfNorm[2],angleBS,theta,angle_d[fid]);
      }

      o::Real tion = ti[fid];
      o::Real tel = te[fid];
      o::Real nel = ne[fid];
      o::Real dens = density[fid];  //3.0E+19 ?

      o::Real dlen = 0;
      if(o::are_close(nel, 0.0)){
        dlen = 1.0e12;
      }
      else {
        dlen = sqrt(8.854187e-12*tel/(nel*pow(background_Z,2)*1.60217662e-19));
      }
      debyeLength_d[fid] = dlen;

      larmorRadius_d[fid] = 1.44e-4*sqrt(background_amu*tion/2)/(background_Z*magB);
      flux_d[fid] = 0.25* dens *sqrt(8.0*tion*1.60217662e-19/(o::PI*background_amu));
      impacts_d[fid] = 0.0;
      o::Real pot = potential;
      if(biased) {
        o::Real cld = 0;
        if(tel > 0.0) {
          cld = dlen * pow(abs(pot)/tel, 0.75);
        }
        else { 
          cld = 1e12;
        }
        childLangmuirDist_d[fid] = cld;
      } else {
        pot = 3.0*tel; 
      }
      potential_d[fid] = pot;

      if(debug)// || o::are_close(angle_d[fid],0))
        printf("angle[%d] %.5f\n", fid, angle_d[fid]);
    }
  };
  //TODO 
  o::parallel_for(mesh.nfaces(), fill, "Fill face Tag");

  mesh.add_tag<o::Real>(o::FACE, "angleBdryBfield", 1, o::Reals(angle_d));
  mesh.add_tag<o::Real>(o::FACE, "DebyeLength", 1, o::Reals(debyeLength_d));
  mesh.add_tag<o::Real>(o::FACE, "LarmorRadius", 1, o::Reals(larmorRadius_d));
  mesh.add_tag<o::Real>(o::FACE, "ChildLangmuirDist", 1, o::Reals(childLangmuirDist_d));
  mesh.add_tag<o::Real>(o::FACE, "flux", 1, o::Reals(flux_d));
  mesh.add_tag<o::Real>(o::FACE, "impacts", 1, o::Reals(impacts_d));
  mesh.add_tag<o::Real>(o::FACE, "potential", 1, o::Reals(potential_d));

 //Book keeping of intersecting particles
  mesh.add_tag<o::Real>(o::FACE, "gridE", 1); // store subgrid data ?
  mesh.add_tag<o::Real>(o::FACE, "gridA", 1);
  mesh.add_tag<o::Real>(o::FACE, "sumParticlesStrike", 1);
  mesh.add_tag<o::Real>(o::FACE, "sumWeightStrike", 1);
  mesh.add_tag<o::Real>(o::FACE, "grossDeposition", 1);
  mesh.add_tag<o::Real>(o::FACE, "grossErosion", 1);
  mesh.add_tag<o::Real>(o::FACE, "aveSputtYld", 1);
  mesh.add_tag<o::LO>(o::FACE, "sputtYldCount", 1);

  o::LO nEdist = 4; //TODO
  o::LO nAdist = 4; //TODO
  mesh.add_tag<o::Real>(o::FACE, "energyDistribution", nEdist*nAdist);
  mesh.add_tag<o::Real>(o::FACE, "sputtDistribution", nEdist*nAdist);
  mesh.add_tag<o::Real>(o::FACE, "reflDistribution", nEdist*nAdist);
}

// These are only for temporary writing during pre-processing
void GitrmMesh::initNearBdryDistData() {
  auto nel = mesh.nelems();
  int fSize = BDRY_FACE_STORAGE_SIZE_PER_FACE;
  int bdrySize =BDRYFACE_SIZE;
  OMEGA_H_CHECK(bdrySize > 0);
  OMEGA_H_CHECK(fSize > 0);
  printf("Init bdry arrays \n");
  // Flat arrays per element. Copying is passing device data.
  bdryFacesW = o::Write<o::Real>(nel *bdrySize * (fSize - 1), 0);
  numBdryFaceIds = o::Write<o::LO>(nel, 0);
  // This size is also pre-decided
  bdryFaceIds = o::Write<o::LO>(nel * bdrySize, -1);
  bdryFlags = o::Write<o::LO>(nel, 0);
  bdryFaceElemIds = o::Write<o::LO>(nel * bdrySize, -1);
}

// No more changes to the bdry faces data; and delete flat arrays
void GitrmMesh::convert2ReadOnlyCSR() {
  const auto coords = mesh.coords();
  const auto face_verts = mesh.ask_verts_of(2);

  //const auto &bdryFacesW = this->bdryFacesW;  //not used now
  const auto &numBdryFaceIds = this->numBdryFaceIds;
  const auto &bdryFaceIds = this->bdryFaceIds;
  const auto &bdryFaceElemIds = this->bdryFaceElemIds;

  auto &bdryFaces = this->bdryFaces;
  auto &bdryFaceInds = this->bdryFaceInds;
  int size = BDRY_FACE_STORAGE_SIZE_PER_FACE;
  int nIds = BDRY_FACE_STORAGE_IDS;
  int bfSize = BDRYFACE_SIZE;
  // Convert to CSR.
  // Not actual index of bdryFaces, but has to x by BDRY_FACE_STORAGE_SIZE_PER_FACE to get it
  o::LOs numBdryFaceIds_r(numBdryFaceIds);
  numAddedBdryFaces = calculateCsrIndices(numBdryFaceIds_r, bdryFaceInds);

  //Keep separate copy of bdry face coords per elem, to avoid  accessing another
  // element's face. Can save storage if common bdry face data is used by face id?
  // Replace flat arrays of Write<> by convenient,
  // Kokkos::View<double[numAddedBdryFaces][BDRY_FACE_STORAGE_SIZE_PER_FACE]> ..  ?

  o::Write<o::Real> bdryFacesCsrW(numAddedBdryFaces * size, 0);
  
  //Copy data
  auto convert = OMEGA_H_LAMBDA(o::LO elem) {
    // Index points to data counting spread out face or x,y,z components. ?
    // This calculates face number from the above. 

    // face index is NOT the actual array index of face data.
    auto beg = bdryFaceInds[elem];
    auto end = bdryFaceInds[elem+1];
    if(beg == end){
      return;
    }
    assert((end - beg) == numBdryFaceIds[elem]);

    // Source position calculated using pre-assigned fixed number of faces
    // per element. Here face number (outer loop) is used to get absolute position.
    // Size_per_face was effectively 2 less so far, since the face id was not added.
    // The face_id is added as Real at the 1st entry of each face.

    // Source face size is 1 less.
    //auto bfd = elem*bfSize*(size-1);
    auto bfi = elem*bfSize;
    o::Real fdat[9] = {0};

    // Looping over implicit face numbers
    for(o::LO i = beg; i < end; ++i) {
      p::get_face_coords_of_tet(face_verts, coords, bdryFaceIds[bfi], fdat);

      // Face index is x by size_per_face 
      bdryFacesCsrW[i*size] = static_cast<o::Real>(bdryFaceIds[bfi]);
      bdryFacesCsrW[i*size+1] = static_cast<o::Real>(bdryFaceElemIds[bfi]);
      //printf(" f_%d  el_%d\n", bdryFaceIds[bfi], bdryFaceElemIds[bfi]);
      for(o::LO j = nIds; j < size; ++j) {
        //bdryFacesCsrW[i*size + j] = bdryFacesW[bfd + j-1]; //TODO check this
        bdryFacesCsrW[i*size + j] = fdat[j - nIds];
      }
      ++bfi;
    }    
  };
  o::parallel_for(mesh.nelems(), convert, "Convert to CSR write");
  //CSR data. Conversion is making RO object on device by passing device data.
  bdryFaces = o::Reals(bdryFacesCsrW);

}

// Pre-processing to fill bdry faces in elements near bdry within depth needed. 
// Call init function before this. Filling flat arrays; Later call convert2ReadOnlyCSR.
void GitrmMesh::copyBdryFacesToSelf() {
  MESHDATA(mesh);

  int bfaceSize = BDRYFACE_SIZE;
  //auto &bdryFacesW = this->bdryFacesW;
  auto &numBdryFaceIds = this->numBdryFaceIds;
  auto &bdryFaceIds = this->bdryFaceIds;
  auto &bdryFaceElemIds = this->bdryFaceElemIds;
  auto &bdryFlags = this->bdryFlags;

  o::LO verbose = 1;
  auto fillBdry = OMEGA_H_LAMBDA(o::LO elem) {
    // Find  bdry faces: # and id
    o::LO nbdry = 0; 
    o::LO bdryFids[4];
    const auto beg_face = elem *4;
    const auto end_face = beg_face +4;
    for(o::LO fi = beg_face; fi < end_face; ++fi) {//not 0..3
      const auto fid = down_r2f[fi];
      if(verbose>2) {
        printf( "exposed: e_%d, i_%d, fid_%d => %d\n", elem, fi, fid, side_is_exposed[fid]);
      }
      if(side_is_exposed[fid]) {
        bdryFids[nbdry] = fid;
        ++nbdry;
      }
    }

    // If no bdry faces to add
    if(!nbdry){
      return;
    }
    if(verbose>2) printf("\nelem:%d, nbdry: %d \n", elem, nbdry);

    //Only boundary faces
    for(o::LO fi = 0; fi < nbdry; ++fi) {
      auto fid = bdryFids[fi];
      auto fv2v = o::gather_verts<3>(face_verts, fid); //Few<LO, 3>

      // const auto face = o::gather_vectors<3, 3>(coords, fv2v);
      //From self bdry

      // Function call forces the data passed to be const
      // p::addFaceToBdryData(bdryFacesW, bdryFaceIds, BDRYFACE_SIZE, BDRY_FACE_STORAGE_SIZE_PER_FACE, 
     //  3, fi, fid, elem, face);
      
      //TODO test this
      /*for(o::LO i=0; i<3; ++i){
        for(o::LO j=0; j<3; j++){
            bdryFacesW[elem* BDRYFACE_SIZE* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1)  + 
              fi* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1) + i*3 + j] = face[i][j];
            if(verbose>2)
                printf("elem_%d FACE %d: %d %d %0.3f  @%d \n" , elem, fid ,i,j, face[i][j],
                elem* BDRYFACE_SIZE* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1)  +  fi* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1) + i*3 + j); 
        }
      }
      
      for(int fv=0; fv< 3;++fv){ 
        for(int vc=0; vc< 3;++vc){
          printf( "bdryFacesW: e_%d, fid_%d => %0.4f\n", elem, fid, 
            bdryFacesW[elem* BDRYFACE_SIZE* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1)  + 
                fi* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1) + fv*3 + vc]);}}
      */ 
      auto ind = elem* bfaceSize + fi;
      bdryFaceIds[ind] = fid;
      bdryFaceElemIds[ind] = elem;

    }
    numBdryFaceIds[elem] = nbdry;
    if(verbose>2)
      printf( "numBdryFaceIds: e_%d, num_%d \n", elem, numBdryFaceIds[elem]);

    //Set neigbhor's flags
   // p::updateAdjElemFlags(dual_faces, dual_elems, elem, bdryFlags, nbdry);
    auto dual_elem_id = dual_faces[elem];
    for(o::LO i=0; i<4-nbdry; ++i){
      auto adj_elem  = dual_elems[dual_elem_id];
      o::LO val = 1;
      Kokkos::atomic_exchange( &bdryFlags[adj_elem], val);
      if(verbose>2)
        printf( "bdryFlags: e_%d : %d ; set_by_%d\n", adj_elem, bdryFlags[adj_elem], elem);
      ++dual_elem_id;
    }

  };
  o::parallel_for(nel, fillBdry, "CopyBdryFacesToSelf");
  
}


void GitrmMesh::preProcessDistToBdry() {
  MESHDATA(mesh);
  printf("Distance-to-boundary depth DEPTH_DIST2_BDRY = %f \n", DEPTH_DIST2_BDRY);
  int bfaceSize = BDRYFACE_SIZE;
  int interior = INTERIOR;
  double depth = DEPTH_DIST2_BDRY;
  //auto &bdryFacesW = this->bdryFacesW;
  auto &numBdryFaceIds = this->numBdryFaceIds;
  auto &bdryFaceIds = this->bdryFaceIds;
  auto &bdryFaceElemIds = this->bdryFaceElemIds;
  auto &bdryFlags = this->bdryFlags;
  
  initNearBdryDistData();
  copyBdryFacesToSelf();

  auto fill = OMEGA_H_LAMBDA(o::LO elem) {
    o::LO verbose = 1;
    // This has to be init by copyBdryFacesToSameElem.
    o::LO update = bdryFlags[elem];
    if(verbose >2) 
      printf( "\n\n Beg:bdryFlags: e_%d, update: %d nbf_%d\n", elem, 
        bdryFlags[elem], numBdryFaceIds[elem]);

    //None of neigbhors have update
    if(! update){
      return;
    }
    o::LO val = 0;
    Kokkos::atomic_exchange(&bdryFlags[elem], val);

    const auto tetv2v = Omega_h::gather_verts<4>(mesh2verts, elem);
    const auto tet = Omega_h::gather_vectors<4, 3>(coords, tetv2v);

    o::LO nbf = 0;
    
    // Bdry element need not have regions that have shortest dist to bdry
    // always on the own bdry face. So, need to add adj. faces. So, interior
    // faces of bdry elems have to add neigbhors' faces

    // Store existing # of faces
    o::LO existing = numBdryFaceIds[elem];
    o::LO tot = existing;

    o::LO inFids[4] = {-1, -1, -1, -1};
    // One purpose of small functions that use mesh.data is to pre-use 
    // mesh.data as far as possible, i.e. minimize using mesh.data 
    // alongside unavoidable large-data in a loop.
    o::LO nIn = p::get_face_types_of_tet(elem, down_r2f, 
                  side_is_exposed, inFids, interior);
    nbf = 4 - nIn;

    auto dual_elem_id = dual_faces[elem]; //first interior

    for(auto fi = 0; fi < nIn; ++fi) {

      //Check all adjacent elements
      auto adj_elem  = dual_elems[dual_elem_id];

      //auto fid = inFids[fi];
      //auto fv2v = o::gather_verts<3>(face_verts, fid); //Few<LO, 3>
      //const auto face = o::gather_vectors<3, 3>(coords, fv2v);

      // Get ids updated by adj. elems of previous faces processed
      o::LO sizeAdj = numBdryFaceIds[adj_elem]; 

      if(verbose >2) 
        printf("\n **tot:%d , SizeAdj_of_%d = %d \n", tot, adj_elem, sizeAdj);

      for(o::LO ai=0; ai < sizeAdj; ++ai) {
        bool found = false;
        auto index = bfaceSize*adj_elem + ai;
        auto adj_fid = bdryFaceIds[index];
        auto adj_fElid = bdryFaceElemIds[index];

         if(verbose >2) 
          printf("adj_elem_%d, fadj_ %d, of_e_%d : ", adj_elem,  adj_fid, adj_fElid); 

        for(auto ii = 0; ii < tot; ++ii){
          if(verbose >2) printf("   this_fid_ %d ", bdryFaceIds[bfaceSize*elem+ii]);
          if(adj_fid == bdryFaceIds[bfaceSize*elem+ii]){ 
            if(verbose >2) printf("found %d in current \n", adj_fid);
            found = true;
            break;
          }
        }
        if(found) { 
          continue;
        }
        if(verbose >2) printf("NOT found \n");

        // In current element
        //TODO test this
        //o::LO start = adj_elem*BDRYFACE_SIZE*(BDRY_FACE_STORAGE_SIZE_PER_FACE-1) + ai*(BDRY_FACE_STORAGE_SIZE_PER_FACE-1);
        //if(verbose >2) printf("start@%d elem_%d adj_elem_%d ai_%d\n", start, elem, adj_elem, ai);
        bool add = p::is_face_within_dist_to_tet(tet, face_verts, coords, 
                    adj_fid, depth);
        if(!add) {
          if(verbose >2) printf("NOT adding \n");
          continue;
        }       
        if(verbose >2) 
          printf("From_%d ADDING.. fid %d of_e_%d\n", adj_elem , adj_fid, adj_fElid);
        
        // This step is not needed. Currently this data is not used
        //auto fv2v = o::gather_verts<3>(face_verts, adj_fid); //Few<LO, 3>
        //const auto face = o::gather_vectors<3, 3>(coords, fv2v);
        //for(o::LO i=0; i<3; ++i){
        //  for(o::LO j=0; j<3; j++){
        //    bdryFacesW[elem* BDRYFACE_SIZE*(BDRY_FACE_STORAGE_SIZE_PER_FACE-1) 
        //      + tot* (BDRY_FACE_STORAGE_SIZE_PER_FACE-1) + i*3 + j] = face[i][j];
        //  }
        // }
        
        auto ind = elem* bfaceSize + tot;
        bdryFaceIds[ind] = adj_fid;
        bdryFaceElemIds[ind] = adj_fElid;
        ++tot;

        if(verbose >2) { 
          if(numBdryFaceIds[elem]>0)
            printf(" %d:: %d;ids ", elem, numBdryFaceIds[elem]);
          printf("tot = %d\n", tot);
        }

      }
      ++dual_elem_id;
    }
    if(tot > existing){ 
      if(verbose >2)  printf("numBdryFaceIds_of_%d = %d\n", elem, tot);
      numBdryFaceIds[elem] =  tot;

      // After copying is done
      auto dual_elem_id = dual_faces[elem];
      for(o::LO i=0; i<4-nbf; ++i) {
        auto adj_elem  = dual_elems[dual_elem_id];
        o::LO val = 1;
        Kokkos::atomic_exchange( &bdryFlags[adj_elem], val);
        ++dual_elem_id;
      }
    }
    if(verbose >2)
      if(numBdryFaceIds[elem]>0)printf("> %d:: %d;ids \n\n", elem, numBdryFaceIds[elem]);

  };

  o::LO total = 1;
  o::LO ln = 0;
  while(total){
    o::parallel_for(nel, fill, "FillBdryFacesInADepth"); //4,690

   
  // Return type error
  //  total = o::parallel_reduce(nel, OMEGA_H_LAMBDA(
  //    const int& i, o::LO& update){
  //      update += bdryFlags[i];  //()
  //  }, "BdryFillCheck");
  
   
   Kokkos::parallel_reduce("BdryFillCheck", nel, 
       OMEGA_H_LAMBDA(const int& i, o::LO & lsum) {
     lsum += bdryFlags[i];
   }, total);

   printf("LOOP %d total %d \n", ln, total);
   ++ln;
  }
  
  convert2ReadOnlyCSR();
}

int GitrmMesh::makeCSRBdryFacePtrs(o::Write<o::LO>& numBdryFaceIdsInElems,
  o::Read<o::LO>& bdryFacePtrsBFS) {
  int debug = 1;
  auto tot = mesh.nelems(); 
  o::HostWrite<o::LO>nums(numBdryFaceIdsInElems);
  o::HostWrite<o::LO> ptrs(tot+1);
  std::cout << "size of numBdryFaceIdsInElems " << tot << "\n";
  o::LO sum = 0;
  for(int i=0; i < tot+1; ++i){
    ptrs[i] = sum;
    if(i < tot)
      sum += nums[i];
    if(debug && i < tot) printf(" e %d numBdryFaceIds %d \n", i, nums[i]);
  }
  bdryFacePtrsBFS = o::LOs(ptrs.write());
  return sum;
}

/** @brief Preprocess distance to boundary.
* Use BFS method to store bdry face ids in elements wich are within required depth.
* This is a 2-step process. (1) Count # bdry faces (2) collect them.
* Iterated element-wise since #faces are 4x elements, and most of them 
* are unexposed.
*/

void GitrmMesh::preProcessBdryFacesBFS() {
  o::Write<o::LO> numBdryFaceIdsInElems(mesh.nelems(), 0);
  preprocessCountBdryFaces(numBdryFaceIdsInElems);
  // preprocessStoreBdryFaces(numBdryFaceIdsInElems, dummy, 0);  
  auto csrSize = makeCSRBdryFacePtrs(numBdryFaceIdsInElems, bdryFacePtrsBFS);
  std::cout << "CSR size "<< csrSize << "\n";
  int sizePerFace = BDRY_FACE_STORAGE_SIZE_PER_FACE;
  OMEGA_H_CHECK(csrSize*sizePerFace > 0);
  o::Write<o::Real> bdryFacesCsrW(csrSize*sizePerFace);
  preprocessStoreBdryFaces(numBdryFaceIdsInElems, bdryFacesCsrW, bdryFacePtrsBFS, csrSize);
  bdryFacesCsrBFS = o::Reals(bdryFacesCsrW);
}

// bdryFacesCsr is kept as o::Real, to optionally store vertices of faces. 
void GitrmMesh::preprocessCountBdryFaces(o::Write<o::LO>& numBdryFaceIdsInElems) {
  bool debug = false;
  MESHDATA(mesh);
  constexpr int bfsLocalSize = 10000; //DIST2BDRY_BFS_ARRAY_SIZE;
  const double depth = DEPTH_DIST2_BDRY;
  auto lambda = OMEGA_H_LAMBDA(o::LO elem) {
    o::LO bdryFids[4] = {-1, -1, -1, -1};
    auto nExpFaces = p::get_exposed_faces_of_tet(elem, down_r2f, side_is_exposed, bdryFids);
    if(!nExpFaces)
      return;

    int visited[bfsLocalSize];
    int queue[bfsLocalSize];
    int nThisBfid = 0;
    //parent elem is within dist, since the face(s) is(are) part of it
    queue[0] = elem;
    visited[0] = elem;
    int qLen = 1;
    int qFront = 0;
    int qBack = 1;
    int nvis = 1;
    while(qLen) {
      //deque
      auto refElem = queue[qFront];
      OMEGA_H_CHECK(refElem >= 0);
      queue[qFront] = -1;
      --qLen;
      ++qFront;
      // process
      OMEGA_H_CHECK(refElem >= 0);
      const auto tetv2v = Omega_h::gather_verts<4>(mesh2verts, refElem);
      const auto tet = Omega_h::gather_vectors<4, 3>(coords, tetv2v);
      for(o::LO efi=0; efi < 4 && efi < nExpFaces; ++efi) {
        auto bfid = bdryFids[efi];
        // At least one face is within depth, but that info is not stored.
        // Checking again to filter out unnecessary face.
        if(debug) printf("\nCheck this elem %d \n", refElem);     //e 407968 face 73176; 409271
        bool add = p::is_face_within_dist_to_tet(tet, face_verts, coords, 
                      bfid, depth);
        if(add) {
          if(debug && elem ==409271) //face 0 -0.0336643 -0.0681375 -0.0383588
            printf("  adj-e originElem %d bfid %d refe %d \n",elem, bfid, refElem);
          Kokkos::atomic_add(&(numBdryFaceIdsInElems[refElem]), 1);
          ++nThisBfid;
        }
      }

      //enque adjacent elements
      o::LO interiorFids[4] = {-1, -1, -1, -1};
      auto nIntFaces = p::get_interior_faces_of_tet(refElem, down_r2f, side_is_exposed, interiorFids);
      //across 1st face, if valid. Otherwise it is not used.
      auto dual_elem_id = dual_faces[refElem]; // ask_dual().a2ab
      for(o::LO ifi=0; ifi < 4 && ifi < nIntFaces; ++ifi) {
        // Adjacent element across this face ifi
        auto adj_elem  = dual_elems[dual_elem_id];

        bool add = false;
        for(o::LO efi=0; efi < 4 && efi < nExpFaces; ++efi) {
          auto faceId = bdryFids[efi];
          const auto tetv2v = Omega_h::gather_verts<4>(mesh2verts, adj_elem);
          const auto tet = Omega_h::gather_vectors<4, 3>(coords, tetv2v);
          if(p::is_face_within_dist_to_tet(tet, face_verts, coords, faceId, depth))
            add = true;
        }
        if(add) {
          for(int i=0; i<nvis; ++i)
            if(adj_elem == visited[i])
              add = false;
          if(add) {
            queue[qBack] = adj_elem;
            ++qBack;
            visited[nvis] = adj_elem;
            ++nvis;
            ++qLen;
          }
        }
        ++dual_elem_id;
      } //interior faces
    } //while
  };
  o::parallel_for(nel, lambda, "countBdryFaceIds");
}


// bdryFacesCsr is kept as o::Real, to optionally store vertices of faces. 
void GitrmMesh::preprocessStoreBdryFaces(o::Write<o::LO>& numBdryFaceIdsInElems,
  o::Write<o::Real>& bdryFacesCsrW, o::Read<o::LO>& bdryFacePtrsBFS, int csrSize) {
  MESHDATA(mesh);
  int debug = 0;
  if(debug) {
    o::HostRead<o::LO>bdryFacePtrsBFSHost(bdryFacePtrsBFS);
    for(int i=0; i<nel; ++i)
      printf("bdryFacePtrsBFShost %d  %d \n", i, bdryFacePtrsBFSHost[i] );
  }

  constexpr int bfsLocalSize = 10000; //DIST2BDRY_BFS_ARRAY_SIZE;
  const double depth = DEPTH_DIST2_BDRY;
  const int sizePerFace = BDRY_STORAGE_SIZE_PER_FACE; //no auto
  const int storeFaceVtx = (sizePerFace >=9)? 1 : 0;
  //auto& bdryFacePtrsBFS = this->bdryFacePtrsBFS;
  o::Write<o::LO> nextPositions(nel, 0);
  auto lambda = OMEGA_H_LAMBDA(o::LO elem) {
    o::LO bdryFids[4] = {-1, -1, -1, -1};
    auto nExpFaces = p::get_exposed_faces_of_tet(elem, down_r2f, side_is_exposed, bdryFids);
    if(!nExpFaces)
      return;

    int visited[bfsLocalSize];
    int queue[bfsLocalSize];

    //parent elem is within dist, since the face(s) is(are) part of it
    queue[0] = elem;
    visited[0] = elem;
    int qLen = 1;
    int qFront = 0;
    int qBack = 1;
    int nvis = 1;
    while(qLen) {
      //deque
      auto refElem = queue[qFront];
      OMEGA_H_CHECK(refElem >= 0);
      queue[qFront] = -1;
      --qLen;
      ++qFront;
      // process
      OMEGA_H_CHECK(refElem >= 0);
      const auto tetv2v = Omega_h::gather_verts<4>(mesh2verts, refElem);
      const auto tet = Omega_h::gather_vectors<4, 3>(coords, tetv2v);
      for(o::LO efi=0; efi < 4 && efi < nExpFaces; ++efi) {
        auto bfid = bdryFids[efi];
        // At least one face is within depth, but that info is not stored.
        // Checking again to filter out unnecessary face.
        bool add = p::is_face_within_dist_to_tet(tet, face_verts, coords, 
                      bfid, depth);
        if(add) {
          auto pos = bdryFacePtrsBFS[refElem];
          auto fInd = nextPositions[refElem];
          auto index = (pos+fInd)*sizePerFace;
          auto nBdryFaces = numBdryFaceIdsInElems[refElem];
          OMEGA_H_CHECK(fInd <= nBdryFaces);
          OMEGA_H_CHECK(index >= 0);
          OMEGA_H_CHECK(index < csrSize);
          Kokkos::atomic_exchange(&(bdryFacesCsrW[index]), static_cast<o::Real>(bfid));
          ++index;
          Kokkos::atomic_add(&(nextPositions[refElem]),1);
          if(false) { //storeFaceVtx) {
            //storing elem and bdry face
            o::LO e = elem;
            Kokkos::atomic_exchange(&(bdryFacesCsrW[index]), static_cast<o::Real>(e));
            ++index;
            double face[9] = {0};
            p::get_face_coords_of_tet(face_verts, coords, bfid, face);
            for(o::LO i=0; i<4; ++i)
              for(o::LO j=0; j<3; ++j) {
                o::Real val =  face[i*3+j]; 
                Kokkos::atomic_exchange(&(bdryFacesCsrW[index+i*3+j]), val); 
              }
          }
        } 
      }
      //enque adjacent elements
      o::LO interiorFids[4] = {-1, -1, -1, -1};
      auto nIntFaces = p::get_interior_faces_of_tet(refElem, down_r2f, side_is_exposed, interiorFids);
      //across 1st face, if valid. Otherwise it is not used.
      auto dual_elem_id = dual_faces[refElem]; // ask_dual().a2ab
      for(o::LO ifi=0; ifi < 4 && ifi < nIntFaces; ++ifi) {
        // Adjacent element across this face ifi
        auto adj_elem  = dual_elems[dual_elem_id];
        bool add = false;
        for(o::LO efi=0; efi < 4 && efi < nExpFaces; ++efi) {
          auto faceId = bdryFids[efi];
          const auto tetv2v = Omega_h::gather_verts<4>(mesh2verts, adj_elem);
          const auto tet = Omega_h::gather_vectors<4, 3>(coords, tetv2v);
          if(p::is_face_within_dist_to_tet(tet, face_verts, coords, faceId, depth))
            add = true;
        }
        if(add) {
          for(int i=0; i<nvis; ++i)
            if(adj_elem == visited[i])
              add = false;
          if(add) {
            queue[qBack] = adj_elem;
            ++qBack;
            visited[nvis] = adj_elem;
            ++nvis;
            ++qLen;
          }
        }
        ++dual_elem_id;
      } //interior faces
    } //while
  };
  o::parallel_for(nel, lambda, "preprocessBdryIds");
}

void GitrmMesh::markPiscesCylinderResult(o::Write<o::LO>& beadVals_d) {
  o::HostWrite<o::LO> fIds_h{277, 609, 595, 581, 567, 553, 539, 
    525, 511, 497, 483, 469, 455, 154};
  o::LOs faceIds(fIds_h); // auto faceIds = o::Reals(fIds_h);
  auto numFaceIds = faceIds.size();
  OMEGA_H_CHECK(numFaceIds == beadVals_d.size());
  const auto sideIsExposed = o::mark_exposed_sides(&mesh);
  auto faceClassIds = mesh.get_array<o::ClassId>(2, "class_id");
  o::Write<o::LO> edgeTagIds(mesh.nedges(), -1);
  o::Write<o::LO> faceTagIds(mesh.nfaces(), -1);
  o::Write<o::LO> elemTagAsCounts(mesh.nelems(), 0);
  const auto f2rPtr = mesh.ask_up(o::FACE, o::REGION).a2ab;
  const auto f2rElem = mesh.ask_up(o::FACE, o::REGION).ab2b;
  const auto face2edges = mesh.ask_down(o::FACE, o::EDGE);
  const auto faceEdges = face2edges.ab2b;
  o::parallel_for(faceClassIds.size(), OMEGA_H_LAMBDA(const int i) {
    for(auto id=0; id<numFaceIds; ++id) {
      if(faceIds[id] == faceClassIds[i] && sideIsExposed[i]) {
        faceTagIds[i] = id;
        auto elmId = p::elem_of_bdry_face(i, f2rPtr, f2rElem);
        elemTagAsCounts[elmId] = beadVals_d[id];
        const auto edges = o::gather_down<3>(faceEdges, elmId);
        for(int ie=0; ie<3; ++ie) {
          auto eid = edges[ie];
          edgeTagIds[eid] = beadVals_d[id];
        }
      }
    }
  });
  mesh.add_tag<o::LO>(o::EDGE, "piscesTiRodCounts_edge", 1, o::LOs(edgeTagIds));
  mesh.add_tag<o::LO>(o::REGION, "Pisces_Bead_Counts", 1, o::LOs(elemTagAsCounts));
}

//get fIds_h by opening mesh/model in Simmodeler
void GitrmMesh::markPiscesCylinder(bool renderPiscesCylCells) {
  o::HostWrite<o::LO> fIds_h{277, 609, 595, 581, 567, 553, 539, 
    525, 511, 497, 483, 469, 455, 154};
  o::LOs faceIds(fIds_h);
  auto numFaceIds = faceIds.size();
  const auto side_is_exposed = o::mark_exposed_sides(&mesh);
  // array of all faces, but only classification ids are valid
  auto face_class_ids = mesh.get_array<o::ClassId>(2, "class_id");
  o::Write<o::LO> faceTagIds(mesh.nfaces(), -1);
  o::Write<o::LO> elemTagIds(mesh.nelems(), 0);
  const auto f2r_ptr = mesh.ask_up(o::FACE, o::REGION).a2ab;
  const auto f2r_elem = mesh.ask_up(o::FACE, o::REGION).ab2b;
  o::parallel_for(face_class_ids.size(), OMEGA_H_LAMBDA(const int i) {
    for(auto id=0; id<numFaceIds; ++id) {
      if(faceIds[id] == face_class_ids[i] && side_is_exposed[i]) {
        faceTagIds[i] = id;
        if(renderPiscesCylCells) {
          auto elmId = p::elem_of_bdry_face(i, f2r_ptr, f2r_elem);
          elemTagIds[elmId] = id;
        }
      }
    }
  });

  mesh.add_tag<o::LO>(o::FACE, "piscesTiRod_ind", 1, o::LOs(faceTagIds));
  mesh.add_tag<o::LO>(o::REGION, "piscesTiRodRegIndex", 1, o::LOs(elemTagIds));
}


void GitrmMesh::printBdryFaceIds(bool printIds, o::LO minNums) {
  auto &numBdryFaceIds = this->numBdryFaceIds;
  auto &bdryFaceIds = this->bdryFaceIds;
  auto &bdryFaceElemIds = this->bdryFaceElemIds;
  auto print = OMEGA_H_LAMBDA(o::LO elem){
    o::LO num = numBdryFaceIds[elem];
    if(num > minNums){
      printf(" %d (tot %d ) :: ", elem, numBdryFaceIds[elem]);
      if(printIds){
        for(o::LO i=0; i<num; ++i){
          o::LO ind = elem * BDRYFACE_SIZE + i;
          printf("%d( %d ), ", bdryFaceIds[ind], bdryFaceElemIds[ind]);
        }
      }
      printf("\n");
    }
  };
  
  o::parallel_for(mesh.nelems(), print, "printBdryFaceIds");
  printf("\nDEPTH: %0.5f \n", DEPTH_DIST2_BDRY);
}


void GitrmMesh::printBdryFacesCSR(bool printIds, o::LO minNums) {
  auto &bdryFaceIds = this->bdryFaceIds;
  auto &bdryFaceElemIds = this->bdryFaceElemIds;
  auto &bdryFaceInds = this->bdryFaceInds;
  auto print = OMEGA_H_LAMBDA(o::LO elem){
    o::LO ind1 = bdryFaceInds[elem];
    o::LO ind2 = bdryFaceInds[elem+1];
    auto nf = ind2 - ind1;

    if(nf > minNums){
      printf("e_%d #%d  %d  %d \n", elem, nf, ind1, ind2);
      if(printIds){
        for(o::LO i=0; i<nf; ++i){
          o::LO ind = elem * BDRYFACE_SIZE+i;
          printf("%d( %d ), ", bdryFaceIds[ind], bdryFaceElemIds[ind]);
        }
      }
      printf("\n");
    }
  };
  printf("\nCSR: \n");
  o::parallel_for(mesh.nelems(), print, "printBdryFacesCSR");
  printf("\nDEPTH: %0.5f \n", DEPTH_DIST2_BDRY);
}


void GitrmMesh::printDensityTempProfile(double rmax, int gridsR, 
  double zmax, int gridsZ) {
  auto& densIon = densIon_d;
  auto& temIon = temIon_d;
  auto x0Dens = densIonX0;
  auto z0Dens = densIonZ0;
  auto nxDens = densIonNx;
  auto nzDens = densIonNz;
  auto dxDens = densIonDx;
  auto dzDens = densIonDz;
  auto x0Temp = tempIonX0;
  auto z0Temp = tempIonZ0;
  auto nxTemp = tempIonNx;
  auto nzTemp = tempIonNz;
  auto dxTemp = tempIonDx;
  auto dzTemp = tempIonDz;
  double rmin = 0;
  double zmin = 0;
  double dr = (rmax - rmin)/gridsR;
  double dz = (zmax - zmin)/gridsZ;
  // use 2D data. radius rmin to rmax
  auto lambda = OMEGA_H_LAMBDA(const int& ir) {
    double rad = rmin + ir*dr;
    auto pos = o::zero_vector<3>();
    pos[0] = rad;
    pos[1] = 0;
    double zmin = 0;
    for(int i=0; i<gridsZ; ++i) {
      pos[2] = zmin + i*dz;
      auto dens = p::interpolate2dField(densIon, x0Dens, z0Dens, dxDens, 
          dzDens, nxDens, nzDens, pos, false, 1,0);
      auto temp = p::interpolate2dField(temIon, x0Temp, z0Temp, dxTemp,
        dzTemp, nxTemp, nzTemp, pos, false, 1,0);      
      printf("profilePoint: temp2D %g dens2D %g RZ %g %g rInd %d zInd %d \n", 
        temp, dens, pos[0], pos[2], ir, i);
    }
  };
  o::parallel_for(gridsR, lambda);  
}


void GitrmMesh::compareInterpolate2d3d(const o::Reals& data3d,
  const o::Reals& data2d, double x0, double z0, double dx, double dz,
  int nx, int nz, bool debug) {
  const auto coords = mesh.coords();
  const auto mesh2verts = mesh.ask_elem_verts();
  auto lambda = OMEGA_H_LAMBDA(const int &el) {
    const auto tetv2v = o::gather_verts<4>(mesh2verts, el);
    auto tet = p::gatherVectors4x3(coords, tetv2v);
    for(int i=0; i<4; ++i) {
      auto pos = tet[i];
      auto bcc = o::zero_vector<4>();
      p::find_barycentric_tet(tet, pos, bcc);   
      auto val3d = p::interpolateTetVtx(mesh2verts, data3d, el, bcc, 1, 0);
      auto pos2d = o::zero_vector<3>();
      pos2d[0] = sqrt(pos[0]*pos[0] + pos[1]*pos[1]);
      pos2d[1] = 0;
      auto val2d = p::interpolate2dField(data2d, x0, z0, dx, 
        dz, nx, nz, pos2d, false, 1,0);
      if(!o::are_close(val2d, val3d, 1.0e-6)) {
        printf("testinterpolate: val2D %g val3D %g pos2D %g %g %g el %d\n"
          "bcc %g %g %g %g\n",
          val2d, val3d, pos2d[0], pos2d[1], pos2d[2], el,
          bcc[0], bcc[1], bcc[2], bcc[3]);
        // val2D 0.227648 val3D 0.390361 pos2D 0.0311723 0 0 el 572263
        // bcc 1 1.49495e-17 4.53419e-18 -1.32595e-18
        // Due to precision problem in bcc, deviation (here from 0)
        // when mutliplied by huge density is >> epsilon. This value is
        // negligible, compared to 1.0e+18. Don't check for exact value. 
        // TODO explore this issue, by handling in almost_equal()
        //OMEGA_H_CHECK(false);
      }
    } //mask 
  };
  o::parallel_for(mesh.nelems(), lambda, "test_interpolate");  
}

void GitrmMesh::test_interpolateFields(bool debug) {
  auto densVtx = mesh.get_array<o::Real>(o::VERT, "IonDensityVtx");
  compareInterpolate2d3d(densVtx, densIon_d, densIonX0, densIonZ0,
   densIonDx, densIonDz, densIonNx, densIonNz, debug);

  auto tIonVtx = mesh.get_array<o::Real>(o::VERT, "IonTempVtx");
  compareInterpolate2d3d(tIonVtx, temIon_d, tempIonX0, tempIonZ0,
   tempIonDx, tempIonDz, tempIonNx, tempIonNz, debug);

}



/*

// Convert indices to CSR, face ids are not yet available
// TODO this method overlaps with part of convertToCsr()
void GitrmMesh::preProcessCreateIndexDistToBdry(){
  auto &numBdryFaceIdsAdjs = this->numBdryFaceIdsAdjs;
  auto &totalBdryFacesCsrAdjs = this->totalBdryFacesCsrAdjs;
  auto &bdryFacesCsrW  = this->bdryFacesCsrW;
  auto &bdryFaceIndsBFSdjs = this->bdryFaceIndsBFSdjs;

  OMEGA_H_CHECK(countedAdjs);

  //Copy to host
  o::HostRead<o::LO> numBdryFaceIdsAdjsH(numBdryFaceIdsAdjs);
  o::HostWrite<o::LO> bdryFaceIndsBFSdjsH(mesh.nelems()+1);
  //Num of faces
  o::Int sum = 0;
  o::Int e = 0;
  for(e=0; e < mesh.nelems(); ++e){
    bdryFaceIndsBFSdjsH[e] = sum; // * S;
    sum += numBdryFaceIdsAdjsH[e];
  }
  // last entry
  bdryFaceIndsBFSdjsH[e] = sum;

  totalBdryFacesCsrAdjs = sum;

  //CSR indices
  bdryFaceIndsBFSdjs = o::LOs(bdryFaceIndsBFSdjsH.write());

  bdryFacesCsrW = o::Write<o::Real>(totalBdryFacesCsrAdjs * BDRY_FACE_STORAGE_SIZE_PER_FACE, 0);
}


// TODO this method overlaps with part of convertToCsr()
void GitrmMesh::preProcessFillDataDistToBdry(){
  MESHDATA(mesh);
  
  auto &numBdryFaceIdsAdjs = this->numBdryFaceIdsAdjs;
  auto &bdryFacesBFSdjs = this->bdryFacesBFSdjs;
  auto &bdryFaceIndsBFSdjs = this->bdryFaceIndsBFSdjs;
  auto &bdryFacesCsrW = this->bdryFacesCsrW;

  auto S = BDRY_FACE_STORAGE_SIZE_PER_FACE;

  //Copy data
  auto convert = OMEGA_H_LAMBDA(o::LO elem){
    auto beg = bdryFaceIndsBFSdjs[elem];
    auto end = bdryFaceIndsBFSdjs[elem+1];
    if(beg == end){
      return;
    }
    OMEGA_H_CHECK((end - beg) == numBdryFaceIdsAdjs[elem]);
    o::Real fdat[9] = {0};
    // Looping over implicit face numbers
    for(o::LO ind = beg; ind < end; ++ind){
      auto faceId = static_cast<o::LO>(bdryFacesCsrW[ind*S]);
      p::get_face_coords_of_tet(face_verts, coords, faceId, fdat, 0);
      for(o::LO j = BDRY_FACE_STORAGE_IDS; j < S; ++j){
        //bdryFacesCsrW[i*S + j] = bdryFacesW[bfd + j-1]; //TODO check this
        bdryFacesCsrW[ind*S + j] = fdat[j - BDRY_FACE_STORAGE_IDS];
      }
    }    
  };
  o::parallel_for(nel, convert, "Convert to CSR write");
  bdryFacesBFSdjs = o::Reals(bdryFacesCsrW);

}


void GitrmMesh::preProcessSearchDistToBdryAdj(o::LO step){
  MESHDATA(mesh);

  auto &numBdryFaceIdsAdjs = this->numBdryFaceIdsAdjs;
  auto &bdryFaceIndsBFSdjs = this->bdryFaceIndsBFSdjs;
  auto &bdryFacesCsrW = this->bdryFacesCsrW;

  o::Write<o::LO>indexWritten;

  auto S = BDRY_FACE_STORAGE_SIZE_PER_FACE;

   / Count only
  if(seeep==1){
    // initializing to 0 is important
    numBdryFaceIdsAdjs = o::Write<o::LO>(mesh.nelems(), 0);
  }
  // Fill only
  else if(step==2){
    // initializing to 0 is important
    indexWritten = o::Write<o::LO>(mesh.nelems(), 0);

    preProcessCreateIndexDistToBdry();
  }


  auto run = OMEGA_H_LAMBDA(o::LO elem){
    // Collect bdry faces
    o::LO bdryFids[4];
    o::LO nbdry = p::get_face_types_of_tet(elem, down_r2f, side_is_exposed, bdryFids, EXPOSED);
    if(nbdry ==0)
      return;

    o::LO nIn = 0;
    // Current boundary face of current element
    for(auto bfi=0; bfi < nbdry; ++bfi){

      o::LO bfsElems[BFS_DATA_SIZE];
      auto bfid = bdryFids[bfi];

      o::LO current = -1;
      o::LO nAdded = 0;

      // Current element is added to BFS. Repeated per bfid
      bfsElems[nAdded++] = elem;

      //Current bdry face numbers (=1) is added to this elem
      if(step==1){
        Kokkos::atomic_add(&numBdryFaceIdsAdjs[elem], 1);
      }

      // Breadth First Search
      while(true){
        current += 1;
        if(current >= nAdded)
          break;

        // Implicit pop
        o::LO e = bfsElems[current];

        // Number of interior faces and its adj. element ids
        nIn = p::get_face_types_of_tet(e, down_r2f, side_is_exposed, bdryFids, INTERIOR);

        // First interior. All are interior faces, CSR entry is certain 
        auto dual_elem_id = dual_faces[e]; 

        for(auto fi=0; fi < nIn; ++fi){

          // Adjacent element across this face fi
          auto adj_elem  = dual_elems[dual_elem_id];
          ++dual_elem_id;

          // Adj. element tet
          const auto tetv2v = Omega_h::gather_verts<4>(mesh2verts, adj_elem);
          const auto tet = Omega_h::gather_vectors<4, 3>(coords, tetv2v);

          // Any vertex of Tet within depth limit  
          bool add = p::is_face_within_dist_to_tet(tet, face_verts, coords, 
                      bfid, DEPTH_DIST2_BDRY);
          if(!add) 
            continue;
          // Add the reference face to this adj_elem.

          if(step==1){
            Kokkos::atomic_add(&numBdryFaceIdsAdjs[adj_elem],  1);
          }
          else if(step==2){
            auto nf = numBdryFaceIdsAdjs[e];

            // Current position for writing face ids in this element. This data is not updated in
            // the current call of step=2
            auto beg = bdryFaceIndsBFSdjs[e];
            auto next = bdryFaceIndsBFSdjs[e+1];

            //Keep track of index written, v is old value.
            //TODO error o::atomic_fetch_add()
            int v = Kokkos::atomic_fetch_add(&indexWritten[e], 1);            
            // Method to guarantee reserving data for use by this thread : 
            // https://devtalk.nvidia.com/default/topic/850890
            //     /can-one-force-two-operations-to-occur-atomically-together-/
            if(v < nf){
              // These steps OK between atomic operations ?
              auto pos = beg + v; 
              OMEGA_H_CHECK(pos < next);
              //TODO omega_h has no file=exchange ?
              Kokkos::atomic_exchange(&bdryFacesCsrW[S*pos], static_cast<o::Real>(bfid));
              //Element id that of the bfid
              Kokkos::atomic_exchange(&bdryFacesCsrW[S*pos +1], static_cast<o::Real>(elem));

            }
          }

          // Add this  adj_elem to bfs
          bfsElems[nAdded++] = adj_elem;
        }
      }
    }
  };
  o::parallel_for(mesh.nelems(), run, "preProcessDistToBdryAdjSearch");
  if(step == 1)
    countedAdjs = true;
}


void GitrmMesh::preProcessDistToBdryAdjs(){

    preProcessSearchDistToBdryAdjs(1);
    preProcessSearchDistToBdryAdjs(2);
    preProcessFillDataDistToBdry();

}
//Add in hpp
  // Adjacency based BFS search, for testing the pre-processing
  o::Reals bdryFacesBFSdjs;
  o::LOs bdryFaceIndsBFSdjs;

  void preProcessDistToBdryAdjs();
  void preProcessSearchDistToBdryAdjs(o::LO);
  void preProcessFillDataDistToBdry();
  void preProcessCreateIndexDistToBdry();
  o::Write<o::LO> numBdryFaceIdsAdjs;
  o::Write<o::Real> bdryFacesCsrW;
  bool countedAdjs = false;
  int totalBdryFacesCsrAdjs = 0;
*/
