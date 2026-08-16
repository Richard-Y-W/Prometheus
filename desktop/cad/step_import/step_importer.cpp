#include "prometheus/cad/step_importer.hpp"
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <DE_ShapeFixParameters.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Tool.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace prometheus::cad { namespace {
void configure_reader(STEPCAFControl_Reader& reader) {
  DE_ShapeFixParameters parameters;
  parameters.FixSolidMode = DE_ShapeFixParameters::FixMode::NotFix;
  reader.ChangeReader().SetShapeFixParameters(parameters);
  reader.ChangeReader().SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
}
bool read_file_without_automatic_healing(STEPCAFControl_Reader& reader,const std::filesystem::path& path) {
  if(reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) return false;
  configure_reader(reader);
  return true;
}
enum class BoundsSource { unavailable, closed_brep, tessellation };
struct LeafShape { std::string id;TopoDS_Shape shape;Bnd_Box bounds;BoundsSource bounds_source{BoundsSource::unavailable}; };
std::string label_id(const TDF_Label& label) { TCollection_AsciiString id; TDF_Tool::Entry(label,id); return id.ToCString(); }
std::string label_name(const TDF_Label& label,const std::string& fallback) {
  Handle(TDataStd_Name) attr;
  if(label.FindAttribute(TDataStd_Name::GetID(),attr)){ TCollection_AsciiString ascii(attr->Get(),'?' ); return ascii.ToCString(); }
  return fallback;
}
std::array<double,16> matrix(const TopLoc_Location& location) {
  const auto& t=location.Transformation();
  return {t.Value(1,1),t.Value(1,2),t.Value(1,3),t.Value(1,4),t.Value(2,1),t.Value(2,2),t.Value(2,3),t.Value(2,4),t.Value(3,1),t.Value(3,2),t.Value(3,3),t.Value(3,4),0,0,0,1};
}
DisplayMesh tessellate(const TopoDS_Shape& shape,double deflection) {
  DisplayMesh out; if(shape.IsNull()) return out;
  BRepMesh_IncrementalMesh mesh(shape,deflection,false,0.35,true);
  for(TopExp_Explorer ex(shape,TopAbs_FACE);ex.More();ex.Next()){
    const auto face=TopoDS::Face(ex.Current()); TopLoc_Location loc;
    const Handle(Poly_Triangulation) tri=BRep_Tool::Triangulation(face,loc); if(tri.IsNull()) continue;
    const auto offset=static_cast<std::uint32_t>(out.positions.size()/3); const auto tr=loc.Transformation();
    for(Standard_Integer i=1;i<=tri->NbNodes();++i){const auto p=tri->Node(i).Transformed(tr);out.positions.insert(out.positions.end(),{static_cast<float>(p.X()/1000.0),static_cast<float>(p.Y()/1000.0),static_cast<float>(p.Z()/1000.0)});}
    for(Standard_Integer i=1;i<=tri->NbTriangles();++i){Standard_Integer a,b,c;tri->Triangle(i).Get(a,b,c);if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);out.indices.insert(out.indices.end(),{offset+static_cast<std::uint32_t>(a-1),offset+static_cast<std::uint32_t>(b-1),offset+static_cast<std::uint32_t>(c-1)});}
  } return out;
}
bool ordered_finite(const BoundingBox& bounds) {
  const std::array<double,6> values{bounds.min_x,bounds.min_y,bounds.min_z,bounds.max_x,bounds.max_y,bounds.max_z};
  return std::ranges::all_of(values,[](const double value){return std::isfinite(value);})&&
         bounds.min_x<=bounds.max_x&&bounds.min_y<=bounds.max_y&&bounds.min_z<=bounds.max_z;
}
bool closed_brep_bounds(const TopoDS_Shape& shape,BoundingBox& metres) {
  Bnd_Box box;
  try{
    BRepBndLib::Add(shape,box);
    if(box.IsVoid()||box.IsOpen())return false;
    Standard_Real min_x,min_y,min_z,max_x,max_y,max_z;
    box.Get(min_x,min_y,min_z,max_x,max_y,max_z);
    metres={min_x/1000.0,min_y/1000.0,min_z/1000.0,max_x/1000.0,max_y/1000.0,max_z/1000.0};
    return ordered_finite(metres);
  }catch(const Standard_Failure&){return false;}
}
bool finite_mesh_bounds(const DisplayMesh& mesh,BoundingBox& bounds) {
  if(mesh.positions.size()<3||mesh.positions.size()%3!=0)return false;
  const double infinity=std::numeric_limits<double>::infinity();
  BoundingBox candidate{infinity,infinity,infinity,-infinity,-infinity,-infinity};
  for(std::size_t i=0;i<mesh.positions.size();i+=3){
    const double x=mesh.positions[i],y=mesh.positions[i+1],z=mesh.positions[i+2];
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    candidate.min_x=std::min(candidate.min_x,x);candidate.min_y=std::min(candidate.min_y,y);candidate.min_z=std::min(candidate.min_z,z);
    candidate.max_x=std::max(candidate.max_x,x);candidate.max_y=std::max(candidate.max_y,y);candidate.max_z=std::max(candidate.max_z,z);
  }
  if(!ordered_finite(candidate))return false;
  bounds=candidate;
  return true;
}
bool broad_phase_bounds(const BoundingBox& metres,double deflection_m,Bnd_Box& box) {
  if(!ordered_finite(metres)||!std::isfinite(deflection_m))return false;
  const std::array<double,6> millimetres{metres.min_x*1000.0,metres.min_y*1000.0,metres.min_z*1000.0,
                                        metres.max_x*1000.0,metres.max_y*1000.0,metres.max_z*1000.0};
  const double enlargement_mm=std::max(0.0,deflection_m)*1000.0;
  if(!std::ranges::all_of(millimetres,[](const double value){return std::isfinite(value);})||!std::isfinite(enlargement_mm))return false;
  box.SetVoid();
  box.Update(millimetres[0],millimetres[1],millimetres[2],millimetres[3],millimetres[4],millimetres[5]);
  box.Enlarge(enlargement_mm);
  return !box.IsVoid()&&!box.IsOpen();
}
bool finite_shape_broad_phase(const TopoDS_Shape& shape,double deflection_m,Bnd_Box& box) {
  BoundingBox metres;
  if(!closed_brep_bounds(shape,metres)){
    try{
      const auto mesh=tessellate(shape,deflection_m*1000.0);
      if(!finite_mesh_bounds(mesh,metres))return false;
    }catch(const Standard_Failure&){return false;}
  }
  return broad_phase_bounds(metres,deflection_m,box);
}
bool finite_placement(const PartPlacement& placement) {
  const std::array<double,6> values{placement.translation_x_m,placement.translation_y_m,placement.translation_z_m,
                                    placement.rotation_x_deg,placement.rotation_y_deg,placement.rotation_z_deg};
  return std::ranges::all_of(values,[](const double value){return std::isfinite(value);});
}
void validate_placements(const std::vector<PartPlacement>& placements) {
  if(!std::ranges::all_of(placements,finite_placement))throw std::invalid_argument("placement values must be finite");
}
void apply_placements(std::vector<LeafShape>& leaves,const std::vector<PartPlacement>& placements) {
  validate_placements(placements);
  constexpr double pi=3.14159265358979323846;
  constexpr double transformed_deflection_m=0.0005;
  for(auto& leaf:leaves){
    const auto placement=std::find_if(placements.begin(),placements.end(),[&](const auto& value){return value.persistent_id==leaf.id;});
    if(placement==placements.end())continue;
    Standard_Real x1,y1,z1,x2,y2,z2;leaf.bounds.Get(x1,y1,z1,x2,y2,z2);
    const gp_Pnt center((x1+x2)/2,(y1+y2)/2,(z1+z2)/2);
    for(const auto [angle,axis]:{std::pair{placement->rotation_x_deg,gp_Dir(1,0,0)},std::pair{placement->rotation_y_deg,gp_Dir(0,1,0)},std::pair{placement->rotation_z_deg,gp_Dir(0,0,1)}}){if(angle==0)continue;gp_Trsf rotation;rotation.SetRotation(gp_Ax1(center,axis),angle*pi/180.0);leaf.shape=BRepBuilderAPI_Transform(leaf.shape,rotation,true).Shape();}
    gp_Trsf translation;translation.SetTranslation(gp_Vec(placement->translation_x_m*1000,placement->translation_y_m*1000,placement->translation_z_m*1000));leaf.shape=BRepBuilderAPI_Transform(leaf.shape,translation,true).Shape();
    if(!finite_shape_broad_phase(leaf.shape,transformed_deflection_m,leaf.bounds))throw std::runtime_error("transformed STEP shape could not be bounded");
  }
}
BoundsSource populate_geometry(AssemblyNode& node,const TopoDS_Shape& shape,double deflection) {
  if(shape.IsNull())return BoundsSource::unavailable;
  try{
  node.mesh=tessellate(shape,deflection*1000.0);
  const auto bounds_source=closed_brep_bounds(shape,node.bounds)?BoundsSource::closed_brep:
                           finite_mesh_bounds(node.mesh,node.bounds)?BoundsSource::tessellation:
                           BoundsSource::unavailable;
  if(bounds_source==BoundsSource::unavailable)return bounds_source;
  GProp_GProps props;BRepGProp::VolumeProperties(shape,props);node.volume_m3=props.Mass()/1e9;
  GProp_GProps surface;BRepGProp::SurfaceProperties(shape,surface);node.surface_area_m2=surface.Mass()/1e6;
  TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);node.face_count=faces.Extent();node.edge_count=edges.Extent();
  return bounds_source;
  }catch(const Standard_Failure&){return BoundsSource::unavailable;}
}
AssemblyNode read_node(const TDF_Label& label,const Handle(XCAFDoc_ShapeTool)& tool,double deflection,std::vector<LeafShape>& leaves,int& skipped_geometry) {
  TDF_Label geometry_label=label; if(tool->IsReference(label)) tool->GetReferredShape(label,geometry_label);
  const auto shape=tool->GetShape(label); AssemblyNode node; node.persistent_id=label_id(label); const auto referenced_name=label_name(geometry_label,"Unnamed part");node.name=label_name(label,referenced_name);if(node.name.starts_with("=>"))node.name=referenced_name;node.transform=matrix(shape.Location());
  const auto node_bounds_source=populate_geometry(node,shape,deflection);const bool node_geometry_ok=node_bounds_source!=BoundsSource::unavailable;if(!node_geometry_ok&&!shape.IsNull())++skipped_geometry;
  TDF_LabelSequence children;tool->GetComponents(label,children,false);
  for(Standard_Integer i=1;i<=children.Length();++i)node.children.push_back(read_node(children.Value(i),tool,deflection,leaves,skipped_geometry));
  if(children.IsEmpty()&&!shape.IsNull()){
    std::vector<TopoDS_Shape> solids;for(TopExp_Explorer ex(shape,TopAbs_SOLID);ex.More();ex.Next())solids.push_back(ex.Current());
    if(solids.size()>1){
      node.mesh={};
      for(std::size_t i=0;i<solids.size();++i){AssemblyNode detail;detail.persistent_id=node.persistent_id+"/solid/"+std::to_string(i+1);detail.name="Solid "+std::to_string(i+1);detail.transform=matrix(solids[i].Location());const auto detail_bounds_source=populate_geometry(detail,solids[i],deflection);Bnd_Box bounds;if(detail_bounds_source!=BoundsSource::unavailable&&broad_phase_bounds(detail.bounds,deflection,bounds))leaves.push_back({detail.persistent_id,solids[i],bounds,detail_bounds_source});else ++skipped_geometry;node.children.push_back(std::move(detail));}
    }else if(node_geometry_ok){Bnd_Box bounds;if(broad_phase_bounds(node.bounds,deflection,bounds))leaves.push_back({node.persistent_id,shape,bounds,node_bounds_source});else ++skipped_geometry;}
  }
  return node;
}
}

StepImportResult StepImporter::import_file(const std::filesystem::path& path,double deflection,bool compute_interferences) const {
  if(path.extension() != ".step" && path.extension() != ".stp" && path.extension() != ".STEP" && path.extension() != ".STP") throw std::invalid_argument("only STEP files are supported");
  STEPCAFControl_Reader reader; reader.SetNameMode(true); reader.SetColorMode(true); reader.SetLayerMode(true);
  if(!read_file_without_automatic_healing(reader,path)) throw std::runtime_error("STEP parser rejected the file");
  Handle(TDocStd_Document) doc; XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc); if(!reader.Transfer(doc)) throw std::runtime_error("STEP transfer failed");
  const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main()); TDF_LabelSequence roots; tool->GetFreeShapes(roots); StepImportResult result; result.source_name=path.filename().string();
  std::vector<LeafShape> leaves;int skipped_geometry=0;for(Standard_Integer i=1;i<=roots.Length();++i) result.roots.push_back(read_node(roots.Value(i),tool,deflection,leaves,skipped_geometry));
  const auto fallback_leaf_count=std::ranges::count_if(leaves,[](const auto& leaf){return leaf.bounds_source==BoundsSource::tessellation;});
  const bool defer_for_fallback=fallback_leaf_count>0;
  if(compute_interferences&&!defer_for_fallback)for(std::size_t i=0;i<leaves.size();++i)for(std::size_t j=i+1;j<leaves.size();++j){if(leaves[i].bounds.IsOut(leaves[j].bounds))continue;BRepAlgoAPI_Common common(leaves[i].shape,leaves[j].shape);common.Build();if(!common.IsDone()||common.Shape().IsNull())continue;GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);const double volume=props.Mass()/1e9;if(volume>1e-12)result.interferences.push_back({leaves[i].id,leaves[j].id,volume});}
  else if(compute_interferences)result.warnings.push_back("Static interference was deferred because "+std::to_string(fallback_leaf_count)+" imported shapes required tessellation-derived bounds");
  else result.warnings.push_back("Static interference was deferred for this large assembly");
  result.warnings.push_back("Automatic STEP shape healing was disabled; imported topology is preserved without repair");
  if(skipped_geometry>0)result.warnings.push_back(std::to_string(skipped_geometry)+" topology nodes could not be bounded or tessellated and remain visible without geometry");
  if(result.roots.empty()) result.warnings.push_back("No free shapes were found in the STEP document"); return result;
}

std::vector<StaticInterference> StepImporter::static_interferences(const std::filesystem::path& path,const std::vector<PartPlacement>& placements)const{
  STEPCAFControl_Reader reader;reader.SetNameMode(true);if(!read_file_without_automatic_healing(reader,path))throw std::runtime_error("STEP parser rejected the file");Handle(TDocStd_Document) doc;XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc);if(!reader.Transfer(doc))throw std::runtime_error("STEP transfer failed");const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main());TDF_LabelSequence roots;tool->GetFreeShapes(roots);std::vector<LeafShape> leaves;int skipped_geometry=0;for(Standard_Integer i=1;i<=roots.Length();++i)(void)read_node(roots.Value(i),tool,0.0005,leaves,skipped_geometry);apply_placements(leaves,placements);std::vector<StaticInterference> result;for(std::size_t i=0;i<leaves.size();++i)for(std::size_t j=i+1;j<leaves.size();++j){if(leaves[i].bounds.IsOut(leaves[j].bounds))continue;BRepAlgoAPI_Common common(leaves[i].shape,leaves[j].shape);common.Build();if(!common.IsDone()||common.Shape().IsNull())continue;GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);const double volume=props.Mass()/1e9;if(volume>1e-12)result.push_back({leaves[i].id,leaves[j].id,volume});}return result;
}

std::vector<SweepInterference> StepImporter::sweep_revolute(const std::filesystem::path& path,const std::string& moving_id,const std::string& excluded_id,const std::array<double,3>& pivot_m,const std::array<double,3>& axis,double minimum_deg,double maximum_deg,int samples,const std::vector<PartPlacement>& placements)const{
  const std::array<double,8> sweep_values{pivot_m[0],pivot_m[1],pivot_m[2],axis[0],axis[1],axis[2],minimum_deg,maximum_deg};
  const double axis_magnitude_squared=axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2];
  if(samples<2||maximum_deg<minimum_deg||!std::ranges::all_of(sweep_values,[](const double value){return std::isfinite(value);})||!std::isfinite(axis_magnitude_squared)||axis_magnitude_squared<=0)throw std::invalid_argument("invalid joint sweep range");
  validate_placements(placements);
  STEPCAFControl_Reader reader;reader.SetNameMode(true);if(!read_file_without_automatic_healing(reader,path))throw std::runtime_error("STEP parser rejected the file");Handle(TDocStd_Document) doc;XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc);if(!reader.Transfer(doc))throw std::runtime_error("STEP transfer failed");const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main());TDF_LabelSequence roots;tool->GetFreeShapes(roots);std::vector<LeafShape> leaves;int skipped_geometry=0;for(Standard_Integer i=1;i<=roots.Length();++i)(void)read_node(roots.Value(i),tool,0.0005,leaves,skipped_geometry);apply_placements(leaves,placements);
  const auto moving=std::find_if(leaves.begin(),leaves.end(),[&](const auto& leaf){return leaf.id==moving_id;});if(moving==leaves.end())throw std::invalid_argument("moving joint entity not found");const gp_Dir direction(axis[0],axis[1],axis[2]);const gp_Ax1 rotation_axis(gp_Pnt(pivot_m[0]*1000,pivot_m[1]*1000,pivot_m[2]*1000),direction);std::map<std::string,SweepInterference> hits;constexpr double pi=3.14159265358979323846;
  for(int sample=0;sample<samples;++sample){const double angle=minimum_deg+(maximum_deg-minimum_deg)*sample/(samples-1.0);gp_Trsf rotation;rotation.SetRotation(rotation_axis,angle*pi/180.0);const auto moved=BRepBuilderAPI_Transform(moving->shape,rotation,true).Shape();Bnd_Box moved_bounds;if(!finite_shape_broad_phase(moved,0.0005,moved_bounds))throw std::runtime_error("swept STEP shape could not be bounded");for(const auto& other:leaves){if(other.id==moving_id||other.id==excluded_id||moved_bounds.IsOut(other.bounds))continue;BRepAlgoAPI_Common common(moved,other.shape);common.Build();if(!common.IsDone()||common.Shape().IsNull())continue;GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);const double volume=props.Mass()/1e9;if(volume<=1e-12)continue;auto [it,inserted]=hits.try_emplace(other.id,SweepInterference{moving_id,other.id,angle,volume,samples});if(!inserted)it->second.maximum_volume_m3=std::max(it->second.maximum_volume_m3,volume);}}
  std::vector<SweepInterference> result;for(auto& [id,hit]:hits)result.push_back(std::move(hit));return result;
}
}
