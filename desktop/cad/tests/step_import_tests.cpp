#include "prometheus/cad/step_importer.hpp"
#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
bool valid_external_bounds(const prometheus::cad::AssemblyNode& node) {
  const auto& bounds=node.bounds;
  const std::array<double,6> values{bounds.min_x,bounds.min_y,bounds.min_z,bounds.max_x,bounds.max_y,bounds.max_z};
  if(!std::ranges::all_of(values,[](const double value){return std::isfinite(value);})||
     bounds.min_x>bounds.max_x||bounds.min_y>bounds.max_y||bounds.min_z>bounds.max_z)return false;
  constexpr double pinned_yubi_maximum_span_m=10.0;
  return bounds.max_x-bounds.min_x<pinned_yubi_maximum_span_m&&
         bounds.max_y-bounds.min_y<pinned_yubi_maximum_span_m&&
         bounds.max_z-bounds.min_z<pinned_yubi_maximum_span_m;
}
}

int main(int argc,char** argv){
  if(argc==3&&std::string(argv[1])=="--import-only"){
    try{const auto result=prometheus::cad::StepImporter{}.import_file(argv[2],0.0015,false);std::size_t leaves=0,triangles=0;bool invalid_bounds=false;const auto visit=[&](const auto& self,const auto& node)->void{if(!node.mesh.positions.empty()&&!valid_external_bounds(node)){const auto& bounds=node.bounds;std::cerr<<"external assembly bounds invalid: id="<<node.persistent_id<<" bounds_m=["<<bounds.min_x<<','<<bounds.min_y<<','<<bounds.min_z<<"]-["<<bounds.max_x<<','<<bounds.max_y<<','<<bounds.max_z<<"]\n";invalid_bounds=true;}if(node.children.empty()){++leaves;triangles+=node.mesh.indices.size()/3;}for(const auto& child:node.children)self(self,child);};for(const auto& root:result.roots)visit(visit,root);if(invalid_bounds)return 12;for(const auto& warning:result.warnings)if(warning.find("topology nodes could not be bounded or tessellated")!=std::string::npos){std::cerr<<"finite external geometry was omitted from broad phase: "<<warning<<"\n";return 13;}if(leaves<2||triangles==0){std::cerr<<"external assembly import insufficient: leaves="<<leaves<<" triangles="<<triangles<<"\n";return 10;}std::cout<<"Imported roots="<<result.roots.size()<<" leaves="<<leaves<<" triangles="<<triangles<<" interferences=deferred\n";return 0;}catch(const std::exception& e){std::cerr<<"external import failed: "<<e.what()<<"\n";return 11;}
  }
  const bool keep_fixture=argc>1; const auto path=keep_fixture?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/"prometheus-motor-arm.step";
  Handle(TDocStd_Document) doc; XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc); const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  const auto base=tool->AddShape(BRepPrimAPI_MakeBox(160,20,100).Shape(),false); TDataStd_Name::Set(base,"Base plate");
  const auto arm=tool->AddShape(BRepPrimAPI_MakeBox(200,15,30).Shape(),false); TDataStd_Name::Set(arm,"Arm");
  const auto motor=tool->AddShape(BRepPrimAPI_MakeCylinder(30,70).Shape(),false);TDataStd_Name::Set(motor,"Motor placeholder");
  TopoDS_Compound compound; BRep_Builder builder; builder.MakeCompound(compound); const auto assembly=tool->AddShape(compound,true); TDataStd_Name::Set(assembly,"Motor arm assembly");tool->AddComponent(assembly,base,TopLoc_Location());gp_Trsf motor_transform;motor_transform.SetTranslationPart(gp_Vec(80,20,50));tool->AddComponent(assembly,motor,TopLoc_Location(motor_transform));gp_Trsf arm_transform;arm_transform.SetTranslationPart(gp_Vec(80,55,50));tool->AddComponent(assembly,arm,TopLoc_Location(arm_transform));
  tool->UpdateAssemblies(); STEPCAFControl_Writer writer; writer.SetNameMode(true); if(!writer.Transfer(doc,STEPControl_AsIs)||writer.Write(path.string().c_str())!=IFSelect_RetDone){std::cerr<<"fixture write failed\n";return 1;}
  const auto result=prometheus::cad::StepImporter{}.import_file(path); const auto assembly_it=std::find_if(result.roots.begin(),result.roots.end(),[](const auto& node){return node.children.size()==3;}); if(assembly_it==result.roots.end()){std::cerr<<"hierarchy not preserved; roots="<<result.roots.size()<<"\n";for(const auto& root:result.roots)std::cerr<<root.name<<" children="<<root.children.size()<<"\n";return 2;} if(assembly_it->children.front().mesh.indices.empty()){std::cerr<<"tessellation missing\n";return 3;}if(assembly_it->children.front().surface_area_m2<=0||assembly_it->children.front().face_count<=0||assembly_it->children.front().edge_count<=0){std::cerr<<"B-Rep topology metadata missing\n";return 4;}if(result.interferences.size()!=1||result.interferences.front().volume_m3<=0){std::cerr<<"exact static interference was not detected\n";return 4;}
  const auto arm_node=std::find_if(assembly_it->children.begin(),assembly_it->children.end(),[](const auto& node){return node.name=="Arm";}),motor_node=std::find_if(assembly_it->children.begin(),assembly_it->children.end(),[](const auto& node){return node.name=="Motor placeholder";});if(arm_node==assembly_it->children.end()||motor_node==assembly_it->children.end()){std::cerr<<"named sweep entities missing\n";return 5;}const std::array<double,3> pivot{(motor_node->bounds.min_x+motor_node->bounds.max_x)/2,(motor_node->bounds.min_y+motor_node->bounds.max_y)/2,(motor_node->bounds.min_z+motor_node->bounds.max_z)/2};const auto clear_sweep=prometheus::cad::StepImporter{}.sweep_revolute(path,arm_node->persistent_id,motor_node->persistent_id,pivot,{0,0,1},0,90,19);if(!clear_sweep.empty()){std::cerr<<"clear sweep produced a collision\n";return 6;}const auto collision_sweep=prometheus::cad::StepImporter{}.sweep_revolute(path,arm_node->persistent_id,motor_node->persistent_id,pivot,{0,0,1},-90,0,19);if(collision_sweep.empty()||collision_sweep.front().maximum_volume_m3<=0){std::cerr<<"sampled sweep collision missing\n";return 7;}
  const auto moved_clear=prometheus::cad::StepImporter{}.static_interferences(path,{{motor_node->persistent_id,0,0.20,0}});if(!moved_clear.empty()){std::cerr<<"placement-aware static interference did not clear\n";return 8;}
  const double non_finite=std::numeric_limits<double>::quiet_NaN();try{const auto ignored=prometheus::cad::StepImporter{}.static_interferences(path,{{motor_node->persistent_id,non_finite,0,0}});(void)ignored;std::cerr<<"non-finite placement was accepted\n";return 12;}catch(const std::invalid_argument&){}catch(const std::exception& e){std::cerr<<"non-finite placement failed with the wrong contract: "<<e.what()<<"\n";return 12;}
  try{const auto ignored=prometheus::cad::StepImporter{}.sweep_revolute(path,arm_node->persistent_id,motor_node->persistent_id,{non_finite,0,0},{0,0,1},0,90,19);(void)ignored;std::cerr<<"non-finite sweep was accepted\n";return 13;}catch(const std::invalid_argument&){}catch(const std::exception& e){std::cerr<<"non-finite sweep failed with the wrong contract: "<<e.what()<<"\n";return 13;}
  const auto invalid=std::filesystem::temp_directory_path()/"prometheus-invalid.step"; {std::ofstream stream(invalid);stream<<"not a STEP file";} try{const auto ignored=prometheus::cad::StepImporter{}.import_file(invalid);(void)ignored;std::cerr<<"malformed STEP accepted\n";return 9;}catch(const std::runtime_error&){}
  if(!keep_fixture)std::filesystem::remove(path);std::filesystem::remove(invalid);return 0;
}
