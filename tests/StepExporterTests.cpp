#include "geometry/StepExporter.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <NCollection_Sequence.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string labelName(const TDF_Label& label) {
  Handle(TDataStd_Name) name;
  assert(label.FindAttribute(TDataStd_Name::GetID(), name));
  return TCollection_AsciiString{name->Get()}.ToCString();
}

TDF_Label componentDefinition(const TDF_Label& component) {
  TDF_Label definition;
  assert(XCAFDoc_ShapeTool::GetReferredShape(component, definition));
  return definition;
}

TDF_Label findChild(const TDF_Label& assembly, const std::string& name) {
  NCollection_Sequence<TDF_Label> components;
  assert(XCAFDoc_ShapeTool::GetComponents(assembly, components, false));
  for (int index = 1; index <= components.Length(); ++index) {
    const auto definition = componentDefinition(components.Value(index));
    if (labelName(definition) == name) return definition;
  }
  assert(false);
  return {};
}

} // namespace

int main() {
  using designrc::geometry::NamedPartShape;
  using designrc::geometry::PartMaterial;
  const std::vector<NamedPartShape> parts{
      {"Right Panel 1 - R1", BRepPrimAPI_MakeBox{10.0, 2.0, 3.0}.Shape(),
       PartMaterial::Wood, false},
      {"Right Panel 1 - Spar 1", BRepPrimAPI_MakeBox{
           gp_Pnt{0.0, 5.0, 0.0}, 20.0, 2.0, 2.0}.Shape(),
       PartMaterial::CarbonFiber, false},
      {"Right Panel 1 - Spar 1", BRepPrimAPI_MakeBox{
           gp_Pnt{20.0, 5.0, 0.0}, 5.0, 2.0, 2.0}.Shape(),
       PartMaterial::CarbonFiber, false},
      {"Right Panel 1 - Top TE sheeting", BRepPrimAPI_MakeBox{
           gp_Pnt{25.0, 5.0, 0.0}, 8.0, 2.0, 1.0}.Shape(),
       PartMaterial::Wood, false},
      {"Left Panel 1 - R1", BRepPrimAPI_MakeBox{
           gp_Pnt{0.0, -2.0, 0.0}, 10.0, 2.0, 3.0}.Shape(),
       PartMaterial::Wood, false},
      {"Center - Fixed Joiner 1 CF Tube", BRepPrimAPI_MakeBox{
           gp_Pnt{3.0, -5.0, 1.0}, 2.0, 10.0, 2.0}.Shape(),
       PartMaterial::CarbonFiber, false},
      {"Center - Spoiler", BRepPrimAPI_MakeBox{
           gp_Pnt{8.0, -8.0, 5.0}, 12.0, 16.0, 2.0}.Shape(),
       PartMaterial::Wood, false},
      {"Center - Spoiler Frame Rail 1", BRepPrimAPI_MakeBox{
           gp_Pnt{6.0, -8.0, 4.0}, 2.0, 16.0, 3.0}.Shape(),
       PartMaterial::Wood, false},
      {"Right Panel 2 - Fixed Joiner 2 Steel Rod", BRepPrimAPI_MakeBox{
           gp_Pnt{4.0, 8.0, 1.0}, 2.0, 14.0, 2.0}.Shape(),
       PartMaterial::Steel, false}};
  const auto path = std::filesystem::temp_directory_path() /
      "designrc_step_export_regression.step";
  designrc::geometry::exportStepAssembly(parts, path, "DesignRC Test Wing");
  assert(std::filesystem::exists(path));
  assert(std::filesystem::file_size(path) > 1000);
  std::ifstream input{path};
  const std::string contents{std::istreambuf_iterator<char>{input}, {}};
  assert(contents.find("DesignRC Test Wing") != std::string::npos);
  assert(contents.find("Right Panel 1 -") == std::string::npos);
  assert(contents.find("Left Panel 1 -") == std::string::npos);
  assert(contents.find("Right Wing") != std::string::npos);
  assert(contents.find("Center-Spanning Components") != std::string::npos);
  assert(contents.find("COLOUR_RGB") == std::string::npos);
  input.close();

  Handle(TDocStd_Document) document = new TDocStd_Document("BinXCAF");
  STEPCAFControl_Reader reader;
  assert(reader.ReadFile(path.string().c_str()) == IFSelect_RetDone);
  assert(reader.Transfer(document));
  const Handle(XCAFDoc_ShapeTool) shapeTool =
      XCAFDoc_DocumentTool::ShapeTool(document->Main());
  NCollection_Sequence<TDF_Label> roots;
  shapeTool->GetFreeShapes(roots);
  assert(roots.Length() == 1);
  NCollection_Sequence<TDF_Label> components;
  assert(XCAFDoc_ShapeTool::GetComponents(roots.Value(1), components, false));
  assert(components.Length() == 1);
  const auto wing = findChild(roots.Value(1), "Wing");
  NCollection_Sequence<TDF_Label> wingComponents;
  assert(XCAFDoc_ShapeTool::GetComponents(wing, wingComponents, false));
  assert(wingComponents.Length() == 3);

  const auto rightWing = findChild(wing, "Right Wing");
  const auto rightPanel = findChild(rightWing, "Panel 1");
  const auto rightRibs = findChild(rightPanel, "Ribs");
  const auto rightSpars = findChild(rightPanel, "Spars and Shear Webs");
  const auto rightEdges = findChild(
      rightPanel, "Leading and Trailing Edges");
  assert(labelName(findChild(rightRibs, "R1")) == "R1");
  assert(labelName(findChild(rightSpars, "Spar 1")) == "Spar 1");
  assert(labelName(findChild(rightEdges, "Top TE sheeting")) ==
      "Top TE sheeting");
  NCollection_Sequence<TDF_Label> sparComponents;
  assert(XCAFDoc_ShapeTool::GetComponents(
      rightSpars, sparComponents, false));
  assert(sparComponents.Length() == 1);
  const auto rightPanel2 = findChild(rightWing, "Panel 2");
  assert(labelName(findChild(findChild(rightPanel2, "Joiners"),
      "Fixed Joiner 2 Steel Rod")) == "Fixed Joiner 2 Steel Rod");

  const auto leftWing = findChild(wing, "Left Wing");
  const auto leftPanel = findChild(leftWing, "Panel 1");
  assert(labelName(findChild(findChild(leftPanel, "Ribs"), "R1")) == "R1");

  const auto center = findChild(
      wing, "Center-Spanning Components");
  assert(labelName(findChild(
      findChild(center, "Joiners"), "Fixed Joiner 1 CF Tube")) ==
      "Fixed Joiner 1 CF Tube");
  assert(labelName(findChild(findChild(center, "Spoiler"), "Spoiler")) ==
      "Spoiler");
  assert(labelName(findChild(
      findChild(center, "Frame Rails"), "Spoiler Frame Rail 1")) ==
      "Spoiler Frame Rail 1");

  std::filesystem::remove(path);
  return 0;
}
