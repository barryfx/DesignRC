#include "geometry/StepExporter.h"
#include "geometry/OcctRibBuilder.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <cstdio>
#include <Standard_Failure.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <numbers>
#include <limits>
#include <cmath>

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
#include <cstdlib>
#include <atomic>
#include <algorithm>
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
#ifdef _WIN32
  _set_error_mode(_OUT_TO_STDERR);
#endif
  try {
    // Adjacent panels meet at a shared angled rib plane. Even wide cap stock
    // must terminate at each panel's outside root/tip face.
    double originY = 0.0, originZ = 0.0;
    for (int panel = 0; panel < 2; ++panel) {
      using namespace designrc::domain;
      WingParameters p;
      p.ribCount = 3; p.halfSpan = 150.0; p.ribThickness = 2.0;
      p.rootChord = panel == 0 ? 160.0 : 140.0;
      p.tipChord = panel == 0 ? 140.0 : 120.0; p.dihedralDegrees = 0.0;
      p.rootTwistDegrees = panel == 0 ? 0.0 : -4.0;
      p.tipTwistDegrees = panel == 0 ? -4.0 : -8.0;
      const auto foil = AirfoilProfile::nacaSymmetric(0.12);
      auto ribs = generateRibs(p, foil, foil);
      const double inclination = panel == 0 ? 10.0 : 25.0;
      const double radians = inclination * std::numbers::pi / 180.0;
      for (std::size_t i = 0; i < ribs.size(); ++i) {
        const double span = ribs[i].spanPosition;
        ribs[i].spanPosition = originY + std::cos(radians) * span;
        ribs[i].dihedralHeight = originZ + std::sin(radians) * span;
        ribs[i].ribPlaneAngleDegrees = i == 0 ? (panel == 0 ? 0.0 : 17.5) :
            i + 1 == ribs.size() ? (panel == 0 ? 17.5 : 25.0) : inclination;
        ribs[i].ribThicknessStartFactor = i == 0 ? 0.0 : i + 1 == ribs.size() ? -1.0 : -0.5;
      }
      StructureParameters s;
      s.ribThickness = p.ribThickness;
      s.topRibCaps = s.bottomRibCaps = true;
      s.topRibCapWidth = s.bottomRibCapWidth = 25.4;
      const auto wing = applyWingStructure(ribs, s);
      designrc::geometry::MaterialShapeSet materials;
      const auto shape = designrc::geometry::buildStructuredWingPreview(
          wing, p.ribThickness, nullptr, &materials, {}, 3);
      assert(BRepCheck_Analyzer{shape}.IsValid());
      for (const auto& cap : wing.ribCaps) {
        if (cap.ribIndex != 0 && cap.ribIndex + 1 != ribs.size()) continue;
        const auto part = std::find_if(materials.parts.begin(), materials.parts.end(),
            [&](const auto& candidate) { return candidate.name == cap.name; });
        assert(part != materials.parts.end());
        const auto& rib = ribs[cap.ribIndex];
        const double angle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
        for (TopExp_Explorer vertices{part->shape, TopAbs_VERTEX}; vertices.More(); vertices.Next()) {
          const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
          const double offset = (point.Y() - rib.spanPosition) * std::cos(angle) +
              (point.Z() - rib.dihedralHeight) * std::sin(angle);
          if (cap.ribIndex == 0) assert(offset >= -1.0e-6);
          else assert(offset <= 1.0e-6);
        }
      }
      originY = ribs.back().spanPosition;
      originZ = ribs.back().dihedralHeight;
    }
    for (const bool obstructed : {false, true}) {
    using namespace designrc::domain;
    WingParameters p;
    p.ribCount = obstructed ? 4 : 3; p.halfSpan = 250.0;
    p.rootChord = 300.0; p.tipChord = 240.0;
    p.dihedralDegrees = obstructed ? 0.0 : 12.0; p.tipTwistDegrees = -3.0;
    const auto foil = AirfoilProfile::nacaSymmetric(0.12);
    auto ribs = generateRibs(p, foil, foil);
    ribs.front().ribThicknessStartFactor = 0.0;
    ribs.back().ribThicknessStartFactor = -1.0;
    StructureParameters s;
    s.ribThickness = p.ribThickness;
    s.topRibCaps = s.bottomRibCaps = true;
    if (obstructed) {
      s.topSpar = s.bottomSpar = true;
      s.leadingEdgeType = 2; s.leadingEdgeWidth = 6.0; s.leadingEdgeHeight = 30.0;
      s.leTopSheet = true; s.leTopSheetStopRib = 4;
      s.spoilers = true; s.spoilerStartRib = 1; s.spoilerEndRib = 4;
      s.spoilerChordLocationPercent = 40.0;
    }
    const auto wing = applyWingStructure(ribs, s);
    designrc::geometry::MaterialShapeSet serialMaterials;
    designrc::geometry::PanelBuildTimings serialTimings, parallelTimings;
    const auto serial = designrc::geometry::buildStructuredWingPreview(
        wing, p.ribThickness, &serialTimings, &serialMaterials, {}, 1);
    assert(BRepCheck_Analyzer{serial}.IsValid());
    std::atomic_int capProgressReports{0};
    const auto report = [&](int, const std::string& message) {
      if (message.starts_with("Checking rib cap collisions for Rib ")) {
        assert(message.find("Top") != std::string::npos || message.find("Bottom") != std::string::npos);
        ++capProgressReports;
      }
    };
    designrc::geometry::MaterialShapeSet materials;
    const auto shape = designrc::geometry::buildStructuredWingPreview(
        wing, p.ribThickness, &parallelTimings, &materials, report, 4);
    assert(capProgressReports == static_cast<int>(wing.ribCaps.size()));
    assert(serialMaterials.parts.size() == materials.parts.size());
    for (std::size_t i = 0; i < materials.parts.size(); ++i) {
      assert(serialMaterials.parts[i].name == materials.parts[i].name);
      if (materials.parts[i].name.find("rib cap") == std::string::npos) continue;
      GProp_GProps before, after;
      BRepGProp::VolumeProperties(serialMaterials.parts[i].shape, before);
      BRepGProp::VolumeProperties(materials.parts[i].shape, after);
      assert(std::abs(before.Mass() - after.Mass()) <= 1.0e-6 * std::max(1.0, before.Mass()));
    }
    std::fprintf(stderr, "Rib cap collision stage (%s): serial %.1f ms, parallel %.1f ms\n",
        obstructed ? "with structure" : "bare ribs", serialTimings.ribCapsMs, parallelTimings.ribCapsMs);
    for (const auto& part : materials.parts)
      if (!BRepCheck_Analyzer{part.shape}.IsValid())
        std::fprintf(stderr, "Invalid rib cap test part: %s\n", part.name.c_str());
    assert(BRepCheck_Analyzer{shape}.IsValid());
    int caps = 0;
    for (const auto& cap : materials.parts) {
      if (cap.name.find("rib cap") == std::string::npos) continue;
      ++caps;
      GProp_GProps volume;
      BRepGProp::VolumeProperties(cap.shape, volume);
      assert(volume.Mass() > 0.0 && BRepCheck_Analyzer{cap.shape}.IsValid());
      Bnd_Box capBounds;
      BRepBndLib::Add(cap.shape, capBounds);
      for (const auto& other : materials.parts) {
        if (&cap == &other) continue;
        Bnd_Box otherBounds;
        BRepBndLib::Add(other.shape, otherBounds);
        if (capBounds.IsOut(otherBounds)) continue;
        BRepAlgoAPI_Common common{cap.shape, other.shape};
        assert(common.IsDone());
        GProp_GProps overlap;
        BRepGProp::VolumeProperties(common.Shape(), overlap);
        if (overlap.Mass() > 1.0e-3)
          std::fprintf(stderr, "%s overlaps %s by %.9f mm3\n", cap.name.c_str(), other.name.c_str(), overlap.Mass());
        assert(overlap.Mass() <= 1.0e-3);
      }
    }
    assert(caps > 0);
    const auto path = std::filesystem::temp_directory_path() / "designrc_rib_caps.step";
    designrc::geometry::exportStepAssembly(materials.parts, path, "Rib caps");
    STEPControl_Reader reader;
    assert(reader.ReadFile(path.string().c_str()) == IFSelect_RetDone);
    assert(reader.TransferRoots() > 0);
    assert(BRepCheck_Analyzer{reader.OneShape()}.IsValid());
    std::filesystem::remove(path);
    }
  } catch (const Standard_Failure& error) {
    std::fprintf(stderr, "Rib cap geometry: %s\n", error.GetMessageString());
    return 1;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Rib cap geometry: %s\n", error.what());
    return 1;
  }
  {
    // The saved glider's AG37-to-AG38 outer panel previously missed the lower
    // surface at rib 4 with its 2 mm LE. Validate the resulting solids as well.
    const auto fixtures = std::filesystem::path{__FILE__}.parent_path() / "fixtures";
    designrc::domain::WingParameters p;
    p.rootChord = 152.4; p.tipChord = 101.6; p.halfSpan = 200.0;
    p.sweep = 20.6375; p.ribCount = 8; p.dihedralDegrees = 0.0;
    p.rootTwistDegrees = 5.0; p.tipTwistDegrees = 10.0;
    const auto ribs = designrc::domain::generateRibs(p,
        designrc::domain::AirfoilProfile::fromDatFile(fixtures / "ag37.dat"),
        designrc::domain::AirfoilProfile::fromDatFile(fixtures / "ag38.dat"));
    designrc::domain::StructureParameters structure;
    structure.addBuildTabs = true;
    structure.leadingEdgeType = 3;
    structure.leadingEdgeTubeOd = 2.0;
    structure.leadingEdgeTubeId = 1.0;
    structure.trailingEdgeType = 2;
    structure.trailingEdgeWidth = 25.4;
    structure.trailingEdgeHeight = 9.525;
    designrc::geometry::MaterialShapeSet materials;
    const auto shape = designrc::geometry::buildStructuredWingPreview(
        designrc::domain::applyWingStructure(ribs, structure), 2.38125, nullptr, &materials);
    assert(BRepCheck_Analyzer{shape}.IsValid());
    const auto path = std::filesystem::temp_directory_path() / "designrc_ag_le_regression.step";
    designrc::geometry::exportStepAssembly(materials.parts, path, "AG glider outer panel");
    STEPControl_Reader reader;
    assert(reader.ReadFile(path.string().c_str()) == IFSelect_RetDone);
    assert(reader.TransferRoots() > 0);
    assert(BRepCheck_Analyzer{reader.OneShape()}.IsValid());
    std::filesystem::remove(path);
  }
  for (const double twist : {-5.0, 0.0, 5.0}) {
    designrc::domain::WingParameters parameters;
    parameters.ribCount = 3;
    parameters.dihedralDegrees = 0.0;
    parameters.tipTwistDegrees = twist;
    const auto foil = designrc::domain::AirfoilProfile::nacaSymmetric(0.12);
    const auto ribs = designrc::domain::generateRibs(parameters, foil, foil);
    designrc::domain::StructureParameters structure;
    // Exercise relocation around a bottom spar as well as the free rear tab.
    structure.bottomSpar = true;
    const auto plain = designrc::geometry::buildStructuredWingPreview(
        designrc::domain::applyWingStructure(ribs, structure), parameters.ribThickness);
    structure.addBuildTabs = true;
    designrc::geometry::MaterialShapeSet materials;
    const auto tabWing = designrc::domain::applyWingStructure(ribs, structure);
    const auto tabbed = designrc::geometry::buildStructuredWingPreview(
        tabWing, parameters.ribThickness,
        nullptr, &materials);
    assert(BRepCheck_Analyzer{tabbed}.IsValid());
    GProp_GProps plainProperties, tabProperties;
    BRepGProp::VolumeProperties(plain, plainProperties);
    BRepGProp::VolumeProperties(tabbed, tabProperties);
    assert(tabProperties.Mass() > plainProperties.Mass());
    const auto tabPath = std::filesystem::temp_directory_path() / "designrc_build_tabs.step";
    double expectedVolume = 0.0;
    for (auto& part : materials.parts) {
      part.name = "Right Panel 1 - " + part.name;
      part.mirrorInAssembly = false;
      GProp_GProps properties;
      BRepGProp::VolumeProperties(part.shape, properties, 1.0e-9);
      expectedVolume += properties.Mass();
    }
    designrc::geometry::exportStepAssembly(materials.parts, tabPath, "Build tabs");
    STEPControl_Reader tabReader;
    assert(tabReader.ReadFile(tabPath.string().c_str()) == IFSelect_RetDone);
    assert(tabReader.TransferRoots() > 0);
    const auto imported = tabReader.OneShape();
    assert(BRepCheck_Analyzer{imported}.IsValid());
    GProp_GProps importedProperties;
    BRepGProp::VolumeProperties(imported, importedProperties, 1.0e-9);
    // STEP healing can change the integral over spline-trimmed faces slightly.
    // Check overall volume, then verify every tab-foot endpoint independently.
    assert(std::abs(importedProperties.Mass() - expectedVolume) < expectedVolume * 5.0e-4);
    const double rootBottom = designrc::domain::untwistedRibBottom(ribs.front());
    const double tipBottom = designrc::domain::untwistedRibBottom(ribs.back());
    int footEndpoints = 0;
    for (const auto& rib : tabWing.ribs) {
      const double plane = rootBottom + (tipBottom - rootBottom) * rib.rib.spanPosition / parameters.halfSpan;
      const auto translation = designrc::domain::ribTwistTranslation(rib.rib);
      const double angle = rib.rib.twistDegrees * std::numbers::pi / 180.0;
      for (const auto& segment : rib.outlineSegments) {
        if (segment.spline || segment.points.size() != 2 ||
            std::abs(std::abs(segment.points[1].x - segment.points[0].x) - 25.4 * 3.0 / 16.0) > 1.0e-7)
          continue;
        for (const auto point : segment.points) {
        const double z = std::sin(angle) * point.x + std::cos(angle) * point.y + translation.y;
        if (std::abs(z - plane) > 1.0e-7) continue;
        const gp_Pnt expected{
            rib.rib.leadingEdgeOffset + std::cos(angle) * point.x - std::sin(angle) * point.y + translation.x,
            rib.rib.spanPosition + rib.rib.ribThicknessStartFactor * parameters.ribThickness, z};
        bool found = false;
        for (TopExp_Explorer vertices{imported, TopAbs_VERTEX}; vertices.More(); vertices.Next())
          found = found || BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current())).Distance(expected) < 1.0e-5;
        assert(found);
        ++footEndpoints;
        }
      }
    }
    assert(footEndpoints == 4 * parameters.ribCount);
    std::filesystem::remove(tabPath);
  }
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
