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

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

int runTest(int argc, char* argv[]) {
  using designrc::domain::AirfoilProfile;
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
  const auto earlyCfSheetedShape = designrc::geometry::buildStructuredWingPreview(
      earlyCfSheeted, parameters.ribThickness);
  if (earlyCfSheetedShape.IsNull()) return 8;
  designrc::domain::StructureParameters controlSheeting;
  controlSheeting.teTopSheet = controlSheeting.teBottomSheet = true;
  controlSheeting.teTopSheetStopRib = controlSheeting.teBottomSheetStopRib =
      static_cast<int>(ribs.size());
  controlSheeting.ailerons = true;
  controlSheeting.aileronStartRib = 5;
  controlSheeting.aileronStopRib = 7;
  controlSheeting.flaps = true;
  controlSheeting.flapStartRib = 2;
  controlSheeting.flapStopRib = 4;
  const auto controlSheetedWing = designrc::domain::applyWingStructure(ribs, controlSheeting);
  const auto controlSheetedShape = designrc::geometry::buildStructuredWingPreview(
      controlSheetedWing, parameters.ribThickness);
  if (controlSheetedShape.IsNull()) return 11;
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
  savedProjectStructure.centerSparWoodJoiner = true;
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
      savedProjectWing.ribs[0].holes.size() != 1 ||
      savedProjectWing.ribs[1].holes.size() != 1 ||
      savedProjectWing.ribs[2].holes.size() != 1) return 9;
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
  structureParameters.controlSurfaceGap = 1.5;
  structureParameters.behindSparJoiner = true;
  structureParameters.behindSparJoinerType = 2;
  structureParameters.fiftyPercentJoiner = true;
  structureParameters.fiftyPercentJoinerType = 1;
  structureParameters.centerSparWoodJoiner = true;
  structureParameters.leBottomSheet = true;
  structureParameters.leBottomSheetStopRib = 3;
  structureParameters.teBottomSheet = true;
  structureParameters.teBottomSheetStopRib = 3;
  const auto structured = designrc::domain::applyWingStructure(ribs, structureParameters);
  const auto structuredShape =
      designrc::geometry::buildStructuredWingPreview(structured, parameters.ribThickness);
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
