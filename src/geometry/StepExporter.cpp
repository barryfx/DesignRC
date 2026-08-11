#include "geometry/StepExporter.h"

#include <BRep_Builder.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <map>
#include <memory>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace designrc::geometry {
namespace {

TCollection_ExtendedString extendedName(const std::string& name) {
  return TCollection_ExtendedString{name.c_str(), true};
}

struct GroupedPart {
  PartMaterial material{PartMaterial::Wood};
  std::vector<TopoDS_Shape> shapes;
};

struct AssemblyNode {
  std::map<std::string, std::unique_ptr<AssemblyNode>> children;
  std::map<std::pair<std::string, int>, GroupedPart> parts;
};

AssemblyNode& childNode(AssemblyNode& parent, const std::string& name) {
  auto& child = parent.children[name];
  if (!child) child = std::make_unique<AssemblyNode>();
  return *child;
}

bool startsWithRibName(const std::string& name) {
  return name.size() >= 2 && name.front() == 'R' &&
      name[1] >= '0' && name[1] <= '9';
}

std::string lowercase(std::string value) {
  for (auto& character : value)
    character = static_cast<char>(std::tolower(
        static_cast<unsigned char>(character)));
  return value;
}

bool isJoinerName(const std::string& name) {
  const auto lower = lowercase(name);
  if (lower.find("joiner") != std::string::npos ||
      lower.starts_with("alignment pin"))
    return true;
  return name.size() >= 2 && name.front() == 'J' &&
      name[1] >= '0' && name[1] <= '9';
}

std::string panelGroup(const std::string& name) {
  const auto lower = lowercase(name);
  if (startsWithRibName(name)) return "Ribs";
  if (lower.find("spar") != std::string::npos ||
      lower.find("shear web") != std::string::npos ||
      lower.starts_with("sw"))
    return "Spars and Shear Webs";
  if (lower == "top te sheeting" || lower == "bottom te sheeting")
    return "Leading and Trailing Edges";
  if (lower.find("sheeting") != std::string::npos)
    return "Sheeting";
  if (lower.find("aileron") != std::string::npos ||
      lower.find("flap") != std::string::npos ||
      lower.find("hinge post") != std::string::npos)
    return "Controls";
  if (lower.starts_with("spoiler")) return "Spoiler Components";
  if (isJoinerName(name)) return "Joiners";
  if (lower.starts_with("le") || lower.starts_with("te") ||
      lower.find("leading edge") != std::string::npos ||
      lower.find("trailing edge") != std::string::npos)
    return "Leading and Trailing Edges";
  return "Other Components";
}

std::string centerGroup(const std::string& name) {
  const auto lower = lowercase(name);
  if (isJoinerName(name)) return "Joiners";
  if (lower.starts_with("spoiler frame rail") ||
      lower.starts_with("spoiler support rail"))
    return "Frame Rails";
  if (lower.starts_with("spoiler")) return "Spoiler";
  return "Other Components";
}

struct PartLocation {
  std::vector<std::string> assemblyPath;
  std::string leafName;
};

PartLocation partLocation(const std::string& exportedName) {
  constexpr std::string_view centerPrefix{"Center - "};
  if (exportedName.starts_with(centerPrefix)) {
    const std::string leaf = exportedName.substr(centerPrefix.size());
    return {{"Center-Spanning Components", centerGroup(leaf)}, leaf};
  }
  for (const std::string side : {"Left", "Right"}) {
    const std::string prefix = side + " Panel ";
    if (!exportedName.starts_with(prefix)) continue;
    const auto separator = exportedName.find(" - ", prefix.size());
    if (separator == std::string::npos) break;
    const std::string panelNumber = exportedName.substr(
        prefix.size(), separator - prefix.size());
    const std::string leaf = exportedName.substr(separator + 3);
    if (panelNumber.empty() || leaf.empty()) break;
    return {{side + " Wing", "Panel " + panelNumber, panelGroup(leaf)}, leaf};
  }
  return {{"Center-Spanning Components", "Other Components"}, exportedName};
}

TopoDS_Shape groupedShape(const GroupedPart& group, BRep_Builder& builder) {
  if (group.shapes.size() == 1) return group.shapes.front();
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  for (const auto& shape : group.shapes) builder.Add(compound, shape);
  return compound;
}

} // namespace

void exportStepAssembly(const std::vector<NamedPartShape>& parts,
                        const std::filesystem::path& path,
                        const std::string& assemblyName) {
  if (parts.empty())
    throw std::invalid_argument("STEP export requires generated 3D parts");
  if (path.empty()) throw std::invalid_argument("STEP export requires a file path");

  AssemblyNode hierarchy;
  for (const auto& part : parts) {
    if (part.shape.IsNull()) continue;
    const auto location = partLocation(part.name);
    AssemblyNode* node = &hierarchy;
    for (const auto& name : location.assemblyPath)
      node = &childNode(*node, name);
    auto& group = node->parts[
        {location.leafName, static_cast<int>(part.material)}];
    group.material = part.material;
    group.shapes.push_back(part.shape);
  }
  if (hierarchy.children.empty() && hierarchy.parts.empty())
    throw std::invalid_argument("STEP export contains no valid 3D parts");

  const Handle(TDocStd_Document) document{
      new TDocStd_Document{TCollection_ExtendedString{"BinXCAF"}}};
  const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
  const TDF_Label assembly = shapeTool->NewShape();
  TDataStd_Name::Set(assembly, extendedName(
      assemblyName.empty() ? std::string{"DesignRC Wing"} : assemblyName));

  BRep_Builder builder;
  const auto addNode = [&](const auto& self, const TDF_Label& parent,
                           const std::string& nodeName,
                           const AssemblyNode& node) -> void {
    const TDF_Label definition = shapeTool->NewShape();
    const auto extendedNodeName = extendedName(nodeName);
    TDataStd_Name::Set(definition, extendedNodeName);
    const TDF_Label nodeComponent = shapeTool->AddComponent(
        parent, definition, TopLoc_Location{});
    if (nodeComponent.IsNull())
      throw std::runtime_error(
          "Unable to add " + nodeName + " to the STEP assembly");
    TDataStd_Name::Set(nodeComponent, extendedNodeName);
    for (const auto& [childName, child] : node.children)
      self(self, definition, childName, *child);
    for (const auto& [key, group] : node.parts) {
      const TopoDS_Shape partShape = groupedShape(group, builder);
      const TDF_Label component =
          shapeTool->AddComponent(definition, partShape, false);
      if (component.IsNull())
        throw std::runtime_error(
            "Unable to add " + key.first + " to the STEP assembly");
      const auto name = extendedName(key.first);
      TDataStd_Name::Set(component, name);
      TDF_Label partDefinition;
      if (shapeTool->GetReferredShape(component, partDefinition)) {
        TDataStd_Name::Set(partDefinition, name);
      }
    }
  };
  const TDF_Label wingDefinition = shapeTool->NewShape();
  const auto wingName = extendedName("Wing");
  TDataStd_Name::Set(wingDefinition, wingName);
  const TDF_Label wingComponent = shapeTool->AddComponent(
      assembly, wingDefinition, TopLoc_Location{});
  if (wingComponent.IsNull())
    throw std::runtime_error("Unable to add Wing to the STEP assembly");
  TDataStd_Name::Set(wingComponent, wingName);
  for (const auto& [nodeName, node] : hierarchy.children)
    addNode(addNode, wingDefinition, nodeName, *node);
  for (const auto& [key, group] : hierarchy.parts) {
    const TopoDS_Shape partShape = groupedShape(group, builder);
    const TDF_Label component =
        shapeTool->AddComponent(wingDefinition, partShape, false);
    const auto name = extendedName(key.first);
    TDataStd_Name::Set(component, name);
  }
  shapeTool->UpdateAssemblies();

  Interface_Static::SetCVal("write.step.schema", "AP242DIS");
  STEPCAFControl_Writer writer;
  writer.SetColorMode(false);
  writer.SetNameMode(true);
  writer.SetPropsMode(true);
  const auto fileName = path.string();
  if (!writer.Perform(document, fileName.c_str()))
    throw std::runtime_error("OpenCascade could not write the STEP assembly");
}

} // namespace designrc::geometry
