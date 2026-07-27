#include "domain/AirfoilProfile.h"
#include "domain/WingDesign.h"
#include "domain/WingStructure.h"
#include "geometry/OcctRibBuilder.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>

int runTest(int argc, char* argv[]) {
  using designrc::domain::AirfoilProfile;
  const auto cappedGeometryWorkers =
      designrc::geometry::ribGeometryWorkerCount(100, 3);
  if (cappedGeometryWorkers < 1 || cappedGeometryWorkers > 3) return 42;
  const auto root = argc > 1
      ? AirfoilProfile::fromDatFile(std::filesystem::path{argv[1]})
      : AirfoilProfile::nacaSymmetric(0.15);
  const auto tip = argc > 2
      ? AirfoilProfile::fromDatFile(std::filesystem::path{argv[2]})
      : AirfoilProfile::nacaSymmetric(0.10);

  designrc::domain::WingParameters parameters;
  const auto ribs = designrc::domain::generateRibs(parameters, root, tip);
  designrc::domain::StructureParameters earlyCfLeadingSheeting;
  earlyCfLeadingSheeting.leadingEdgeType = 3;
  earlyCfLeadingSheeting.leTopSheet = true;
  earlyCfLeadingSheeting.leBottomSheet = true;
  earlyCfLeadingSheeting.leTopSheetStopRib = 3;
  earlyCfLeadingSheeting.leBottomSheetStopRib = 3;
  const auto earlyCfSheeted = designrc::domain::applyWingStructure(ribs, earlyCfLeadingSheeting);
  const auto carbonLeadingEdge = std::find_if(
      earlyCfSheeted.members.begin(), earlyCfSheeted.members.end(),
      [](const auto& member) {
        return member.name == "CF tube leading edge";
      });
  if (carbonLeadingEdge == earlyCfSheeted.members.end()) return 28;
  const auto sheetingOverlapsLeadingEdgeNotch =
      [&](const std::string& name) {
        const auto sheet = std::find_if(
            earlyCfSheeted.sheeting.begin(), earlyCfSheeted.sheeting.end(),
            [&](const auto& part) { return part.name == name; });
        if (sheet == earlyCfSheeted.sheeting.end() ||
            sheet->profiles.empty() ||
            sheet->profiles.size() > carbonLeadingEdge->centers.size())
          return false;
        const double radius = carbonLeadingEdge->width * 0.5;
        for (std::size_t ribIndex = 0;
             ribIndex < sheet->profiles.size(); ++ribIndex) {
          if (sheet->profiles[ribIndex].empty()) return false;
          const auto& start = sheet->profiles[ribIndex].front();
          const auto& center = carbonLeadingEdge->centers[ribIndex];
          if (std::hypot(start.x - center.x, start.y - center.y) >=
              radius - 1.0e-5)
            return false;
        }
        return true;
      };
  if (!sheetingOverlapsLeadingEdgeNotch("LE top sheeting") ||
      !sheetingOverlapsLeadingEdgeNotch("LE bottom sheeting"))
    return 29;
  const auto earlyCfSheetedShape = designrc::geometry::buildStructuredWingPreview(
      earlyCfSheeted, parameters.ribThickness);
  if (earlyCfSheetedShape.IsNull()) return 8;
  designrc::domain::StructureParameters controlSheeting;
  controlSheeting.teTopSheet = true;
  controlSheeting.teTopSheetStopRib = static_cast<int>(ribs.size());
  const auto controlSheetedWing = designrc::domain::applyWingStructure(ribs, controlSheeting);
  designrc::geometry::MaterialShapeSet controlSheetingMaterials;
  const auto controlSheetedShape = designrc::geometry::buildStructuredWingPreview(
      controlSheetedWing, parameters.ribThickness, nullptr, &controlSheetingMaterials);
  if (controlSheetedShape.IsNull()) return 11;
  bool foundSplineSheeting = false;
  for (const auto& part : controlSheetingMaterials.parts) {
    if (part.name.find("sheeting") == std::string::npos) continue;
    foundSplineSheeting = true;
    std::size_t faceCount = 0;
    for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
         faces.More(); faces.Next())
      ++faceCount;
    // A continuous sheet retains sections at both faces of each rib, but each
    // interval now has only four contour faces instead of one face per sampled
    // profile segment.
    if (faceCount > 8 * ribs.size()) return 26;
  }
  if (!foundSplineSheeting) return 27;
  designrc::domain::WingParameters savedProjectParameters;
  savedProjectParameters.halfSpan = 393.7;
  savedProjectParameters.rootChord = savedProjectParameters.tipChord = 152.4;
  savedProjectParameters.sweep = 0.0;
  savedProjectParameters.dihedralDegrees = 10.0;
  savedProjectParameters.ribThickness = 3.175;
  const auto savedProjectRibs = designrc::domain::generateRibs(
      savedProjectParameters, root, root);
  designrc::domain::StructureParameters savedProjectStructure;
  savedProjectStructure.ribThickness = 3.175;
  savedProjectStructure.topSpar = savedProjectStructure.bottomSpar = true;
  savedProjectStructure.topSparHeight = savedProjectStructure.bottomSparHeight = 4.7625;
  savedProjectStructure.topSparWidth = savedProjectStructure.bottomSparWidth = 9.525;
  savedProjectStructure.leadingEdgeType = 4;
  savedProjectStructure.leadingEdgeRodOd = 2.0;
  savedProjectStructure.trailingEdgeType = 2;
  savedProjectStructure.trailingEdgeWidth = 25.4;
  savedProjectStructure.trailingEdgeHeight = 50.0;
  savedProjectStructure.leTopSheet = savedProjectStructure.leBottomSheet = true;
  savedProjectStructure.teTopSheet = savedProjectStructure.teBottomSheet = true;
  savedProjectStructure.leTopSheetThickness = savedProjectStructure.leBottomSheetThickness = 2.38125;
  savedProjectStructure.teTopSheetThickness = savedProjectStructure.teBottomSheetThickness = 2.38125;
  savedProjectStructure.leTopSheetStopRib = savedProjectStructure.leBottomSheetStopRib = 2;
  savedProjectStructure.teTopSheetStopRib = savedProjectStructure.teBottomSheetStopRib = 2;
  const auto savedProjectWing = designrc::domain::applyWingStructure(
      savedProjectRibs, savedProjectStructure);
  if (!savedProjectWing.ribs[0].booleanHoles.empty() ||
      !savedProjectWing.ribs[1].booleanHoles.empty() ||
      !savedProjectWing.ribs[2].booleanHoles.empty() ||
      savedProjectWing.ribs[0].partOutline.empty() ||
      savedProjectWing.ribs[1].partOutline.empty() ||
      savedProjectWing.ribs[2].partOutline.empty() ||
      !savedProjectWing.ribs[0].holes.empty() ||
      !savedProjectWing.ribs[1].holes.empty() ||
      !savedProjectWing.ribs[2].holes.empty()) return 9;
  const auto savedProjectShape = designrc::geometry::buildStructuredWingPreview(
      savedProjectWing, savedProjectParameters.ribThickness);
  if (savedProjectShape.IsNull()) return 10;
  const auto shape = designrc::geometry::buildWingPreview(ribs, parameters.ribThickness, false);
  std::size_t solidCount = 0;
  std::size_t meshedCapCount = 0;
  for (TopExp_Explorer explorer{shape, TopAbs_SOLID}; explorer.More(); explorer.Next()) {
    ++solidCount;
    for (TopExp_Explorer faces{explorer.Current(), TopAbs_FACE}; faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface{face};
      if (surface.GetType() != GeomAbs_Plane || std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
        continue;
      TopLoc_Location location;
      if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCapCount;
    }
  }
  for (TopExp_Explorer faces{shape, TopAbs_FACE, TopAbs_SOLID}; faces.More(); faces.Next()) {
    const auto face = TopoDS::Face(faces.Current());
    const BRepAdaptor_Surface surface{face};
    if (surface.GetType() != GeomAbs_Plane || std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
      continue;
    TopLoc_Location location;
    if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCapCount;
  }
  if (solidCount != parameters.ribCount || meshedCapCount < parameters.ribCount * 2) return 2;

  designrc::domain::StructureParameters structureParameters;
  structureParameters.topSpar = true;
  structureParameters.bottomSpar = true;
  structureParameters.shearWebs = true;
  structureParameters.topRearSpar = true;
  structureParameters.turbulators = true;
  structureParameters.turbulatorCount = 2;
  structureParameters.leadingEdgeType = 2;
  structureParameters.leadingEdgeHeight = 50.0;
  structureParameters.trailingEdgeType = 2;
  structureParameters.trailingEdgeHeight = 50.0;
  structureParameters.trailingEdgeSlotted = true;
  structureParameters.trailingEdgeSlotDepth = 6.0;
  structureParameters.ailerons = true;
  structureParameters.aileronStartRib = 3;
  structureParameters.aileronStopRib = 8;
  structureParameters.aileronWidth = 35.0;
  structureParameters.aileronHingePostWidth = 6.0;
  structureParameters.aileronHingePostHeight = 10.0;
  structureParameters.flaps = true;
  structureParameters.flapStartRib = 2;
  structureParameters.flapStopRib = 3;
  structureParameters.flapWidth = structureParameters.aileronWidth;
  structureParameters.flapHingePostWidth = 6.0;
  structureParameters.flapHingePostHeight = 10.0;
  structureParameters.controlSurfaceGap = 1.5;
  structureParameters.behindSparJoiner = true;
  structureParameters.behindSparJoinerType = 2;
  structureParameters.fiftyPercentJoiner = true;
  structureParameters.fiftyPercentJoinerType = 1;
  structureParameters.teBottomSheet = true;
  structureParameters.teTopSheet = true;
  structureParameters.teTopSheetStopRib = 3;
  structureParameters.teBottomSheetStopRib = 3;
  const auto structured = designrc::domain::applyWingStructure(ribs, structureParameters);
  designrc::geometry::MaterialShapeSet structuredMaterials;
  std::atomic_bool reportedRibs{false};
  std::atomic_bool reportedSheeting{false};
  std::atomic_bool reportedMeshing{false};
  const auto structuredShape =
      designrc::geometry::buildStructuredWingPreview(
          structured, parameters.ribThickness, nullptr, &structuredMaterials,
          [&](const int, const std::string& message) {
            reportedRibs = reportedRibs ||
                message.find("Rib Solids") != std::string::npos;
            reportedSheeting = reportedSheeting ||
                message.find("sheeting") != std::string::npos;
            reportedMeshing = reportedMeshing ||
                message.find("Meshing") != std::string::npos;
          });
  if (!reportedRibs || !reportedSheeting || !reportedMeshing) return 40;
  bool foundSplineLeadingEdge = false;
  bool foundSplineTrailingEdge = false;
  bool foundSplineAileron = false;
  bool foundSplineFlap = false;
  for (const auto& part : structuredMaterials.parts) {
    const bool leadingEdge =
        part.name.find("leading edge") != std::string::npos;
    const bool trailingEdge =
        part.name.find("trailing edge") != std::string::npos;
    const bool aileron = part.name == "Aileron";
    const bool flap = part.name == "Flap";
    if (!leadingEdge && !trailingEdge && !aileron && !flap) continue;
    std::size_t faceCount = 0;
    std::size_t splineFaceCount = 0;
    std::size_t solidCount = 0;
    for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
         faces.More(); faces.Next()) {
      ++faceCount;
      const BRepAdaptor_Surface surface{TopoDS::Face(faces.Current())};
      if (surface.GetType() == GeomAbs_BSplineSurface)
        ++splineFaceCount;
    }
    for (TopExp_Explorer solids{part.shape, TopAbs_SOLID};
         solids.More(); solids.Next())
      ++solidCount;
    if (faceCount > 6) {
      std::cerr << part.name << " has " << faceCount
                << " faces after spline lofting\n";
      return 43;
    }
    if (splineFaceCount < 2) {
      std::cerr << part.name << " has only " << splineFaceCount
                << " B-spline faces\n";
      return 45;
    }
    if ((aileron || flap) && solidCount != 1) {
      std::cerr << part.name << " has " << solidCount
                << " solids instead of one\n";
      return 46;
    }
    foundSplineLeadingEdge = foundSplineLeadingEdge || leadingEdge;
    foundSplineTrailingEdge = foundSplineTrailingEdge || trailingEdge;
    foundSplineAileron = foundSplineAileron || aileron;
    foundSplineFlap = foundSplineFlap || flap;
  }
  if (!foundSplineLeadingEdge || !foundSplineTrailingEdge ||
      !foundSplineAileron || !foundSplineFlap)
    return 44;
  std::size_t structuredSolidCount = 0;
  for (TopExp_Explorer explorer{structuredShape, TopAbs_SOLID}; explorer.More(); explorer.Next())
    ++structuredSolidCount;
  if (structuredSolidCount < parameters.ribCount) return 4;

  designrc::domain::StructureParameters carbonParameters;
  carbonParameters.carbonSpar = 1;
  carbonParameters.leadingEdgeType = 3;
  const auto carbon = designrc::domain::applyWingStructure(ribs, carbonParameters);
  auto numberedCarbon = carbon;
  for (auto& member : numberedCarbon.members)
    if (member.name.find("leading edge") != std::string::npos) member.name = "LE1";
  designrc::geometry::MaterialShapeSet carbonMaterials;
  const auto numberedCarbonShape = designrc::geometry::buildStructuredWingPreview(
      numberedCarbon, parameters.ribThickness, nullptr, &carbonMaterials);
  if (numberedCarbonShape.IsNull() ||
      !TopExp_Explorer{carbonMaterials.carbonFiber, TopAbs_FACE}.More()) return 12;
  const auto carbonShape = designrc::geometry::buildStructuredWingPreview(carbon, parameters.ribThickness);
  if (carbonShape.IsNull()) return 5;
  std::size_t carbonSolids = 0;
  std::size_t carbonMeshedCaps = 0;
  for (TopExp_Explorer solids{carbonShape, TopAbs_SOLID}; solids.More(); solids.Next()) {
    ++carbonSolids;
    for (TopExp_Explorer faces{solids.Current(), TopAbs_FACE}; faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface{face};
      if (surface.GetType() != GeomAbs_Plane ||
          std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
        continue;
      TopLoc_Location location;
      if (!BRep_Tool::Triangulation(face, location).IsNull()) ++carbonMeshedCaps;
    }
  }
  if (carbonSolids < parameters.ribCount + 2 ||
      carbonMeshedCaps < parameters.ribCount * 2) return 6;
  const auto assemblyShape = designrc::geometry::buildMirroredWingAssemblyPreview(
      {carbon}, {parameters.ribThickness});
  if (assemblyShape.IsNull()) return 7;
  designrc::domain::StructureParameters multiSparParameters;
  multiSparParameters.spars = {
      {30, 0, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {45, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {65, 1, 1, 2, 5.0, 9.0, 6.0, 5.0, 6.0, 7.0, 1.5},
      {30, 1, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  multiSparParameters.sparShearWebs = true;
  multiSparParameters.sparShearWebThickness = 3.0;
  multiSparParameters.leTopSheet = multiSparParameters.teTopSheet = true;
  multiSparParameters.leBottomSheet = multiSparParameters.teBottomSheet = true;
  multiSparParameters.leTopSheetStopRib = multiSparParameters.teTopSheetStopRib = 3;
  multiSparParameters.leBottomSheetStopRib = multiSparParameters.teBottomSheetStopRib = 3;
  const auto multiSparWing = designrc::domain::applyWingStructure(
      ribs, multiSparParameters);
  const auto multiSparShape = designrc::geometry::buildStructuredWingPreview(
      multiSparWing, parameters.ribThickness);
  if (multiSparShape.IsNull()) return 13;

  designrc::domain::StructureParameters collidingSpars;
  collidingSpars.spars = {
      {40, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {40, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  bool namedCollision = false;
  try {
    const auto collidingWing = designrc::domain::applyWingStructure(
        ribs, collidingSpars);
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        collidingWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedCollision = message.find("Spar 1") != std::string::npos &&
        message.find("Spar 2") != std::string::npos;
  }
  if (!namedCollision) return 14;
  designrc::domain::StructureParameters joinerCollisionParameters;
  joinerCollisionParameters.spars = {
      {40, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 8.0, 6.0, 1.0}};
  auto joinerCollisionWing = designrc::domain::applyWingStructure(
      ribs, joinerCollisionParameters);
  const auto& spar = joinerCollisionWing.members.front();
  designrc::domain::JoinerPart collidingJoiner;
  collidingJoiner.name = "Fixed Joiner 1 CF Rod";
  collidingJoiner.kind = designrc::domain::SpanMemberKind::Rod;
  collidingJoiner.outerDiameter = 6.0;
  collidingJoiner.hasExplicitEndpoints = true;
  collidingJoiner.innerEndpoint = {
      ribs[0].leadingEdgeOffset + spar.centers[0].x,
      ribs[0].spanPosition, ribs[0].dihedralHeight + spar.centers[0].y};
  collidingJoiner.outerEndpoint = {
      ribs[1].leadingEdgeOffset + spar.centers[1].x,
      ribs[1].spanPosition, ribs[1].dihedralHeight + spar.centers[1].y};
  joinerCollisionWing.joiners.push_back(collidingJoiner);
  bool namedJoinerCollision = false;
  try {
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        joinerCollisionWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedJoinerCollision = message.find("Fixed Joiner 1") != std::string::npos &&
        message.find("Spar 1") != std::string::npos;
  }
  if (!namedJoinerCollision) return 15;

  auto woodJoinerContactWing = joinerCollisionWing;
  woodJoinerContactWing.joiners.back().name = "Wood Fixed Joiner 1";
  const auto woodJoinerContactShape = designrc::geometry::buildStructuredWingPreview(
      woodJoinerContactWing, parameters.ribThickness);
  if (woodJoinerContactShape.IsNull()) return 16;

  auto unmirroredJoinerWing = designrc::domain::applyWingStructure(ribs, {});
  collidingJoiner.name = "Alignment Pin 1 CF";
  collidingJoiner.innerEndpoint = {0.80 * ribs[0].chord, ribs[0].spanPosition, 0.0};
  collidingJoiner.outerEndpoint = {0.80 * ribs[1].chord, ribs[1].spanPosition, 0.0};
  collidingJoiner.mirrorInAssembly = false;
  unmirroredJoinerWing.joiners.push_back(collidingJoiner);
  auto steelJoiner = collidingJoiner;
  steelJoiner.name = "Removable Joiner 1 This Panel Steel";
  steelJoiner.innerEndpoint.x = 0.72 * ribs[0].chord;
  steelJoiner.outerEndpoint.x = 0.72 * ribs[1].chord;
  unmirroredJoinerWing.joiners.push_back(steelJoiner);
  auto fiberglassJoiner = collidingJoiner;
  fiberglassJoiner.name = "Removable Joiner 2 This Panel Fiberglass";
  fiberglassJoiner.kind = designrc::domain::SpanMemberKind::Tube;
  fiberglassJoiner.outerDiameter = 4.0;
  fiberglassJoiner.innerDiameter = 3.0;
  fiberglassJoiner.innerEndpoint.x = 0.88 * ribs[0].chord;
  fiberglassJoiner.outerEndpoint.x = 0.88 * ribs[1].chord;
  unmirroredJoinerWing.joiners.push_back(fiberglassJoiner);
  designrc::geometry::MaterialShapeSet unmirroredMaterials;
  const auto unmirroredShape = designrc::geometry::buildStructuredWingPreview(
      unmirroredJoinerWing, parameters.ribThickness, nullptr, &unmirroredMaterials);
  if (unmirroredShape.IsNull() ||
      !TopExp_Explorer{unmirroredMaterials.unmirroredCarbonFiber, TopAbs_FACE}.More() ||
      !TopExp_Explorer{unmirroredMaterials.unmirroredSteel, TopAbs_FACE}.More() ||
      !TopExp_Explorer{unmirroredMaterials.unmirroredFiberglass, TopAbs_FACE}.More())
    return 17;
  designrc::domain::StructureParameters spoilerParameters;
  spoilerParameters.spoilers = true;
  spoilerParameters.spoilerStartRib = 3;
  spoilerParameters.spoilerEndRib = 7;
  spoilerParameters.spoilerChordLocationPercent = 35;
  spoilerParameters.spoilerWidth = 25.4;
  spoilerParameters.spoilerThickness = 3.0;
  spoilerParameters.spoilerFrameRailWidth = 6.0;
  spoilerParameters.spoilerSupportRailHeight = 3.0;
  spoilerParameters.leTopSheet = true;
  spoilerParameters.teTopSheet = true;
  spoilerParameters.leTopSheetStopRib = static_cast<int>(ribs.size());
  spoilerParameters.teTopSheetStopRib = static_cast<int>(ribs.size());
  const auto spoilerWing = designrc::domain::applyWingStructure(ribs, spoilerParameters);
  const auto spoilerShape = designrc::geometry::buildStructuredWingPreview(
      spoilerWing, parameters.ribThickness);
  if (spoilerShape.IsNull()) return 18;
  auto lightenedSpoilerParameters = spoilerParameters;
  lightenedSpoilerParameters.spoilerLighteningHoles = true;
  lightenedSpoilerParameters.spoilerMinimumWoodMargin = 6.0;
  lightenedSpoilerParameters.spoilerMinimumCircleDistance = 12.0;
  const auto lightenedSpoilerWing = designrc::domain::applyWingStructure(
      ribs, lightenedSpoilerParameters);
  designrc::geometry::MaterialShapeSet lightenedSpoilerMaterials;
  const auto lightenedSpoilerShape =
      designrc::geometry::buildStructuredWingPreview(
          lightenedSpoilerWing, parameters.ribThickness, nullptr,
          &lightenedSpoilerMaterials);
  if (lightenedSpoilerShape.IsNull()) return 35;
  const auto lightenedSpoilerPart = std::find_if(
      lightenedSpoilerMaterials.parts.begin(),
      lightenedSpoilerMaterials.parts.end(),
      [](const auto& part) { return part.name == "Spoiler"; });
  if (lightenedSpoilerPart == lightenedSpoilerMaterials.parts.end())
    return 36;
  std::size_t lightenedSpoilerFaceCount = 0;
  for (TopExp_Explorer faces{
           lightenedSpoilerPart->shape, TopAbs_FACE};
       faces.More(); faces.Next())
    ++lightenedSpoilerFaceCount;
  if (lightenedSpoilerFaceCount <= 6) return 37;
  auto ribLighteningParameters = spoilerParameters;
  ribLighteningParameters.spoilers = false;
  ribLighteningParameters.ribLighteningHoles = true;
  ribLighteningParameters.ribLighteningStartRib = 3;
  ribLighteningParameters.ribLighteningStopRib = 5;
  ribLighteningParameters.ribLighteningMinimumWoodMargin = 3.0;
  ribLighteningParameters.ribLighteningMinimumHoleDistance = 8.0;
  auto ribLightenedWing = designrc::domain::applyWingStructure(
      ribs, ribLighteningParameters);
  designrc::domain::addRibLighteningHoles(
      ribLightenedWing, ribLighteningParameters);
  if (ribLightenedWing.ribs[2].internalCutouts.empty() ||
      ribLightenedWing.ribs[3].internalCutouts.empty() ||
      ribLightenedWing.ribs[4].internalCutouts.empty())
    return 38;
  const auto ribLightenedShape =
      designrc::geometry::buildStructuredWingPreview(
          ribLightenedWing, parameters.ribThickness);
  if (ribLightenedShape.IsNull()) return 39;
  auto centerSpoilerParameters = spoilerParameters;
  centerSpoilerParameters.spoilerStartRib = 1;
  centerSpoilerParameters.spoilerEndRib = 5;
  centerSpoilerParameters.leTopSheet = false;
  centerSpoilerParameters.teTopSheet = false;
  centerSpoilerParameters.spoilerLighteningHoles = true;
  centerSpoilerParameters.spoilerMinimumWoodMargin = 6.0;
  centerSpoilerParameters.spoilerMinimumCircleDistance = 12.0;
  const auto centerSpoilerWing = designrc::domain::applyWingStructure(
      ribs, centerSpoilerParameters);
  designrc::geometry::MaterialShapeSet centerSpoilerMaterials;
  const auto centerSpoilerShape =
      designrc::geometry::buildStructuredWingPreview(
          centerSpoilerWing, parameters.ribThickness, nullptr,
          &centerSpoilerMaterials);
  if (centerSpoilerShape.IsNull()) return 21;
  std::size_t centerLongParts = 0;
  for (const auto& part : centerSpoilerMaterials.parts) {
    if (part.name != "Spoiler" &&
        !part.name.starts_with("Spoiler Frame Rail"))
      continue;
    ++centerLongParts;
    if (part.mirrorInAssembly || part.shape.ShapeType() != TopAbs_SOLID)
      return 22;
  }
  if (centerLongParts != 3) return 23;

  designrc::domain::StructureParameters wiringSparCollision;
  wiringSparCollision.spars = {
      {45, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 8.0, 6.0, 1.0}};
  wiringSparCollision.wiringHoles = true;
  wiringSparCollision.wiringHoleStartRib = 2;
  wiringSparCollision.wiringHoleEndRib = 2;
  wiringSparCollision.wiringHoleChordLocationPercent = 45;
  bool namedWiringSparCollision = false;
  try {
    const auto wired = designrc::domain::applyWingStructure(ribs, wiringSparCollision);
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        wired, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedWiringSparCollision = message.find("Wiring Hole R2") != std::string::npos &&
        message.find("Spar 1") != std::string::npos;
  }
  if (!namedWiringSparCollision) return 19;

  designrc::domain::StructureParameters wiringJoinerParameters;
  wiringJoinerParameters.wiringHoles = true;
  wiringJoinerParameters.wiringHoleStartRib = 1;
  wiringJoinerParameters.wiringHoleEndRib = 2;
  wiringJoinerParameters.wiringHoleChordLocationPercent = 70;
  auto wiringJoinerWing = designrc::domain::applyWingStructure(
      ribs, wiringJoinerParameters);
  designrc::domain::JoinerPart wiringJoiner;
  wiringJoiner.name = "Fixed Joiner 1 CF Rod";
  wiringJoiner.kind = designrc::domain::SpanMemberKind::Rod;
  wiringJoiner.outerDiameter = 4.0;
  wiringJoiner.hasExplicitEndpoints = true;
  const auto wiringCenterX = [](const auto& rib) {
    return 0.70 * rib.chord + 9.525 * 0.5;
  };
  wiringJoiner.innerEndpoint = {ribs[0].leadingEdgeOffset + wiringCenterX(ribs[0]),
      ribs[0].spanPosition, ribs[0].dihedralHeight};
  wiringJoiner.outerEndpoint = {ribs[1].leadingEdgeOffset + wiringCenterX(ribs[1]),
      ribs[1].spanPosition, ribs[1].dihedralHeight};
  wiringJoinerWing.joiners.push_back(wiringJoiner);
  bool namedWiringJoinerCollision = false;
  try {
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        wiringJoinerWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedWiringJoinerCollision = message.find("Wiring Hole") != std::string::npos &&
        message.find("Fixed Joiner 1") != std::string::npos;
  }
  if (!namedWiringJoinerCollision) return 20;

  designrc::domain::StructureParameters ribletParameters;
  ribletParameters.leadingEdgeType = 3;
  ribletParameters.leadingEdgeTubeOd = 4.0;
  ribletParameters.leadingEdgeTubeId = 3.0;
  ribletParameters.spars = {
      {30, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  ribletParameters.riblets = true;
  ribletParameters.ribletStartRib = 2;
  ribletParameters.ribletEndRib = 5;
  ribletParameters.ribletsPerBay = 2;
  auto ribletWing =
      designrc::domain::applyWingStructure(ribs, ribletParameters);
  for (std::size_t index = 0; index < ribletWing.ribs.size(); ++index)
    ribletWing.ribs[index].name = "R" + std::to_string(index + 1);
  designrc::domain::addRiblets(ribletWing, ribletParameters);
  designrc::geometry::MaterialShapeSet ribletMaterials;
  const auto ribletShape =
      designrc::geometry::buildStructuredWingPreview(
          ribletWing, parameters.ribThickness, nullptr,
          &ribletMaterials);
  if (ribletShape.IsNull()) return 24;
  if (std::count_if(
          ribletMaterials.parts.begin(), ribletMaterials.parts.end(),
          [](const auto& part) {
            return part.name.size() > 2 &&
                part.name.front() == 'R' &&
                std::isalpha(static_cast<unsigned char>(part.name.back()));
          }) != 6)
    return 25;
  return 0;
}

int main(int argc, char* argv[]) {
  try {
    return runTest(argc, argv);
  } catch (const Standard_Failure& error) {
    std::cerr << "OCCT: " << error.GetMessageString() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  }
  return 3;
}
