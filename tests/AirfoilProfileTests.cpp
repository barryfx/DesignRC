#include "domain/AirfoilProfile.h"
#include "domain/WingDesign.h"
#include "domain/DxfExporter.h"
#include "domain/WingStructure.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <stdexcept>

int main() {
  using designrc::domain::AirfoilProfile;

  const auto panelAngles = designrc::domain::calculatePanelAssemblyAngles({10.0, 6.0, 4.0});
  assert(panelAngles.size() == 3);
  assert(std::abs(panelAngles[0].panelInclinationDegrees - 5.0) < 1.0e-9);
  assert(std::abs(panelAngles[1].panelInclinationDegrees - 11.0) < 1.0e-9);
  assert(std::abs(panelAngles[2].panelInclinationDegrees - 15.0) < 1.0e-9);
  assert(std::abs(panelAngles[0].rootRibAngleDegrees - 5.0) < 1.0e-9);
  assert(std::abs(panelAngles[0].intermediateRibAngleDegrees - 5.0) < 1.0e-9);
  assert(std::abs(panelAngles[0].tipRibAngleDegrees - 8.0) < 1.0e-9);
  assert(std::abs(panelAngles[1].rootRibAngleDegrees - 8.0) < 1.0e-9);
  assert(std::abs(panelAngles[1].intermediateRibAngleDegrees - 11.0) < 1.0e-9);
  assert(std::abs(panelAngles[1].tipRibAngleDegrees - 13.0) < 1.0e-9);
  assert(std::abs(panelAngles[2].rootRibAngleDegrees - 13.0) < 1.0e-9);
  assert(std::abs(panelAngles[2].intermediateRibAngleDegrees - 15.0) < 1.0e-9);

  std::istringstream dat{
      "Test foil\n"
      "1.0 0.0\n"
      "0.5 0.08\n"
      "0.0 0.0\n"
      "0.5 -0.08\n"
      "1.0 0.0\n"};
  const auto imported = AirfoilProfile::fromDat(dat);
  assert(imported.name() == "Test foil");
  assert(imported.resampled(11).size() == 21);

  const auto thick = AirfoilProfile::nacaSymmetric(0.18);
  const auto thin = AirfoilProfile::nacaSymmetric(0.08);
  const auto middle = AirfoilProfile::interpolate(thick, thin, 0.5, 31);
  assert(middle.outline().size() == 61);
  assert(std::abs(middle.outline().front().x - 1.0) < 1.0e-9);

  designrc::domain::WingParameters parameters;
  parameters.ribCount = 7;
  const auto ribs = designrc::domain::generateRibs(parameters, thick, thin);
  assert(ribs.size() == 7);
  assert(std::abs(ribs.front().chord - parameters.rootChord) < 1.0e-9);
  assert(std::abs(ribs.back().chord - parameters.tipChord) < 1.0e-9);
  assert(std::abs(ribs.back().spanPosition - parameters.halfSpan) < 1.0e-9);
  designrc::domain::WingParameters constantPanel;
  constantPanel.rootChord = constantPanel.tipChord = 200.0;
  constantPanel.sweep = 0.0;
  const auto constantRibs = designrc::domain::generateRibs(constantPanel, thick, thick);
  for (const auto& rib : constantRibs) {
    assert(std::abs(rib.chord - 200.0) < 1.0e-9);
    assert(std::abs(rib.leadingEdgeOffset) < 1.0e-9);
  }
  assert(std::abs(constantRibs.front().profile.outline()[20].y -
                  constantRibs.back().profile.outline()[20].y) < 1.0e-9);
  parameters.tipTwistDegrees = 6.0;
  const auto twistedRibs = designrc::domain::generateRibs(parameters, thick, thin);
  assert(std::abs(twistedRibs.front().twistDegrees) < 1.0e-9);
  assert(std::abs(twistedRibs.back().twistDegrees - 6.0) < 1.0e-9);
  const auto metrics = designrc::domain::calculateWingMetrics(parameters);
  assert(std::abs(metrics.fullSpan - parameters.halfSpan * 2.0) < 1.0e-9);
  assert(std::abs(metrics.taperRatio - parameters.tipChord / parameters.rootChord) < 1.0e-9);

  designrc::domain::StructureParameters structureParameters;
  structureParameters.topSpar = true;
  structureParameters.bottomSpar = true;
  structureParameters.shearWebs = true;
  structureParameters.topRearSpar = true;
  structureParameters.bottomRearSpar = true;
  structureParameters.turbulators = true;
  structureParameters.turbulatorCount = 3;
  const auto structured = designrc::domain::applyWingStructure(ribs, structureParameters);
  assert(structured.ribs.size() == ribs.size());
  assert(structured.members.size() == 7);
  assert(structured.shearWebs.size() == ribs.size() - 1);
  assert(structured.ribs.front().outerOutline.size() > ribs.front().profile.outline().size());
  bool foundSixtyPercentRearSpar = false;
  for (const auto& member : structured.members) {
    if (member.name != "Top 60% rear spar") continue;
    assert(std::abs(member.centers.front().x - 0.60 * ribs.front().chord) < 1.0e-9);
    foundSixtyPercentRearSpar = true;
  }
  assert(foundSixtyPercentRearSpar);
  for (const auto& web : structured.shearWebs) {
    assert(web.name == "SW" + std::to_string(web.bayIndex));
    assert(web.outline.size() == 4);
    assert(web.stationCorners[2].y > web.stationCorners[1].y);
    assert(web.stationCorners[3].y > web.stationCorners[0].y);
  }

  designrc::domain::StructureParameters sheetedSparParameters;
  sheetedSparParameters.topSpar = sheetedSparParameters.bottomSpar = true;
  sheetedSparParameters.leTopSheet = sheetedSparParameters.leBottomSheet = true;
  sheetedSparParameters.teTopSheet = sheetedSparParameters.teBottomSheet = true;
  sheetedSparParameters.leTopSheetStopRib = sheetedSparParameters.leBottomSheetStopRib = 2;
  sheetedSparParameters.teTopSheetStopRib = sheetedSparParameters.teBottomSheetStopRib = 2;
  const auto sheetedSpar = designrc::domain::applyWingStructure(ribs, sheetedSparParameters);
  const double sparRight = 0.25 * ribs.front().chord + sheetedSparParameters.topSparWidth * 0.5;
  const auto& recessedOutline = sheetedSpar.ribs.front().outerOutline;
  const auto checkNoSheetingSpike = [&](const bool top) {
    double boundarySurface = top ? -1.0e9 : 1.0e9;
    double outsideX = 1.0e9;
    double outsideY = 0.0;
    for (const auto point : recessedOutline) {
      if (std::abs(point.x - sparRight) < 1.0e-5 && (top ? point.y > 0.0 : point.y < 0.0))
        boundarySurface = top ? std::max(boundarySurface, point.y)
                              : std::min(boundarySurface, point.y);
      if (point.x > sparRight + 1.0e-5 && point.x < outsideX &&
          (top ? point.y > 0.0 : point.y < 0.0)) {
        outsideX = point.x;
        outsideY = point.y;
      }
    }
    assert(outsideX < 1.0e8);
    assert(std::abs(boundarySurface - outsideY) < 0.5);
  };
  checkNoSheetingSpike(true);
  checkNoSheetingSpike(false);

  designrc::domain::WingParameters thinSparParameters;
  thinSparParameters.halfSpan = 200.0;
  thinSparParameters.rootChord = thinSparParameters.tipChord = 76.2;
  thinSparParameters.ribCount = 4;
  const auto thinSparRibs = designrc::domain::generateRibs(
      thinSparParameters, thin, thin);
  designrc::domain::StructureParameters overlappingSpars;
  overlappingSpars.topSpar = overlappingSpars.bottomSpar = true;
  overlappingSpars.topSparHeight = overlappingSpars.bottomSparHeight = 4.7625;
  bool rejectedOverlappingSpars = false;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(
        thinSparRibs, overlappingSpars));
  } catch (const std::invalid_argument& error) {
    rejectedOverlappingSpars = std::string{error.what()}.find(
        "wood-spar notches overlap") != std::string::npos;
  }
  assert(rejectedOverlappingSpars);

  auto joinerRibs = ribs;
  const auto& r1 = ribs[0];
  const auto& r2 = ribs[1];
  joinerRibs.insert(joinerRibs.begin() + 1, {0.5 * (r1.spanPosition + r2.spanPosition),
      0.5 * (r1.chord + r2.chord), 0.5 * (r1.leadingEdgeOffset + r2.leadingEdgeOffset),
      0.5 * (r1.dihedralHeight + r2.dihedralHeight),
      0.5 * (r1.twistDegrees + r2.twistDegrees),
      0.5 * (r1.ribPlaneAngleDegrees + r2.ribPlaneAngleDegrees), -0.5,
      AirfoilProfile::interpolate(thick, thin, 1.0 / 12.0)});
  designrc::domain::StructureParameters joinerParameters;
  joinerParameters.rib1aPresent = true;
  joinerParameters.topSpar = true;
  joinerParameters.bottomSpar = true;
  joinerParameters.shearWebs = true;
  joinerParameters.centerSparWoodJoiner = true;
  joinerParameters.behindSparJoiner = true;
  joinerParameters.behindSparJoinerType = 2;
  joinerParameters.fiftyPercentJoiner = true;
  joinerParameters.fiftyPercentJoinerType = 1;
  joinerParameters.joinerAxisAngleDegrees = 8.0;
  joinerParameters.joinerDihedralDegrees = 6.0;
  const auto joined = designrc::domain::applyWingStructure(joinerRibs, joinerParameters);
  assert(joined.joiners.size() == 3);
  assert(std::abs(joined.joiners[0].centers.front().x - 0.30 * joinerRibs.front().chord) < 1.0e-8);
  assert(std::abs(joined.joiners[1].centers.front().x - 0.60 * joinerRibs.front().chord) < 1.0e-8);
  assert(joined.ribs[0].booleanHoles.size() == 2 && joined.ribs[0].booleanCutouts.size() == 1);
  assert(joined.ribs[1].booleanHoles.size() == 2 && joined.ribs[1].booleanCutouts.size() == 1);
  assert(joined.ribs[2].booleanHoles.size() == 2);
  assert(joined.joiners.back().rectangularProfiles.size() == 3);
  for (const auto& web : joined.shearWebs)
    assert(web.bayIndex > joined.joiners.back().stopRibIndex);
  assert(joined.joiners.back().dxfOutline.size() == 6);
  assert(joined.joiners.back().dxfOutline[0].y > joined.joiners.back().dxfOutline[1].y);
  assert(joined.joiners.back().dxfOutline[5].y > joined.joiners.back().dxfOutline[4].y);
  const double bottomLeftAngle = std::atan2(
      joined.joiners.back().dxfOutline[0].y - joined.joiners.back().dxfOutline[1].y,
      joined.joiners.back().dxfOutline[1].x - joined.joiners.back().dxfOutline[0].x);
  const double bottomRightAngle = std::atan2(
      joined.joiners.back().dxfOutline[2].y - joined.joiners.back().dxfOutline[1].y,
      joined.joiners.back().dxfOutline[2].x - joined.joiners.back().dxfOutline[1].x);
  const double topLeftAngle = std::atan2(
      joined.joiners.back().dxfOutline[5].y - joined.joiners.back().dxfOutline[4].y,
      joined.joiners.back().dxfOutline[4].x - joined.joiners.back().dxfOutline[5].x);
  const double topRightAngle = std::atan2(
      joined.joiners.back().dxfOutline[3].y - joined.joiners.back().dxfOutline[4].y,
      joined.joiners.back().dxfOutline[3].x - joined.joiners.back().dxfOutline[4].x);
  assert(std::abs((bottomLeftAngle + bottomRightAngle) * 180.0 / std::numbers::pi - 6.0) < 1.0e-9);
  assert(std::abs((topLeftAngle + topRightAngle) * 180.0 / std::numbers::pi - 6.0) < 1.0e-9);
  const double joinerTop = joined.joiners.back().rectangularProfiles.front()[2].y;
  assert(std::any_of(joined.ribs.front().outerOutline.begin(),
      joined.ribs.front().outerOutline.end(), [joinerTop](const auto& point) {
        return std::abs(point.y - joinerTop) < 1.0e-8;
      }));

  designrc::domain::StructureParameters sheetingParameters;
  sheetingParameters.carbonSpar = 1;
  sheetingParameters.leadingEdgeType = 3;
  sheetingParameters.leTopSheet = true;
  sheetingParameters.leTopSheetThickness = 2.0;
  sheetingParameters.leTopSheetStopRib = 3;
  sheetingParameters.teTopSheet = true;
  sheetingParameters.teTopSheetThickness = 2.0;
  sheetingParameters.teTopSheetStopRib = 3;
  const auto sheeted = designrc::domain::applyWingStructure(ribs, sheetingParameters);
  assert(sheeted.sheeting.size() == 2);
  assert(sheeted.sheeting[0].profiles.size() == 3);
  assert(sheeted.sheeting[1].profiles.size() == 3);
  assert(std::abs(sheeted.sheeting[0].profiles.front()[47].x -
                  0.25 * ribs.front().chord) < 1.0e-8);
  assert(std::abs(sheeted.sheeting[1].profiles.front().front().x -
                  0.25 * ribs.front().chord) < 1.0e-8);
  assert(std::abs(sheeted.sheeting[0].profiles.front().front().x -
                  (sheeted.members[1].centers.front().x +
                   sheetingParameters.leadingEdgeTubeOd * 0.5 + 0.05)) < 1.0e-8);
  assert(sheeted.ribs.front().booleanHoles.empty());
  assert(sheeted.ribs.front().holes.size() == 2);

  designrc::domain::StructureParameters controlSheetingParameters;
  controlSheetingParameters.teTopSheet = true;
  controlSheetingParameters.teTopSheetThickness = 2.0;
  controlSheetingParameters.teTopSheetStopRib = 7;
  controlSheetingParameters.ailerons = true;
  controlSheetingParameters.aileronStartRib = 2;
  controlSheetingParameters.aileronStopRib = 4;
  controlSheetingParameters.aileronWidth = 35.0;
  controlSheetingParameters.aileronHingePostWidth = 6.0;
  controlSheetingParameters.controlSurfaceGap = 1.5;
  const auto controlSheeted = designrc::domain::applyWingStructure(
      ribs, controlSheetingParameters);
  assert(controlSheeted.sheeting.size() == 1);
  const auto maximumProfileX = [](const std::vector<designrc::domain::Point2>& profile) {
    return std::max_element(profile.begin(), profile.end(),
        [](const auto& a, const auto& b) { return a.x < b.x; })->x;
  };
  for (const std::size_t boundary : {std::size_t{1}, std::size_t{3}}) {
    const double expectedHingeEdge = ribs[boundary].chord -
        controlSheetingParameters.aileronWidth -
        controlSheetingParameters.controlSurfaceGap -
        controlSheetingParameters.aileronHingePostWidth;
    assert(std::abs(maximumProfileX(controlSheeted.sheeting.front().controlProfiles[boundary]) -
                    expectedHingeEdge) < 1.0e-8);
    assert(maximumProfileX(controlSheeted.sheeting.front().profiles[boundary]) >
           expectedHingeEdge + controlSheetingParameters.aileronWidth);
    assert(maximumProfileX(controlSheeted.sheeting.front().fullProfiles[boundary]) >
           expectedHingeEdge + controlSheetingParameters.aileronWidth);
    assert(maximumProfileX(controlSheeted.ribs[boundary].outerOutline) >
           expectedHingeEdge + controlSheetingParameters.aileronWidth);
  }
  assert(!controlSheeted.sheeting.front().controlBays[0]);
  assert(controlSheeted.sheeting.front().controlBays[1]);
  assert(controlSheeted.sheeting.front().controlBays[2]);
  assert(!controlSheeted.sheeting.front().controlBays[3]);

  designrc::domain::StructureParameters carbonParameters;
  carbonParameters.carbonSpar = 1;
  const auto carbonStructured = designrc::domain::applyWingStructure(ribs, carbonParameters);
  assert(carbonStructured.members.size() == 1);
  assert(carbonStructured.members.front().kind == designrc::domain::SpanMemberKind::Tube);
  assert(carbonStructured.ribs.front().holes.size() == 1);

  auto edgeParameters = structureParameters;
  edgeParameters.leadingEdgeType = 1;
  edgeParameters.leadingEdgeWidth = 8.0;
  edgeParameters.trailingEdgeType = 1;
  edgeParameters.trailingEdgeWidth = 18.0;
  const auto edged = designrc::domain::applyWingStructure(ribs, edgeParameters);
  assert(edged.profiledMembers.size() == 2);
  assert(std::abs(edged.profiledMembers.front().profiles.front().front().x -
                  edgeParameters.leadingEdgeWidth) < 1.0e-9);
  assert(std::abs(edged.profiledMembers.front().profiles.front().back().x -
                  edgeParameters.leadingEdgeWidth) < 1.0e-9);
  const auto& leadingProfile = edged.profiledMembers.front().profiles.front();
  for (const auto endpoint : {leadingProfile.front(), leadingProfile.back()}) {
    const bool matchingRibCorner = std::any_of(
        edged.ribs.front().outerOutline.begin(), edged.ribs.front().outerOutline.end(),
        [endpoint](const auto& point) {
          return std::hypot(point.x - endpoint.x, point.y - endpoint.y) < 1.0e-8;
        });
    assert(matchingRibCorner);
  }
  const auto [minimumX, maximumX] = std::minmax_element(
      edged.ribs.front().outerOutline.begin(), edged.ribs.front().outerOutline.end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  assert(std::abs(minimumX->x - edgeParameters.leadingEdgeWidth) < 1.0e-9);
  assert(std::abs(maximumX->x - (ribs.front().chord - edgeParameters.trailingEdgeWidth)) < 1.0e-9);

  designrc::domain::StructureParameters undersizedLe;
  undersizedLe.leadingEdgeType = 1;
  undersizedLe.leadingEdgeWidth = 1.0;
  undersizedLe.leadingEdgeHeight = 50.0;
  bool rejectedUndersizedLe = false;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(constantRibs, undersizedLe));
  } catch (const std::invalid_argument& error) {
    rejectedUndersizedLe = std::string{error.what()}.find(
        "less than the specified LE Height") != std::string::npos;
  }
  assert(rejectedUndersizedLe);

  designrc::domain::StructureParameters undersizedTe;
  undersizedTe.trailingEdgeType = 2;
  undersizedTe.trailingEdgeWidth = 1.0;
  undersizedTe.trailingEdgeHeight = 10.0;
  bool rejectedUndersizedTe = false;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(constantRibs, undersizedTe));
  } catch (const std::invalid_argument& error) {
    rejectedUndersizedTe = std::string{error.what()}.find(
        "less than the specified TE Height") != std::string::npos;
  }
  assert(rejectedUndersizedTe);

  designrc::domain::StructureParameters leadingTubeParameters;
  leadingTubeParameters.leadingEdgeType = 3;
  const auto leadingTube = designrc::domain::applyWingStructure(ribs, leadingTubeParameters);
  assert(leadingTube.ribs.front().holes.size() == 1);
  assert(leadingTube.members.size() == 1);
  assert(leadingTube.members.front().kind == designrc::domain::SpanMemberKind::Tube);
  assert(leadingTube.members.front().centers.front().x >
         leadingTubeParameters.leadingEdgeTubeOd * 0.5);

  designrc::domain::StructureParameters controlParameters;
  controlParameters.trailingEdgeType = 1;
  controlParameters.trailingEdgeWidth = 20.0;
  controlParameters.ailerons = true;
  controlParameters.aileronStartRib = 2;
  controlParameters.aileronStopRib = 6;
  controlParameters.aileronWidth = 35.0;
  controlParameters.aileronHingePostWidth = 6.0;
  controlParameters.controlSurfaceGap = 1.5;
  const auto controlled = designrc::domain::applyWingStructure(ribs, controlParameters);
  assert(controlled.controlSurfaces.size() == 1);
  assert(controlled.controlSurfaces.front().profiles.size() == 5);
  assert(controlled.profiledMembers.front().activeRanges.size() == 2);
  const auto boundaryMaximum = std::max_element(
      controlled.ribs[1].outerOutline.begin(), controlled.ribs[1].outerOutline.end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  const auto intermediateMaximum = std::max_element(
      controlled.ribs[2].outerOutline.begin(), controlled.ribs[2].outerOutline.end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  assert(std::abs(boundaryMaximum->x - (ribs[1].chord - 20.0)) < 1.0e-9);
  assert(std::abs(intermediateMaximum->x -
      (ribs[2].chord - 35.0 - 6.0 - 1.5)) < 1.0e-9);

  auto boundedControlParameters = controlParameters;
  boundedControlParameters.aileronStartRib = 1;
  boundedControlParameters.aileronStopRib = static_cast<int>(ribs.size());
  const auto boundedControl = designrc::domain::applyWingStructure(ribs, boundedControlParameters);
  assert(boundedControl.controlSurfaces.size() == 1);
  assert(boundedControl.controlSurfaces.front().startRibIndex == 1);
  assert(boundedControl.controlSurfaces.front().stopRibIndex == ribs.size() - 2);

  designrc::domain::StructureParameters sheetTeParameters;
  sheetTeParameters.ribThickness = parameters.ribThickness;
  sheetTeParameters.trailingEdgeType = 2;
  sheetTeParameters.trailingEdgeWidth = 20.0;
  sheetTeParameters.trailingEdgeSlotted = true;
  sheetTeParameters.trailingEdgeSlotDepth = 6.0;
  const auto sheetTe = designrc::domain::applyWingStructure(ribs, sheetTeParameters);
  assert(sheetTe.sheetStockParts.size() == 1);
  assert(sheetTe.sheetStockParts.front().outline.size() == ribs.size() * 4);
  assert(sheetTe.sheetStockParts.front().slots.empty());
  const auto& openCornerStock = sheetTe.sheetStockParts.front();
  for (std::size_t ribIndex = 1; ribIndex + 1 < ribs.size(); ++ribIndex) {
    const double slotLeading = ribs[ribIndex].leadingEdgeOffset +
        ribs[ribIndex].chord - sheetTeParameters.trailingEdgeWidth;
    const auto mouthPoints = std::count_if(
        openCornerStock.outline.begin(), openCornerStock.outline.end(),
        [slotLeading](const auto point) {
          return std::abs(point.x - slotLeading) < 1.0e-8;
        });
    assert(mouthPoints >= 2);
  }
  assert(sheetTe.profiledMembers.front().slotProfiles.size() == ribs.size());
  const auto sheetRibMaximum = std::max_element(
      sheetTe.ribs.front().outerOutline.begin(), sheetTe.ribs.front().outerOutline.end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  assert(std::abs(sheetRibMaximum->x -
      (ribs.front().chord - sheetTeParameters.trailingEdgeWidth +
       sheetTeParameters.trailingEdgeSlotDepth)) < 1.0e-9);

  const auto path = std::filesystem::temp_directory_path() / "designrc_test_rib.dxf";
  designrc::domain::exportRibDxf(ribs.front(), path, "Test rib");
  std::ifstream dxf{path};
  const std::string contents{std::istreambuf_iterator<char>{dxf}, {}};
  assert(contents.find("LWPOLYLINE") != std::string::npos);
  assert(contents.find("Test rib") != std::string::npos);
  dxf.close();
  std::filesystem::remove(path);

  const auto structuredPath = std::filesystem::temp_directory_path() / "designrc_structured_rib.dxf";
  designrc::domain::exportStructuredRibDxf(carbonStructured.ribs.front(), structuredPath,
                                           "Structured rib");
  std::ifstream structuredDxf{structuredPath};
  const std::string structuredContents{std::istreambuf_iterator<char>{structuredDxf}, {}};
  assert(structuredContents.find("RIB_HOLES") != std::string::npos);
  structuredDxf.close();
  std::filesystem::remove(structuredPath);

  const auto webPath = std::filesystem::temp_directory_path() / "designrc_shear_web.dxf";
  designrc::domain::exportShearWebDxf(structured.shearWebs.front(), webPath, "Shear web");
  std::ifstream webDxf{webPath};
  const std::string webContents{std::istreambuf_iterator<char>{webDxf}, {}};
  assert(webContents.find("SHEAR_WEB_OUTLINE") != std::string::npos);
  webDxf.close();
  std::filesystem::remove(webPath);

  const auto sheetPath = std::filesystem::temp_directory_path() / "designrc_sheet_te.dxf";
  designrc::domain::exportSheetStockDxf(sheetTe.sheetStockParts.front(), sheetPath,
                                        "Sheet TE stock");
  std::ifstream sheetDxf{sheetPath};
  const std::string sheetContents{std::istreambuf_iterator<char>{sheetDxf}, {}};
  assert(sheetContents.find("SHEET_TE_OUTLINE") != std::string::npos);
  assert(sheetContents.find("SHEET_TE_SLOTS") == std::string::npos);
  sheetDxf.close();
  std::filesystem::remove(sheetPath);
  const auto joinerPath = std::filesystem::temp_directory_path() / "designrc_wood_joiner.dxf";
  designrc::domain::exportWoodJoinerDxf(joined.joiners.back(), joinerPath, "Wood joiner");
  std::ifstream joinerDxf{joinerPath};
  const std::string joinerContents{std::istreambuf_iterator<char>{joinerDxf}, {}};
  assert(joinerContents.find("WOOD_JOINER_OUTLINE") != std::string::npos);
  joinerDxf.close();
  std::filesystem::remove(joinerPath);
  const auto splitRibPath = std::filesystem::temp_directory_path() / "designrc_split_rib.dxf";
  designrc::domain::exportStructuredRibDxf(joined.ribs.front(), splitRibPath, "R1");
  std::ifstream splitRibDxf{splitRibPath};
  const std::string splitRibContents{std::istreambuf_iterator<char>{splitRibDxf}, {}};
  std::size_t ribOutlineCount = 0;
  std::size_t position = 0;
  while ((position = splitRibContents.find("RIB_OUTLINE", position)) != std::string::npos) {
    ++ribOutlineCount;
    position += 11;
  }
  assert(ribOutlineCount == 2);
  assert(splitRibContents.find("\n1\nR1\n") != std::string::npos);
  splitRibDxf.close();
  std::filesystem::remove(splitRibPath);

  const auto anglePath = std::filesystem::temp_directory_path() / "designrc_dihedral_angle.dxf";
  designrc::domain::exportDihedralAngleDxf(6.0, anglePath, "Dihedral Angle 1");
  std::ifstream angleDxf{anglePath};
  const std::string angleContents{std::istreambuf_iterator<char>{angleDxf}, {}};
  assert(angleContents.find("DIHEDRAL_ANGLE_OUTLINE") != std::string::npos);
  assert(angleContents.find("38.100000") != std::string::npos);
  assert(angleContents.find("25.400000") != std::string::npos);
  assert(angleContents.find("Dihedral Angle 1") != std::string::npos);
  angleDxf.close();
  std::filesystem::remove(anglePath);
  return 0;
}
