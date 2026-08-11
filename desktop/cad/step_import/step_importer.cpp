#include "prometheus/cad/step_importer.hpp"
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPCAFControl_Reader.hxx>
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
#include <cmath>
#include <map>
#include <stdexcept>

namespace prometheus::cad { namespace {
struct LeafShape { std::string id;TopoDS_Shape shape;Bnd_Box bounds; };
void apply_placements(std::vector<LeafShape>& leaves,const std::vector<PartPlacement>& placements){constexpr double pi=3.14159265358979323846;for(auto& leaf:leaves){const auto placement=std::find_if(placements.begin(),placements.end(),[&](const auto& value){return value.persistent_id==leaf.id;});if(placement==placements.end())continue;Standard_Real x1,y1,z1,x2,y2,z2;leaf.bounds.Get(x1,y1,z1,x2,y2,z2);const gp_Pnt center((x1+x2)/2,(y1+y2)/2,(z1+z2)/2);for(const auto [angle,axis]:{std::pair{placement->rotation_x_deg,gp_Dir(1,0,0)},std::pair{placement->rotation_y_deg,gp_Dir(0,1,0)},std::pair{placement->rotation_z_deg,gp_Dir(0,0,1)}}){if(angle==0)continue;gp_Trsf rotation;rotation.SetRotation(gp_Ax1(center,axis),angle*pi/180.0);leaf.shape=BRepBuilderAPI_Transform(leaf.shape,rotation,true).Shape();}gp_Trsf translation;translation.SetTranslation(gp_Vec(placement->translation_x_m*1000,placement->translation_y_m*1000,placement->translation_z_m*1000));leaf.shape=BRepBuilderAPI_Transform(leaf.shape,translation,true).Shape();leaf.bounds.SetVoid();BRepBndLib::Add(leaf.shape,leaf.bounds);}}
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
AssemblyNode read_node(const TDF_Label& label,const Handle(XCAFDoc_ShapeTool)& tool,double deflection,std::vector<LeafShape>& leaves) {
  TDF_Label geometry_label=label; if(tool->IsReference(label)) tool->GetReferredShape(label,geometry_label);
  const auto shape=tool->GetShape(label); AssemblyNode node; node.persistent_id=label_id(label); const auto referenced_name=label_name(geometry_label,"Unnamed part");node.name=label_name(label,referenced_name);if(node.name.starts_with("=>"))node.name=referenced_name;node.transform=matrix(shape.Location());
  if(!shape.IsNull()) { Bnd_Box box; BRepBndLib::Add(shape,box); if(!box.IsVoid()){box.Get(node.bounds.min_x,node.bounds.min_y,node.bounds.min_z,node.bounds.max_x,node.bounds.max_y,node.bounds.max_z);node.bounds={node.bounds.min_x/1000,node.bounds.min_y/1000,node.bounds.min_z/1000,node.bounds.max_x/1000,node.bounds.max_y/1000,node.bounds.max_z/1000};} GProp_GProps props; BRepGProp::VolumeProperties(shape,props);node.volume_m3=props.Mass()/1e9;GProp_GProps surface;BRepGProp::SurfaceProperties(shape,surface);node.surface_area_m2=surface.Mass()/1e6;TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);node.face_count=faces.Extent();node.edge_count=edges.Extent();node.mesh=tessellate(shape,deflection*1000.0); }
  TDF_LabelSequence children; tool->GetComponents(label,children,false); for(Standard_Integer i=1;i<=children.Length();++i) node.children.push_back(read_node(children.Value(i),tool,deflection,leaves));if(children.IsEmpty()&&!shape.IsNull()){Bnd_Box bounds;BRepBndLib::Add(shape,bounds);leaves.push_back({node.persistent_id,shape,bounds});} return node;
}
}

StepImportResult StepImporter::import_file(const std::filesystem::path& path,double deflection) const {
  if(path.extension() != ".step" && path.extension() != ".stp" && path.extension() != ".STEP" && path.extension() != ".STP") throw std::invalid_argument("only STEP files are supported");
  STEPCAFControl_Reader reader; reader.SetNameMode(true); reader.SetColorMode(true); reader.SetLayerMode(true);
  if(reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) throw std::runtime_error("STEP parser rejected the file");
  Handle(TDocStd_Document) doc; XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc); if(!reader.Transfer(doc)) throw std::runtime_error("STEP transfer failed");
  const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main()); TDF_LabelSequence roots; tool->GetFreeShapes(roots); StepImportResult result; result.source_name=path.filename().string();
  std::vector<LeafShape> leaves;for(Standard_Integer i=1;i<=roots.Length();++i) result.roots.push_back(read_node(roots.Value(i),tool,deflection,leaves));
  for(std::size_t i=0;i<leaves.size();++i)for(std::size_t j=i+1;j<leaves.size();++j){if(leaves[i].bounds.IsOut(leaves[j].bounds))continue;BRepAlgoAPI_Common common(leaves[i].shape,leaves[j].shape);common.Build();if(!common.IsDone()||common.Shape().IsNull())continue;GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);const double volume=props.Mass()/1e9;if(volume>1e-12)result.interferences.push_back({leaves[i].id,leaves[j].id,volume});}
  if(result.roots.empty()) result.warnings.push_back("No free shapes were found in the STEP document"); return result;
}

std::vector<StaticInterference> StepImporter::static_interferences(const std::filesystem::path& path,const std::vector<PartPlacement>& placements)const{
  STEPCAFControl_Reader reader;reader.SetNameMode(true);if(reader.ReadFile(path.string().c_str())!=IFSelect_RetDone)throw std::runtime_error("STEP parser rejected the file");Handle(TDocStd_Document) doc;XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc);if(!reader.Transfer(doc))throw std::runtime_error("STEP transfer failed");const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main());TDF_LabelSequence roots;tool->GetFreeShapes(roots);std::vector<LeafShape> leaves;for(Standard_Integer i=1;i<=roots.Length();++i)(void)read_node(roots.Value(i),tool,0.0005,leaves);apply_placements(leaves,placements);std::vector<StaticInterference> result;for(std::size_t i=0;i<leaves.size();++i)for(std::size_t j=i+1;j<leaves.size();++j){if(leaves[i].bounds.IsOut(leaves[j].bounds))continue;BRepAlgoAPI_Common common(leaves[i].shape,leaves[j].shape);common.Build();if(!common.IsDone()||common.Shape().IsNull())continue;GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);const double volume=props.Mass()/1e9;if(volume>1e-12)result.push_back({leaves[i].id,leaves[j].id,volume});}return result;
}

std::vector<SweepInterference> StepImporter::sweep_revolute(const std::filesystem::path& path,const std::string& moving_id,const std::string& excluded_id,const std::array<double,3>& pivot_m,const std::array<double,3>& axis,double minimum_deg,double maximum_deg,int samples,const std::vector<PartPlacement>& placements)const{
  if(samples<2||maximum_deg<minimum_deg)throw std::invalid_argument("invalid joint sweep range");
  STEPCAFControl_Reader reader;reader.SetNameMode(true);if(reader.ReadFile(path.string().c_str())!=IFSelect_RetDone)throw std::runtime_error("STEP parser rejected the file");Handle(TDocStd_Document) doc;XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",doc);if(!reader.Transfer(doc))throw std::runtime_error("STEP transfer failed");const auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main());TDF_LabelSequence roots;tool->GetFreeShapes(roots);std::vector<LeafShape> leaves;for(Standard_Integer i=1;i<=roots.Length();++i)(void)read_node(roots.Value(i),tool,0.0005,leaves);apply_placements(leaves,placements);
  const auto moving=std::find_if(leaves.begin(),leaves.end(),[&](const auto& leaf){return leaf.id==moving_id;});if(moving==leaves.end())throw std::invalid_argument("moving joint entity not found");const gp_Dir direction(axis[0],axis[1],axis[2]);const gp_Ax1 rotation_axis(gp_Pnt(pivot_m[0]*1000,pivot_m[1]*1000,pivot_m[2]*1000),direction);std::map<std::string,SweepInterference> hits;constexpr double pi=3.14159265358979323846;
  for(int sample=0;sample<samples;++sample){const double angle=minimum_deg+(maximum_deg-minimum_deg)*sample/(samples-1.0);gp_Trsf rotation;rotation.SetRotation(rotation_axis,angle*pi/180.0);const auto moved=BRepBuilderAPI_Transform(moving->shape,rotation,true).Shape();Bnd_Box moved_bounds;BRepBndLib::Add(moved,moved_bounds);for(const auto& other:leaves){if(other.id==moving_id||other.id==excluded_id||moved_bounds.IsOut(other.bounds))continue;BRepAlgoAPI_Common common(moved,other.shape);common.Build();if(!common.IsDone()||common.Shape().IsNull())continue;GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);const double volume=props.Mass()/1e9;if(volume<=1e-12)continue;auto [it,inserted]=hits.try_emplace(other.id,SweepInterference{moving_id,other.id,angle,volume,samples});if(!inserted)it->second.maximum_volume_m3=std::max(it->second.maximum_volume_m3,volume);}}
  std::vector<SweepInterference> result;for(auto& [id,hit]:hits)result.push_back(std::move(hit));return result;
}
}
