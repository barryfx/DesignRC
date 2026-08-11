#include "domain/AirfoilProfile.h"
#include "domain/WingDesign.h"
#include "domain/DxfExporter.h"
#include "domain/WingStructure.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <string>
#include <stdexcept>
#include <thread>

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

  const auto twistRanges = designrc::domain::calculatePanelTwistRanges({4.0, 3.0, -2.0});
  assert(twistRanges.size() == 3);
  assert(std::abs(twistRanges[0].rootTwistDegrees) < 1.0e-9);
  assert(std::abs(twistRanges[0].tipTwistDegrees - 4.0) < 1.0e-9);
  assert(std::abs(twistRanges[1].rootTwistDegrees - 4.0) < 1.0e-9);
  assert(std::abs(twistRanges[1].tipTwistDegrees - 7.0) < 1.0e-9);
  assert(std::abs(twistRanges[2].rootTwistDegrees - 7.0) < 1.0e-9);
  assert(std::abs(twistRanges[2].tipTwistDegrees - 5.0) < 1.0e-9);

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
  parameters.rootTwistDegrees = twistRanges[1].rootTwistDegrees;
  parameters.tipTwistDegrees = twistRanges[1].tipTwistDegrees;
  const auto inheritedTwistRibs = designrc::domain::generateRibs(parameters, thick, thin);
  assert(std::abs(inheritedTwistRibs.front().twistDegrees - 4.0) < 1.0e-9);
  assert(std::abs(inheritedTwistRibs.back().twistDegrees - 7.0) < 1.0e-9);
  assert(std::abs(inheritedTwistRibs[3].twistDegrees - 5.5) < 1.0e-9);
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
  assert(std::any_of(structured.ribs.front().outlineSegments.begin(),
      structured.ribs.front().outlineSegments.end(),
      [](const auto& segment) { return segment.spline; }));
  assert(std::any_of(structured.ribs.front().outlineSegments.begin(),
      structured.ribs.front().outlineSegments.end(),
      [](const auto& segment) { return !segment.spline; }));
  bool foundSixtyPercentRearSpar = false;
  for (const auto& member : structured.members) {
    if (member.name != "Top 60% rear spar") continue;
    assert(std::abs(member.centers.front().x - 0.60 * ribs.front().chord) < 1.0e-9);
    assert(member.verticalLocation == 0);
    assert(member.cutsSheeting);
    foundSixtyPercentRearSpar = true;
  }
  assert(foundSixtyPercentRearSpar);
  designrc::domain::StructureParameters ribLighteningParameters;
  ribLighteningParameters.wiringHoles = true;
  ribLighteningParameters.wiringHoleStartRib = 3;
  ribLighteningParameters.wiringHoleEndRib = 5;
  ribLighteningParameters.wiringHoleChordLocationPercent = 48;
  ribLighteningParameters.wiringHoleWidth = 8.0;
  ribLighteningParameters.wiringHoleHeight = 5.0;
  ribLighteningParameters.ribLighteningHoles = true;
  ribLighteningParameters.ribLighteningStartRib = 3;
  ribLighteningParameters.ribLighteningStopRib = 5;
  ribLighteningParameters.ribLighteningMinimumWoodMargin = 3.0;
  ribLighteningParameters.ribLighteningMinimumHoleDistance = 5.0;
  auto ribLightenedWing = designrc::domain::applyWingStructure(
      ribs, ribLighteningParameters);
  std::vector<std::size_t> originalOpeningCounts;
  for (const auto& rib : ribLightenedWing.ribs)
    originalOpeningCounts.push_back(rib.internalCutouts.size());
  designrc::domain::addRibLighteningHoles(
      ribLightenedWing, ribLighteningParameters);
  std::vector<double> lighteningRadii;
  std::vector<double> chordwiseCoverage;
  for (std::size_t ribIndex = 0;
       ribIndex < ribLightenedWing.ribs.size(); ++ribIndex) {
    const auto& openings = ribLightenedWing.ribs[ribIndex].internalCutouts;
    if (ribIndex < 2 || ribIndex > 4) {
      assert(openings.size() == originalOpeningCounts[ribIndex]);
      continue;
    }
    assert(openings.size() > originalOpeningCounts[ribIndex]);
    double firstHoleEdge = std::numeric_limits<double>::max();
    double lastHoleEdge = std::numeric_limits<double>::lowest();
    for (std::size_t openingIndex = originalOpeningCounts[ribIndex];
         openingIndex < openings.size(); ++openingIndex) {
      const auto& opening = openings[openingIndex];
      designrc::domain::Point2 center{};
      for (const auto point : opening) {
        center.x += point.x;
        center.y += point.y;
      }
      center.x /= static_cast<double>(opening.size());
      center.y /= static_cast<double>(opening.size());
      lighteningRadii.push_back(std::hypot(
          opening.front().x - center.x, opening.front().y - center.y));
      for (const auto point : opening) {
        firstHoleEdge = std::min(firstHoleEdge, point.x);
        lastHoleEdge = std::max(lastHoleEdge, point.x);
      }
    }
    chordwiseCoverage.push_back(
        (lastHoleEdge - firstHoleEdge) /
        ribLightenedWing.ribs[ribIndex].rib.chord);
  }
  assert(lighteningRadii.size() >= 3);
  const auto [smallestLighteningRadius, largestLighteningRadius] =
      std::minmax_element(lighteningRadii.begin(), lighteningRadii.end());
  assert(*largestLighteningRadius - *smallestLighteningRadius > 0.25);
  assert(chordwiseCoverage.size() == 3);
  assert(std::all_of(
      chordwiseCoverage.begin(), chordwiseCoverage.end(),
      [](const double coverage) { return coverage > 0.50; }));
  const auto [smallestCoverage, largestCoverage] =
      std::minmax_element(
          chordwiseCoverage.begin(), chordwiseCoverage.end());
  assert(*largestCoverage - *smallestCoverage < 0.15);
  const auto logicalProcessors = std::thread::hardware_concurrency();
  const std::size_t expectedMaximumWorkers = logicalProcessors > 2
      ? static_cast<std::size_t>(logicalProcessors - 2) : 1;
  assert(designrc::domain::ribLighteningHoleWorkerCount(100) ==
         std::min<std::size_t>(100, expectedMaximumWorkers));
  assert(designrc::domain::ribLighteningHoleWorkerCount(100, 2) ==
         std::min<std::size_t>(2, expectedMaximumWorkers));
  designrc::domain::StructureParameters endpointSparParameters;
  endpointSparParameters.spars = {
      {20, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0, 60}};
  const auto endpointSparWing = designrc::domain::applyWingStructure(
      constantRibs, endpointSparParameters);
  const auto& endpointCenters = endpointSparWing.members.front().centers;
  assert(std::abs(endpointCenters.front().x - 40.0) < 1.0e-8);
  assert(std::abs(endpointCenters.back().x - 120.0) < 1.0e-8);
  for (std::size_t i = 0; i < endpointCenters.size(); ++i) {
    const double along = static_cast<double>(i) /
        static_cast<double>(endpointCenters.size() - 1);
    assert(std::abs(endpointCenters[i].x - (40.0 + 80.0 * along)) < 1.0e-8);
  }
  designrc::domain::StructureParameters multiSparParameters;
  multiSparParameters.spars = {
      {30, 0, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {45, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {65, 1, 1, 2, 5.0, 9.0, 6.0, 5.0, 6.0, 7.0, 1.5}};
  const auto multiSparWing = designrc::domain::applyWingStructure(
      ribs, multiSparParameters);
  assert(multiSparWing.members.size() == 3);
  assert(multiSparWing.members[0].name == "Spar 1");
  assert(multiSparWing.members[0].kind == designrc::domain::SpanMemberKind::Rectangular);
  assert(std::abs(multiSparWing.members[0].centers.front().x -
                  0.30 * ribs.front().chord) < 1.0e-9);
  assert(multiSparWing.members[1].kind == designrc::domain::SpanMemberKind::Tube);
  assert(multiSparWing.members[1].carbonFiber);
  assert(std::abs(multiSparWing.members[1].innerDiameter - 5.0) < 1.0e-9);
  assert(multiSparWing.members[2].kind == designrc::domain::SpanMemberKind::Rectangular);
  assert(multiSparWing.members[2].carbonFiber);
  assert(std::abs(multiSparWing.members[2].width - 7.0) < 1.0e-9);
  assert(std::abs(multiSparWing.members[2].height - 1.5) < 1.0e-9);
  for (const auto& rib : multiSparWing.ribs)
    assert(rib.booleanHoles.size() == 1);
  const auto topSparMember = std::find_if(structured.members.begin(), structured.members.end(),
      [](const auto& member) { return member.name == "Top spar"; });
  const auto bottomSparMember = std::find_if(structured.members.begin(), structured.members.end(),
      [](const auto& member) { return member.name == "Bottom spar"; });
  assert(topSparMember != structured.members.end());
  assert(bottomSparMember != structured.members.end());
  for (const auto& web : structured.shearWebs) {
    assert(web.name == "SW" + std::to_string(web.bayIndex));
    assert(web.outline.size() == 4);
    assert(web.stationCorners[2].y > web.stationCorners[1].y);
    assert(web.stationCorners[3].y > web.stationCorners[0].y);
    const std::size_t outer = web.bayIndex;
    const std::size_t inner = outer - 1;
    const double ribCenterBay = std::hypot(
        ribs[outer].spanPosition - ribs[inner].spanPosition,
        ribs[outer].dihedralHeight - ribs[inner].dihedralHeight);
    assert(web.outline[1].x > 0.0);
    assert(web.outline[1].x < ribCenterBay);
    assert(std::abs(web.stationCorners[0].y -
                    (bottomSparMember->centers[inner].y +
                     structureParameters.bottomSparHeight * 0.5)) < 1.0e-9);
    assert(std::abs(web.stationCorners[1].y -
                    (bottomSparMember->centers[outer].y +
                     structureParameters.bottomSparHeight * 0.5)) < 1.0e-9);
    assert(std::abs(web.stationCorners[2].y -
                    (topSparMember->centers[outer].y -
                     structureParameters.topSparHeight * 0.5)) < 1.0e-9);
    assert(std::abs(web.stationCorners[3].y -
                    (topSparMember->centers[inner].y -
                     structureParameters.topSparHeight * 0.5)) < 1.0e-9);
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

  auto solidLeSheetingParameters = sheetedSparParameters;
  solidLeSheetingParameters.leadingEdgeType = 2;
  solidLeSheetingParameters.leadingEdgeWidth = 4.7625;
  solidLeSheetingParameters.leadingEdgeHeight = 30.0;
  const auto solidLeSheeted = designrc::domain::applyWingStructure(
      ribs, solidLeSheetingParameters);
  const auto& solidLeOutline = solidLeSheeted.ribs.front().outerOutline;
  const auto solidLeFacePointCount = std::count_if(
      solidLeOutline.begin(), solidLeOutline.end(), [&](const auto& point) {
        return std::abs(point.x - solidLeSheetingParameters.leadingEdgeWidth) <
            1.0e-8;
      });
  // Only the recessed top and bottom endpoints should remain at the LE face.
  // Extra unrecessed endpoints become visible slivers when the rib is extruded.
  assert(solidLeFacePointCount == 2);

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
  const auto& leSheetStart = sheeted.sheeting[0].profiles.front().front();
  const auto& leCenter = sheeted.members[1].centers.front();
  assert(std::hypot(leSheetStart.x - leCenter.x,
                    leSheetStart.y - leCenter.y) <
         sheetingParameters.leadingEdgeTubeOd * 0.5);
  assert(sheeted.ribs.front().booleanHoles.empty());
  assert(!sheeted.ribs.front().partOutline.empty());
  assert(sheeted.ribs.front().holes.size() == 1);

  auto turbulatorSheetingParameters = sheetingParameters;
  turbulatorSheetingParameters.teTopSheet = false;
  turbulatorSheetingParameters.turbulators = true;
  turbulatorSheetingParameters.turbulatorCount = 2;
  turbulatorSheetingParameters.turbulatorWidth = 3.0;
  turbulatorSheetingParameters.turbulatorHeight = 2.0;
  const auto turbulatorSheeted = designrc::domain::applyWingStructure(
      ribs, turbulatorSheetingParameters);
  assert(turbulatorSheeted.sheeting.size() == 3);
  for (const auto& strip : turbulatorSheeted.sheeting)
    assert(strip.profiles.size() == 3);
  const auto xBounds = [](const std::vector<designrc::domain::Point2>& profile) {
    const auto [minimum, maximum] = std::minmax_element(
        profile.begin(), profile.end(),
        [](const auto& a, const auto& b) { return a.x < b.x; });
    return std::pair{minimum->x, maximum->x};
  };
  designrc::domain::StructureParameters newSparSheetingParameters;
  newSparSheetingParameters.spars = {
      {35, 0, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {40, 1, 0, 0, 5.0, 10.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  newSparSheetingParameters.leTopSheet = true;
  newSparSheetingParameters.teTopSheet = true;
  newSparSheetingParameters.leBottomSheet = true;
  newSparSheetingParameters.teBottomSheet = true;
  const auto newSparSheeted = designrc::domain::applyWingStructure(
      ribs, newSparSheetingParameters);
  const auto sheetBounds = [&](const std::string& name) {
    const auto sheet = std::find_if(newSparSheeted.sheeting.begin(),
        newSparSheeted.sheeting.end(), [&](const auto& part) {
          return part.name == name;
        });
    assert(sheet != newSparSheeted.sheeting.end());
    return xBounds(sheet->profiles.front());
  };
  const double topCenter = 0.35 * ribs.front().chord;
  const double bottomCenter = 0.40 * ribs.front().chord;
  assert(std::abs(sheetBounds("Front top sheeting").second -
                  (topCenter - 4.0)) < 1.0e-8);
  assert(std::abs(sheetBounds("Rear top sheeting").first -
                  (topCenter + 4.0)) < 1.0e-8);
  assert(std::abs(sheetBounds("Front bottom sheeting").second -
                  (bottomCenter - 5.0)) < 1.0e-8);
  assert(std::abs(sheetBounds("Rear bottom sheeting").first -
                  (bottomCenter + 5.0)) < 1.0e-8);

  auto manualFrontSheetingParameters = newSparSheetingParameters;
  manualFrontSheetingParameters.leTopSheetUpToSpar = false;
  manualFrontSheetingParameters.leBottomSheetUpToSpar = false;
  manualFrontSheetingParameters.leTopSheetStopChordPercent = 35.0;
  manualFrontSheetingParameters.leBottomSheetStopChordPercent = 40.0;
  const auto manualFrontSheeted = designrc::domain::applyWingStructure(
      ribs, manualFrontSheetingParameters);
  const auto manualBounds = [&](const std::string& name) {
    const auto sheet = std::find_if(manualFrontSheeted.sheeting.begin(),
        manualFrontSheeted.sheeting.end(), [&](const auto& part) {
          return part.name == name;
        });
    assert(sheet != manualFrontSheeted.sheeting.end());
    return xBounds(sheet->profiles.front());
  };
  assert(std::abs(manualBounds("Front top sheeting").second -
                  (topCenter + 4.0)) < 1.0e-8);
  assert(std::abs(manualBounds("Front bottom sheeting").second -
                  (bottomCenter + 5.0)) < 1.0e-8);
  assert(std::abs(manualFrontSheeted.members[0].centers.front().y -
                  (newSparSheeted.members[0].centers.front().y -
                   manualFrontSheetingParameters.leTopSheetThickness)) < 1.0e-8);
  assert(std::abs(manualFrontSheeted.members[1].centers.front().y -
                  (newSparSheeted.members[1].centers.front().y +
                   manualFrontSheetingParameters.leBottomSheetThickness)) < 1.0e-8);

  manualFrontSheetingParameters.leTopSheetStopChordPercent = 50.0;
  const auto buttedFrontAndRear = designrc::domain::applyWingStructure(
      ribs, manualFrontSheetingParameters);
  const auto buttedRear = std::find_if(buttedFrontAndRear.sheeting.begin(),
      buttedFrontAndRear.sheeting.end(), [](const auto& part) {
        return part.name == "Rear top sheeting";
      });
  assert(buttedRear != buttedFrontAndRear.sheeting.end());
  assert(std::abs(xBounds(buttedRear->profiles.front()).first -
                  0.50 * ribs.front().chord) < 1.0e-8);

  designrc::domain::StructureParameters teSheetingParameters;
  teSheetingParameters.carbonSpar = 1;
  teSheetingParameters.teTopSheet = true;
  teSheetingParameters.teTopSheetThickness = 1.0;
  teSheetingParameters.teTopSheetStopRib = 3;
  teSheetingParameters.topTeSheeting = true;
  teSheetingParameters.topTeSheetingWidth = 30.0;
  teSheetingParameters.topTeSheetingThickness = 1.5875;
  teSheetingParameters.topTeSheetingTaper = true;
  teSheetingParameters.topTeSheetingTaperStartLocationPercent = 50.0;
  const auto teSheeted = designrc::domain::applyWingStructure(
      ribs, teSheetingParameters);
  const auto topTeSheet = std::find_if(teSheeted.sheeting.begin(),
      teSheeted.sheeting.end(), [](const auto& part) {
        return part.name == "Top TE sheeting";
      });
  const auto rearTopSheet = std::find_if(teSheeted.sheeting.begin(),
      teSheeted.sheeting.end(), [](const auto& part) {
        return part.name == "Rear top sheeting";
      });
  assert(topTeSheet != teSheeted.sheeting.end());
  assert(rearTopSheet != teSheeted.sheeting.end());
  const auto& topTeProfile = topTeSheet->profiles.front();
  assert(topTeProfile.size() == 96);
  assert(std::abs(xBounds(topTeProfile).first -
                  (ribs.front().chord - 30.0)) < 1.0e-8);
  assert(std::abs(xBounds(rearTopSheet->profiles.front()).second -
                  (ribs.front().chord - 30.0)) < 1.0e-8);
  assert(std::abs((topTeProfile.front().y - topTeProfile.back().y) -
                  1.5875) < 1.0e-8);
  assert(std::abs(xBounds(topTeSheet->profiles.back()).first -
                  (ribs.back().chord - 30.0)) < 1.0e-8);
  assert(std::hypot(topTeProfile[47].x - topTeProfile[48].x,
                    topTeProfile[47].y - topTeProfile[48].y) < 1.0e-8);

  const auto hasSkinnyTrailingRibSliver = [](
      const designrc::domain::StructuredWing& wing,
      const double width, const double minimumRetainedDepth) {
    for (const auto& structuredRib : wing.ribs) {
      const double startX = structuredRib.rib.chord - width;
      const auto& outline = structuredRib.outerOutline;
      for (const auto point : outline) {
        if (point.x <= startX + 1.0e-6) continue;
        double minimumY = std::numeric_limits<double>::max();
        double maximumY = std::numeric_limits<double>::lowest();
        std::size_t matches = 0;
        for (const auto candidate : outline) {
          if (std::abs(candidate.x - point.x) > 1.0e-8) continue;
          minimumY = std::min(minimumY, candidate.y);
          maximumY = std::max(maximumY, candidate.y);
          ++matches;
        }
        const double depth = maximumY - minimumY;
        if (matches >= 2 && depth > 1.0e-6 &&
            depth < minimumRetainedDepth - 1.0e-6)
          return true;
      }
    }
    return false;
  };
  const auto ribsTerminateBeforeTrailingEdge = [](
      const designrc::domain::StructuredWing& wing) {
    return std::all_of(wing.ribs.begin(), wing.ribs.end(),
        [](const auto& structuredRib) {
          const double maximumX = std::max_element(
              structuredRib.outerOutline.begin(),
              structuredRib.outerOutline.end(),
              [](const auto first, const auto second) {
                return first.x < second.x;
              })->x;
          return maximumX < structuredRib.rib.chord - 1.0e-5;
        });
  };
  auto untaperedTopOnlyParameters = teSheetingParameters;
  untaperedTopOnlyParameters.teTopSheet = false;
  untaperedTopOnlyParameters.topTeSheetingTaper = false;
  const auto untaperedTopOnly = designrc::domain::applyWingStructure(
      ribs, untaperedTopOnlyParameters);
  assert(!hasSkinnyTrailingRibSliver(
      untaperedTopOnly, untaperedTopOnlyParameters.topTeSheetingWidth,
      0.25));
  assert(ribsTerminateBeforeTrailingEdge(untaperedTopOnly));
  auto untaperedBottomOnlyParameters = untaperedTopOnlyParameters;
  untaperedBottomOnlyParameters.topTeSheeting = false;
  untaperedBottomOnlyParameters.bottomTeSheeting = true;
  untaperedBottomOnlyParameters.bottomTeSheetingWidth = 30.0;
  untaperedBottomOnlyParameters.bottomTeSheetingThickness = 1.5875;
  untaperedBottomOnlyParameters.bottomTeSheetingTaper = false;
  const auto untaperedBottomOnly = designrc::domain::applyWingStructure(
      ribs, untaperedBottomOnlyParameters);
  assert(!hasSkinnyTrailingRibSliver(
      untaperedBottomOnly, untaperedBottomOnlyParameters.bottomTeSheetingWidth,
      0.25));
  assert(ribsTerminateBeforeTrailingEdge(untaperedBottomOnly));
  designrc::domain::WingParameters savedTeSheetingPanel;
  savedTeSheetingPanel.halfSpan = 698.5;
  savedTeSheetingPanel.rootChord = 254.0;
  savedTeSheetingPanel.tipChord = 152.4;
  savedTeSheetingPanel.ribCount = 11;
  const auto savedTeSheetingRibs = designrc::domain::generateRibs(
      savedTeSheetingPanel, AirfoilProfile::nacaSymmetric(0.15),
      AirfoilProfile::nacaSymmetric(0.10));
  auto savedTopOnlyParameters = untaperedTopOnlyParameters;
  savedTopOnlyParameters.topTeSheetingWidth = 25.4;
  savedTopOnlyParameters.teTopSheet = true;
  savedTopOnlyParameters.teBottomSheet = true;
  savedTopOnlyParameters.teTopSheetThickness = 1.5875;
  savedTopOnlyParameters.teBottomSheetThickness = 1.5875;
  savedTopOnlyParameters.teTopSheetStopRib = 2;
  savedTopOnlyParameters.teBottomSheetStopRib = 2;
  const auto savedTopOnly = designrc::domain::applyWingStructure(
      savedTeSheetingRibs, savedTopOnlyParameters);
  assert(!hasSkinnyTrailingRibSliver(
      savedTopOnly, savedTopOnlyParameters.topTeSheetingWidth,
      0.25));
  assert(ribsTerminateBeforeTrailingEdge(savedTopOnly));
  auto savedBottomOnlyParameters = savedTopOnlyParameters;
  savedBottomOnlyParameters.topTeSheeting = false;
  savedBottomOnlyParameters.bottomTeSheeting = true;
  const auto savedBottomOnly = designrc::domain::applyWingStructure(
      savedTeSheetingRibs, savedBottomOnlyParameters);
  assert(!hasSkinnyTrailingRibSliver(
      savedBottomOnly, savedBottomOnlyParameters.bottomTeSheetingWidth,
      0.25));
  assert(ribsTerminateBeforeTrailingEdge(savedBottomOnly));

  auto bothTeSheetingParameters = teSheetingParameters;
  bothTeSheetingParameters.teTopSheet = false;
  bothTeSheetingParameters.topTeSheetingTaper = false;
  bothTeSheetingParameters.bottomTeSheeting = true;
  bothTeSheetingParameters.bottomTeSheetingWidth = 28.0;
  bothTeSheetingParameters.bottomTeSheetingThickness = 1.5875;
  bothTeSheetingParameters.bottomTeSheetingTaper = false;
  bothTeSheetingParameters.bottomTeSheetingTaperStartLocationPercent = 50.0;
  const auto bothTeSheeted = designrc::domain::applyWingStructure(
      ribs, bothTeSheetingParameters);
  assert(!hasSkinnyTrailingRibSliver(
      bothTeSheeted, std::max(
          bothTeSheetingParameters.topTeSheetingWidth,
          bothTeSheetingParameters.bottomTeSheetingWidth), 0.25));
  assert(ribsTerminateBeforeTrailingEdge(bothTeSheeted));
  for (const auto& name : {"Top TE sheeting", "Bottom TE sheeting"}) {
    const auto sheet = std::find_if(bothTeSheeted.sheeting.begin(),
        bothTeSheeted.sheeting.end(), [&](const auto& part) {
          return part.name == name;
        });
    assert(sheet != bothTeSheeted.sheeting.end());
    const auto& profile = sheet->profiles.front();
    assert(std::hypot(profile[47].x - profile[48].x,
                      profile[47].y - profile[48].y) < 1.0e-8);
  }
  auto conflictingTeStock = teSheetingParameters;
  conflictingTeStock.trailingEdgeType = 2;
  bool rejectedConflictingTeStock = false;
  try {
    (void)designrc::domain::applyWingStructure(ribs, conflictingTeStock);
  } catch (const std::invalid_argument&) {
    rejectedConflictingTeStock = true;
  }
  assert(rejectedConflictingTeStock);
  for (std::size_t i = 0; i < 2; ++i) {
    const auto left = xBounds(turbulatorSheeted.sheeting[i].profiles.front());
    const auto right = xBounds(turbulatorSheeted.sheeting[i + 1].profiles.front());
    assert(std::abs((right.first - left.second) -
        turbulatorSheetingParameters.turbulatorWidth) < 1.0e-8);
  }

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
  edgeParameters.leadingEdgeType = 2;
  edgeParameters.leadingEdgeWidth = 8.0;
  edgeParameters.leadingEdgeHeight = 50.0;
  edgeParameters.trailingEdgeType = 2;
  edgeParameters.trailingEdgeWidth = 18.0;
  edgeParameters.trailingEdgeHeight = 50.0;
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

  designrc::domain::StructureParameters removedShapedStock;
  removedShapedStock.leadingEdgeType = 1;
  removedShapedStock.trailingEdgeType = 1;
  const auto withoutRemovedStock = designrc::domain::applyWingStructure(
      ribs, removedShapedStock);
  assert(withoutRemovedStock.profiledMembers.empty());
  assert(withoutRemovedStock.sheetStockParts.empty());

  designrc::domain::StructureParameters insufficientLeHeight;
  insufficientLeHeight.leadingEdgeType = 2;
  insufficientLeHeight.leadingEdgeWidth = 1.0;
  insufficientLeHeight.leadingEdgeHeight = 0.01;
  bool rejectedInsufficientLeHeight = false;
  double reportedLeCutHeight = 0.0;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(
        constantRibs, insufficientLeHeight));
  } catch (const designrc::domain::EdgeHeightError& error) {
    rejectedInsufficientLeHeight = std::string{error.what()}.find(
        "not smaller than the specified LE Height") != std::string::npos;
    assert(error.edgeName() == "LE");
    reportedLeCutHeight = error.cutHeightMm();
  }
  assert(rejectedInsufficientLeHeight);
  assert(reportedLeCutHeight > insufficientLeHeight.leadingEdgeHeight);

  designrc::domain::StructureParameters insufficientTeHeight;
  insufficientTeHeight.trailingEdgeType = 2;
  insufficientTeHeight.trailingEdgeWidth = 1.0;
  insufficientTeHeight.trailingEdgeHeight = 0.01;
  bool rejectedInsufficientTeHeight = false;
  double reportedTeCutHeight = 0.0;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(
        constantRibs, insufficientTeHeight));
  } catch (const designrc::domain::EdgeHeightError& error) {
    rejectedInsufficientTeHeight = std::string{error.what()}.find(
        "not smaller than the specified TE Height") != std::string::npos;
    assert(error.edgeName() == "TE");
    reportedTeCutHeight = error.cutHeightMm();
  }
  assert(rejectedInsufficientTeHeight);
  assert(reportedTeCutHeight > insufficientTeHeight.trailingEdgeHeight);

  designrc::domain::StructureParameters leadingTubeParameters;
  leadingTubeParameters.leadingEdgeType = 3;
  const auto leadingTube = designrc::domain::applyWingStructure(ribs, leadingTubeParameters);
  assert(leadingTube.ribs.front().holes.empty());
  assert(leadingTube.ribs.front().booleanHoles.empty());
  assert(leadingTube.members.size() == 1);
  assert(leadingTube.members.front().kind == designrc::domain::SpanMemberKind::Tube);
  assert(leadingTube.members.front().centers.front().x -
         leadingTubeParameters.leadingEdgeTubeOd * 0.5 < -0.09);
  assert(!leadingTube.ribs.front().partOutline.empty());
  const auto leadingTubeDrawing = designrc::domain::makeStructuredRibPartDrawing(
      leadingTube.ribs.front(), "Leading Tube Rib");
  assert(std::none_of(leadingTubeDrawing.paths.begin(),
      leadingTubeDrawing.paths.end(),
      [](const auto& path) { return path.layer == "RIB_HOLES"; }));
  assert(std::count_if(leadingTubeDrawing.paths.begin(),
      leadingTubeDrawing.paths.end(),
      [](const auto& path) {
        return path.layer == "RIB_OUTLINE" && path.spline;
      }) >= 3);

  designrc::domain::StructureParameters controlParameters;
  controlParameters.trailingEdgeType = 2;
  controlParameters.trailingEdgeWidth = 20.0;
  controlParameters.trailingEdgeHeight = 50.0;
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
  assert(boundedControl.controlSurfaces.front().stopRibIndex == ribs.size() - 1);
  assert(boundedControl.controlSurfaces.front().cutStopRib);
  assert(boundedControl.controlSurfaces.front().extendThroughStopRib);
  const auto tipMaximum = std::max_element(
      boundedControl.ribs.back().outerOutline.begin(),
      boundedControl.ribs.back().outerOutline.end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  assert(std::abs(tipMaximum->x -
      (ribs.back().chord - boundedControlParameters.aileronWidth -
       boundedControlParameters.aileronHingePostWidth -
       boundedControlParameters.controlSurfaceGap)) < 1.0e-9);

  auto sharedControlParameters = controlParameters;
  sharedControlParameters.flaps = true;
  sharedControlParameters.flapWidth = sharedControlParameters.aileronWidth;
  sharedControlParameters.flapStartRib = 2;
  sharedControlParameters.flapStopRib = 4;
  sharedControlParameters.aileronStartRib = 4;
  sharedControlParameters.aileronStopRib = static_cast<int>(ribs.size());
  const auto sharedControl = designrc::domain::applyWingStructure(
      ribs, sharedControlParameters);
  assert(sharedControl.controlSurfaces.size() == 2);
  const auto& flap = sharedControl.controlSurfaces.front();
  const auto& aileron = sharedControl.controlSurfaces.back();
  assert(flap.stopRibIndex == aileron.startRibIndex);
  assert(flap.cutStopRib && flap.extendThroughStopRib);
  assert(aileron.cutStartRib && aileron.cutStopRib);
  const auto sharedRibMaximum = std::max_element(
      sharedControl.ribs[flap.stopRibIndex].outerOutline.begin(),
      sharedControl.ribs[flap.stopRibIndex].outerOutline.end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  assert(sharedRibMaximum->x <
      ribs[flap.stopRibIndex].chord - sharedControlParameters.flapWidth);

  auto unequalSharedWidths = sharedControlParameters;
  unequalSharedWidths.flapWidth += 3.0;
  bool rejectedSharedWidths = false;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(
        ribs, unequalSharedWidths));
  } catch (const std::invalid_argument& error) {
    rejectedSharedWidths = std::string{error.what()}.find(
        "Width and Aileron Width must match") != std::string::npos;
  }
  assert(rejectedSharedWidths);

  auto invalidControlOrder = sharedControlParameters;
  invalidControlOrder.aileronStartRib = invalidControlOrder.flapStopRib - 1;
  bool rejectedControlOrder = false;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(
        ribs, invalidControlOrder));
  } catch (const std::invalid_argument& error) {
    rejectedControlOrder = std::string{error.what()}.find(
        "Aileron Start Rib cannot be less") != std::string::npos;
  }
  assert(rejectedControlOrder);

  designrc::domain::StructureParameters sheetTeParameters;
  sheetTeParameters.ribThickness = parameters.ribThickness;
  sheetTeParameters.trailingEdgeType = 2;
  sheetTeParameters.trailingEdgeWidth = 20.0;
  sheetTeParameters.trailingEdgeHeight = 50.0;
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
  auto slottedSheetingParameters = sheetTeParameters;
  slottedSheetingParameters.teTopSheet = true;
  slottedSheetingParameters.teTopSheetStopRib = 2;
  const auto slottedSheeting = designrc::domain::applyWingStructure(
      ribs, slottedSheetingParameters);
  assert(slottedSheeting.sheeting.size() == 1);
  const double teInnerFace = ribs.front().chord -
      slottedSheetingParameters.trailingEdgeWidth;
  const auto sheetingMaximum = std::max_element(
      slottedSheeting.sheeting.front().profiles.front().begin(),
      slottedSheeting.sheeting.front().profiles.front().end(),
      [](const auto& a, const auto& b) { return a.x < b.x; });
  assert(std::abs(sheetingMaximum->x - teInnerFace) < 1.0e-9);

  const auto path = std::filesystem::temp_directory_path() / "designrc_test_rib.dxf";
  designrc::domain::exportRibDxf(ribs.front(), path, "Test rib");
  std::ifstream dxf{path};
  const std::string contents{std::istreambuf_iterator<char>{dxf}, {}};
  assert(contents.find("$HANDSEED") != std::string::npos);
  assert(contents.find("FFFF") != std::string::npos);
  assert(contents.find("SECTION\n  2\nCLASSES") != std::string::npos);
  assert(contents.find("SECTION\n  2\nTABLES") != std::string::npos);
  assert(contents.find("TABLE\n  2\nBLOCK_RECORD") != std::string::npos);
  assert(contents.find("BLOCK_RECORD") != std::string::npos);
  assert(contents.find("SECTION\n  2\nBLOCKS") != std::string::npos);
  assert(contents.find("*Model_Space") != std::string::npos);
  assert(contents.find("SECTION\n  2\nOBJECTS") != std::string::npos);
  assert(contents.find("DICTIONARY") != std::string::npos);
  assert(contents.find("SPLINE") != std::string::npos);
  assert(contents.find("0\nSPLINE\n5\n") != std::string::npos);
  assert(contents.find("330\n17\n100\nAcDbEntity\n8\nRIB_OUTLINE") !=
         std::string::npos);
  assert(contents.find("100\nAcDbEntity\n8\nRIB_OUTLINE\n100\nAcDbSpline") !=
         std::string::npos);
  assert(contents.find("210\n0.0\n220\n0.0\n230\n1.0") != std::string::npos);
  assert(contents.find("\nTEXT\n") == std::string::npos);
  assert(contents.find("999\nPart label: Test rib") != std::string::npos);
  assert(contents.find(
      "330\n17\n100\nAcDbEntity\n8\nANNOTATION\n100\nAcDbLine") !=
      std::string::npos);
  dxf.close();
  std::filesystem::remove(path);

  const auto structuredPath = std::filesystem::temp_directory_path() / "designrc_structured_rib.dxf";
  designrc::domain::exportStructuredRibDxf(carbonStructured.ribs.front(), structuredPath,
                                           "Structured rib");
  std::ifstream structuredDxf{structuredPath};
  const std::string structuredContents{std::istreambuf_iterator<char>{structuredDxf}, {}};
  assert(structuredContents.find("RIB_HOLES") != std::string::npos);
  assert(structuredContents.find("SPLINE") != std::string::npos);
  assert(structuredContents.find("100\nAcDbPolyline") != std::string::npos);
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
  assert(splitRibContents.find("SPLINE") != std::string::npos);
  assert(splitRibContents.find("999\nPart label: R1") != std::string::npos);
  splitRibDxf.close();
  std::filesystem::remove(splitRibPath);

  const auto anglePath = std::filesystem::temp_directory_path() / "designrc_dihedral_angle.dxf";
  designrc::domain::exportDihedralAngleDxf(6.0, anglePath, "Dihedral Angle 1");
  std::ifstream angleDxf{anglePath};
  const std::string angleContents{std::istreambuf_iterator<char>{angleDxf}, {}};
  assert(angleContents.find("DIHEDRAL_ANGLE_OUTLINE") != std::string::npos);
  assert(angleContents.find("38.100000") != std::string::npos);
  assert(angleContents.find("25.400000") != std::string::npos);
  assert(angleContents.find("999\nPart label: Dihedral Angle 1") !=
         std::string::npos);
  assert(angleContents.find("TABLE\n  2\nLAYER") != std::string::npos);
  assert(angleContents.find("  2\nDIHEDRAL_ANGLE_OUTLINE\n 70\n0\n 62\n7") !=
         std::string::npos);
  assert(angleContents.find("0\nLWPOLYLINE\n5\n") != std::string::npos);
  assert(angleContents.find("330\n17\n100\nAcDbEntity") != std::string::npos);
  angleDxf.close();
  std::filesystem::remove(anglePath);

  const auto svgDirectory = std::filesystem::temp_directory_path();
  const auto ribSvgPath = svgDirectory / "designrc_test_rib.svg";
  designrc::domain::exportRibSvg(ribs.front(), ribSvgPath, "Test & rib");
  std::ifstream ribSvg{ribSvgPath};
  const std::string ribSvgContents{std::istreambuf_iterator<char>{ribSvg}, {}};
  assert(ribSvgContents.find("<svg") != std::string::npos);
  assert(ribSvgContents.find("mm\"") != std::string::npos);
  assert(ribSvgContents.find("<path") != std::string::npos);
  assert(ribSvgContents.find("Test &amp; rib") != std::string::npos);
  ribSvg.close();
  std::filesystem::remove(ribSvgPath);

  const auto structuredSvgPath = svgDirectory / "designrc_structured_rib.svg";
  designrc::domain::exportStructuredRibSvg(
      carbonStructured.ribs.front(), structuredSvgPath, "Structured rib");
  std::ifstream structuredSvg{structuredSvgPath};
  const std::string structuredSvgContents{
      std::istreambuf_iterator<char>{structuredSvg}, {}};
  assert(structuredSvgContents.find("viewBox=") != std::string::npos);
  assert(structuredSvgContents.find("Structured rib") != std::string::npos);
  assert(structuredSvgContents.find("<path") != std::string::npos);
  structuredSvg.close();
  std::filesystem::remove(structuredSvgPath);

  auto unevenSplineRib = carbonStructured.ribs.front();
  unevenSplineRib.partOutline = {
      {0.0, 0.0}, {100.0, 0.0}, {100.001, 0.001},
      {100.002, 1.0}, {0.0, 1.0}};
  unevenSplineRib.partOutlineSegments = {
      {{{0.0, 0.0}, {100.0, 0.0}, {100.001, 0.001},
        {100.002, 1.0}}, true},
      {{{100.002, 1.0}, {0.0, 1.0}, {0.0, 0.0}}, false}};
  unevenSplineRib.holes.clear();
  unevenSplineRib.booleanCutouts.clear();
  unevenSplineRib.booleanHoles.clear();
  unevenSplineRib.positiveHalfBooleanHoles.clear();
  unevenSplineRib.internalCutouts.clear();
  unevenSplineRib.ribSplitCutouts.clear();
  const auto unevenSplineSvgPath =
      svgDirectory / "designrc_uneven_spline_rib.svg";
  designrc::domain::exportStructuredRibSvg(
      unevenSplineRib, unevenSplineSvgPath, "Uneven spline rib");
  std::ifstream unevenSplineSvg{unevenSplineSvgPath};
  const std::string unevenSplineSvgContents{
      std::istreambuf_iterator<char>{unevenSplineSvg}, {}};
  // The old equal-spacing conversion placed this short segment's control
  // point at x=118.666833, producing the reported near-circular loop.
  assert(unevenSplineSvgContents.find("118.666833") == std::string::npos);
  assert(unevenSplineSvgContents.find("<path") != std::string::npos);
  unevenSplineSvg.close();
  std::filesystem::remove(unevenSplineSvgPath);

  const auto webSvgPath = svgDirectory / "designrc_shear_web.svg";
  designrc::domain::exportShearWebSvg(
      structured.shearWebs.front(), webSvgPath, "Shear web");
  const auto sheetSvgPath = svgDirectory / "designrc_sheet_te.svg";
  designrc::domain::exportSheetStockSvg(
      sheetTe.sheetStockParts.front(), sheetSvgPath, "Sheet TE stock");
  const auto joinerSvgPath = svgDirectory / "designrc_wood_joiner.svg";
  designrc::domain::exportWoodJoinerSvg(
      joined.joiners.back(), joinerSvgPath, "Wood joiner");
  const auto angleSvgPath = svgDirectory / "designrc_dihedral_angle.svg";
  designrc::domain::exportDihedralAngleSvg(
      6.0, angleSvgPath, "Dihedral Angle 1");
  for (const auto& svgPath : {webSvgPath, sheetSvgPath, joinerSvgPath, angleSvgPath}) {
    assert(std::filesystem::file_size(svgPath) > 100);
    std::filesystem::remove(svgPath);
  }
  const std::vector<designrc::domain::PartDrawing> compositeParts{
      designrc::domain::makeStructuredRibPartDrawing(
          carbonStructured.ribs.front(), "Composite Rib"),
      designrc::domain::makeShearWebPartDrawing(
          structured.shearWebs.front(), "Composite Web"),
      designrc::domain::makeDihedralAnglePartDrawing(
          6.0, "Composite Angle")};
  const auto arrangedParts = designrc::domain::arrangePartDrawings(compositeParts);
  assert(arrangedParts.size() == compositeParts.size());
  const auto teDrawing = designrc::domain::makeSheetStockPartDrawing(
      sheetTe.sheetStockParts.front(), "Rotated TE");
  const auto rotatedTe = designrc::domain::arrangePartDrawings({teDrawing}).front();
  const auto pathBounds = [](const designrc::domain::PartDrawing& drawing) {
    double minimumX = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double minimumY = std::numeric_limits<double>::max();
    double maximumY = std::numeric_limits<double>::lowest();
    for (const auto& drawingPath : drawing.paths)
      for (const auto point : drawingPath.points) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
      }
    return std::array{minimumX, maximumX, minimumY, maximumY};
  };
  const auto originalTeBounds = pathBounds(teDrawing);
  const auto rotatedTeBounds = pathBounds(rotatedTe);
  assert(std::abs((rotatedTeBounds[1] - rotatedTeBounds[0]) -
                  (originalTeBounds[3] - originalTeBounds[2])) < 1.0e-8);
  assert(std::abs((rotatedTeBounds[3] - rotatedTeBounds[2]) -
                  (originalTeBounds[1] - originalTeBounds[0])) < 1.0e-8);
  const char* preservedDxfPath = std::getenv("DESIGNRC_DXF_TEST_OUTPUT");
  const auto compositeDxfPath = preservedDxfPath
      ? std::filesystem::path{preservedDxfPath}
      : svgDirectory / "designrc_all_parts.dxf";
  designrc::domain::exportPartsDxf(compositeParts, compositeDxfPath);
  std::ifstream compositeDxf{compositeDxfPath};
  const std::string compositeDxfContents{
      std::istreambuf_iterator<char>{compositeDxf}, {}};
  assert(compositeDxfContents.find("Composite Rib") != std::string::npos);
  assert(compositeDxfContents.find("Composite Web") != std::string::npos);
  assert(compositeDxfContents.find("Composite Angle") != std::string::npos);
  assert(compositeDxfContents.find("100\nAcDbPolyline") != std::string::npos);
  assert(compositeDxfContents.find("100\nAcDbLine") != std::string::npos);
  assert(compositeDxfContents.find("100\nAcDbSpline") != std::string::npos);
  assert(compositeDxfContents.find("\nTEXT\n") == std::string::npos);
  assert(compositeDxfContents.find("999\nPart label: Composite Rib") !=
         std::string::npos);
  compositeDxf.close();
  if (!preservedDxfPath) std::filesystem::remove(compositeDxfPath);
  const auto compositeSvgPath = svgDirectory / "designrc_all_parts.svg";
  designrc::domain::exportPartsSvg(compositeParts, compositeSvgPath);
  std::ifstream compositeSvg{compositeSvgPath};
  const std::string compositeSvgContents{
      std::istreambuf_iterator<char>{compositeSvg}, {}};
  assert(compositeSvgContents.find("Composite Rib") != std::string::npos);
  assert(compositeSvgContents.find("Composite Web") != std::string::npos);
  assert(compositeSvgContents.find("Composite Angle") != std::string::npos);
  compositeSvg.close();
  std::filesystem::remove(compositeSvgPath);

  designrc::domain::StructureParameters spoilerParameters;
  spoilerParameters.spoilers = true;
  spoilerParameters.spoilerStartRib = 3;
  spoilerParameters.spoilerEndRib = 7;
  spoilerParameters.spoilerChordLocationPercent = 35;
  spoilerParameters.spoilerWidth = 25.4;
  spoilerParameters.spoilerThickness = 3.0;
  spoilerParameters.spoilerFrameRailWidth = 6.0;
  spoilerParameters.spoilerSupportRailHeight = 3.0;
  const auto spoilerWing = designrc::domain::applyWingStructure(ribs, spoilerParameters);
  assert(spoilerWing.spoilers.size() == 1);
  assert(spoilerWing.spoilers.front().spoilerProfiles.size() == 5);
  assert(spoilerWing.spoilers.front().supportProfiles.size() == 2);

  auto legacyTopSparSpoilerParameters = spoilerParameters;
  legacyTopSparSpoilerParameters.spoilerChordLocationPercent = 10;
  legacyTopSparSpoilerParameters.topSpar = true;
  legacyTopSparSpoilerParameters.topSparWidth = 10.0;
  const auto legacyTopSparSpoiler = designrc::domain::applyWingStructure(
      ribs, legacyTopSparSpoilerParameters);
  for (std::size_t local = 0;
       local < legacyTopSparSpoiler.spoilers.front().forwardRailProfiles.size();
       ++local) {
    const std::size_t ribIndex =
        legacyTopSparSpoiler.spoilers.front().startRibIndex + local;
    const double expectedAftFace = 0.25 * ribs[ribIndex].chord + 5.01;
    assert(std::abs(
        legacyTopSparSpoiler.spoilers.front().forwardRailProfiles[local][0].x -
        expectedAftFace) < 1.0e-8);
  }

  auto immediatelyBehindSparParameters = legacyTopSparSpoilerParameters;
  immediatelyBehindSparParameters.spoilerChordLocationPercent = 70;
  immediatelyBehindSparParameters.spoilerImmediatelyBehindSpar = true;
  const auto immediatelyBehindSpar = designrc::domain::applyWingStructure(
      ribs, immediatelyBehindSparParameters);
  for (std::size_t local = 0;
       local < immediatelyBehindSpar.spoilers.front().forwardRailProfiles.size();
       ++local) {
    const std::size_t ribIndex =
        immediatelyBehindSpar.spoilers.front().startRibIndex + local;
    const double expectedAftFace = 0.25 * ribs[ribIndex].chord + 5.01;
    assert(std::abs(
        immediatelyBehindSpar.spoilers.front().forwardRailProfiles[local][0].x -
        expectedAftFace) < 1.0e-8);
  }

  auto configurableTopSparSpoilerParameters = spoilerParameters;
  configurableTopSparSpoilerParameters.spoilerChordLocationPercent = 10;
  configurableTopSparSpoilerParameters.spars = {
      {45, 0, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  const auto configurableTopSparSpoiler = designrc::domain::applyWingStructure(
      ribs, configurableTopSparSpoilerParameters);
  for (std::size_t local = 0;
       local < configurableTopSparSpoiler.spoilers.front().forwardRailProfiles.size();
       ++local) {
    const std::size_t ribIndex =
        configurableTopSparSpoiler.spoilers.front().startRibIndex + local;
    const double expectedAftFace = 0.45 * ribs[ribIndex].chord + 4.51;
    assert(std::abs(
        configurableTopSparSpoiler.spoilers.front().forwardRailProfiles[local][0].x -
        expectedAftFace) < 1.0e-8);
  }
  auto decimalTopSparSpoilerParameters = spoilerParameters;
  decimalTopSparSpoilerParameters.spoilerChordLocationPercent = 26.1;
  decimalTopSparSpoilerParameters.spoilerImmediatelyBehindSpar = true;
  decimalTopSparSpoilerParameters.spars = {
      {25.1, 0, 0, 0, 4.7625, 6.35, 6.0, 5.0, 6.0, 6.0, 1.0},
      {25.1, 1, 0, 0, 4.7625, 6.35, 6.0, 5.0, 6.0, 6.0, 1.0}};
  const auto decimalTopSparSpoiler = designrc::domain::applyWingStructure(
      ribs, decimalTopSparSpoilerParameters);
  for (std::size_t local = 0;
       local < decimalTopSparSpoiler.spoilers.front().forwardRailProfiles.size();
       ++local) {
    const std::size_t ribIndex =
        decimalTopSparSpoiler.spoilers.front().startRibIndex + local;
    const double expectedAftFace =
        0.251 * ribs[ribIndex].chord + 6.35 * 0.5 + 0.01;
    assert(std::abs(
        decimalTopSparSpoiler.spoilers.front().forwardRailProfiles[local][0].x -
        expectedAftFace) < 1.0e-8);
  }
  assert(spoilerWing.spoilers.front().dxfOutline.size() == 4);
  auto lightenedSpoilerParameters = spoilerParameters;
  lightenedSpoilerParameters.spoilerLighteningHoles = true;
  lightenedSpoilerParameters.spoilerMinimumWoodMargin = 6.0;
  lightenedSpoilerParameters.spoilerMinimumCircleDistance = 12.0;
  const auto lightenedSpoilerWing = designrc::domain::applyWingStructure(
      ribs, lightenedSpoilerParameters);
  const auto& lightenedSpoiler = lightenedSpoilerWing.spoilers.front();
  assert(lightenedSpoiler.lighteningHoleOutlines.size() > 4);
  const double spoilerSpan = lightenedSpoiler.dxfOutline[1].x;
  double previousRight = -1.0;
  for (const auto& hole : lightenedSpoiler.lighteningHoleOutlines) {
    assert(hole.size() == 48);
    double minimumX = hole.front().x;
    double maximumX = hole.front().x;
    double minimumY = hole.front().y;
    double maximumY = hole.front().y;
    for (const auto point : hole) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
      minimumY = std::min(minimumY, point.y);
      maximumY = std::max(maximumY, point.y);
    }
    assert(minimumX >= 6.0 - 1.0e-8);
    assert(spoilerSpan - maximumX >= 6.0 - 1.0e-8);
    assert(minimumY >= 6.0 - 1.0e-8);
    assert(lightenedSpoiler.width - maximumY >= 6.0 - 1.0e-8);
    if (previousRight >= 0.0)
      assert(minimumX - previousRight >= 12.0 - 1.0e-8);
    previousRight = maximumX;
  }
  auto carbonSpoilerParameters = spoilerParameters;
  carbonSpoilerParameters.leadingEdgeType = 3;
  carbonSpoilerParameters.topSpar = true;
  carbonSpoilerParameters.bottomSpar = true;
  const auto carbonSpoilerWing = designrc::domain::applyWingStructure(
      ribs, carbonSpoilerParameters);
  const auto sharpNotchCount = [](const auto& segments) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < segments.size(); ++index) {
      const auto& first = segments[index];
      const auto& floor = segments[(index + 1) % segments.size()];
      const auto& last = segments[(index + 2) % segments.size()];
      if (first.spline || floor.spline || last.spline ||
          first.points.size() != 2 || floor.points.size() != 2 ||
          last.points.size() != 2)
        continue;
      const bool connected =
          std::hypot(first.points.back().x - floor.points.front().x,
                     first.points.back().y - floor.points.front().y) < 1.0e-8 &&
          std::hypot(floor.points.back().x - last.points.front().x,
                     floor.points.back().y - last.points.front().y) < 1.0e-8;
      const bool verticalWalls =
          std::abs(first.points.front().x - first.points.back().x) < 1.0e-8 &&
          std::abs(last.points.front().x - last.points.back().x) < 1.0e-8;
      if (connected && verticalWalls) ++count;
    }
    return count;
  };
  // R4 is inside the R3-R7 spoiler range and contains the spoiler plus top
  // and bottom spar notches. All three must retain straight walls and floors.
  assert(sharpNotchCount(carbonSpoilerWing.ribs[3].outlineSegments) >= 3);
  assert(sharpNotchCount(carbonSpoilerWing.ribs[3].partOutlineSegments) >= 3);
  designrc::domain::StructureParameters sheetedCarbonLeParameters;
  sheetedCarbonLeParameters.leadingEdgeType = 4;
  sheetedCarbonLeParameters.leadingEdgeRodOd = 6.0;
  sheetedCarbonLeParameters.leTopSheet = true;
  sheetedCarbonLeParameters.leBottomSheet = true;
  sheetedCarbonLeParameters.leTopSheetThickness = 1.5875;
  sheetedCarbonLeParameters.leBottomSheetThickness = 1.5875;
  sheetedCarbonLeParameters.leTopSheetStopRib = 2;
  sheetedCarbonLeParameters.leBottomSheetStopRib = 2;
  const auto sheetedCarbonLeWing = designrc::domain::applyWingStructure(
      ribs, sheetedCarbonLeParameters);
  const auto carbonLeMember = std::find_if(
      sheetedCarbonLeWing.members.begin(), sheetedCarbonLeWing.members.end(),
      [](const auto& member) {
        return member.name == "CF rod leading edge";
      });
  assert(carbonLeMember != sheetedCarbonLeWing.members.end());
  const auto& rootLeRib = sheetedCarbonLeWing.ribs.front();
  const auto exposedLeCenter = carbonLeMember->centers.front();
  const double leRadius = sheetedCarbonLeParameters.leadingEdgeRodOd * 0.5;
  std::size_t circularCradles = 0;
  for (const auto& segment : rootLeRib.partOutlineSegments) {
    const bool circularCradle = segment.spline &&
        segment.points.size() == 25 &&
        std::all_of(segment.points.begin(), segment.points.end(),
            [&](const auto point) {
              return std::abs(std::hypot(
                  point.x - exposedLeCenter.x,
                  point.y - exposedLeCenter.y) - leRadius) <
                  1.0e-7;
            });
    if (circularCradle) {
      ++circularCradles;
      continue;
    }
    for (const auto point : segment.points)
      assert(std::hypot(
          point.x - exposedLeCenter.x, point.y - exposedLeCenter.y) >=
          leRadius - 1.0e-7);
  }
  assert(circularCradles == 1);
  auto dihedralSpoilerParameters = spoilerParameters;
  dihedralSpoilerParameters.joinerDihedralDegrees = 5.0;
  dihedralSpoilerParameters.spoilerStartRib = 2;
  dihedralSpoilerParameters.spoilerEndRib = 6;
  const auto dihedralSpoilerWing = designrc::domain::applyWingStructure(
      ribs, dihedralSpoilerParameters);
  assert(dihedralSpoilerWing.spoilers.size() == 1);
  dihedralSpoilerParameters.spoilerStartRib = 1;
  bool rejectedCenterSpoilerWithDihedral = false;
  try {
    static_cast<void>(designrc::domain::applyWingStructure(
        ribs, dihedralSpoilerParameters));
  } catch (const std::invalid_argument& error) {
    rejectedCenterSpoilerWithDihedral =
        std::string{error.what()}.find(
            "Dihedral must be 0 degrees for a center spoiler") !=
        std::string::npos;
  }
  assert(rejectedCenterSpoilerWithDihedral);
  const auto spoilerDrawing = designrc::domain::makeSpoilerPartDrawing(
      lightenedSpoiler, "Spoiler");
  assert(spoilerDrawing.paths.size() ==
         lightenedSpoiler.lighteningHoleOutlines.size() + 1);
  assert(spoilerDrawing.labelExclusions.size() ==
         lightenedSpoiler.lighteningHoleOutlines.size());
  assert(spoilerDrawing.preferredLabelPlacement.has_value());
  const auto spoilerLabel = *spoilerDrawing.preferredLabelPlacement;
  double highestLighteningHolePoint =
      std::numeric_limits<double>::lowest();
  double spoilerTop = std::numeric_limits<double>::lowest();
  for (const auto point : lightenedSpoiler.dxfOutline)
    spoilerTop = std::max(spoilerTop, point.y);
  for (const auto& hole : lightenedSpoiler.lighteningHoleOutlines)
    for (const auto point : hole)
      highestLighteningHolePoint =
          std::max(highestLighteningHolePoint, point.y);
  assert(spoilerLabel.position.y > highestLighteningHolePoint);
  assert(spoilerLabel.position.y < spoilerTop);
  const auto arrangedSpoiler =
      designrc::domain::arrangePartDrawings({spoilerDrawing}).front();
  const auto arrangedSpoilerLabel =
      designrc::domain::partLabelPlacement(arrangedSpoiler);
  assert(arrangedSpoilerLabel.has_value());
  double arrangedHighestHolePoint =
      std::numeric_limits<double>::lowest();
  for (const auto& hole : arrangedSpoiler.labelExclusions)
    for (const auto point : hole)
      arrangedHighestHolePoint = std::max(arrangedHighestHolePoint, point.y);
  assert(arrangedSpoilerLabel->position.y > arrangedHighestHolePoint);
  const auto spoilerSvgPath = svgDirectory / "designrc_spoiler.svg";
  designrc::domain::exportSpoilerSvg(
      lightenedSpoiler, spoilerSvgPath, "Spoiler");
  assert(std::filesystem::file_size(spoilerSvgPath) > 100);
  std::filesystem::remove(spoilerSvgPath);
  const auto spoilerDxfPath = svgDirectory / "designrc_spoiler.dxf";
  designrc::domain::exportSpoilerDxf(
      lightenedSpoiler, spoilerDxfPath, "Spoiler");
  std::ifstream spoilerDxf{spoilerDxfPath};
  const std::string spoilerDxfContents{
      std::istreambuf_iterator<char>{spoilerDxf}, {}};
  assert(spoilerDxfContents.find("SPOILER_LIGHTENING_HOLES") !=
         std::string::npos);
  spoilerDxf.close();
  std::filesystem::remove(spoilerDxfPath);

  designrc::domain::StructureParameters wiringParameters;
  wiringParameters.wiringHoles = true;
  wiringParameters.wiringHoleStartRib = 2;
  wiringParameters.wiringHoleEndRib = 4;
  wiringParameters.wiringHoleChordLocationPercent = 50;
  wiringParameters.wiringHoleWidth = 9.525;
  wiringParameters.wiringHoleHeight = 6.35;
  const auto wiredWing = designrc::domain::applyWingStructure(ribs, wiringParameters);
  assert(wiredWing.wiringHoles.size() == 3);
  assert(wiredWing.ribs[0].internalCutouts.empty());
  for (std::size_t index = 1; index <= 3; ++index) {
    assert(wiredWing.ribs[index].internalCutouts.size() == 1);
    const auto& opening = wiredWing.ribs[index].internalCutouts.front();
    const auto [minimum, maximum] = std::minmax_element(opening.begin(), opening.end(),
        [](const auto a, const auto b) { return a.x < b.x; });
    assert(std::abs(minimum->x - 0.50 * ribs[index].chord) < 1.0e-8);
    assert(std::abs(maximum->x - minimum->x - 9.525) < 1.0e-8);
    const auto drawing = designrc::domain::makeStructuredRibPartDrawing(
        wiredWing.ribs[index], "Wired Rib");
    assert(std::any_of(drawing.paths.begin(), drawing.paths.end(),
        [](const auto& path) {
          return path.layer == "RIB_OUTLINE" && path.spline;
        }));
    assert(std::count_if(drawing.paths.begin(), drawing.paths.end(),
        [](const auto& path) { return path.layer == "RIB_HOLES"; }) == 1);
  }

  designrc::domain::StructureParameters ribletParameters;
  ribletParameters.leadingEdgeType = 3;
  ribletParameters.leadingEdgeTubeOd = 4.0;
  ribletParameters.leadingEdgeTubeId = 3.0;
  ribletParameters.spars = {
      {30, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  ribletParameters.riblets = true;
  ribletParameters.ribletStartRib = 2;
  ribletParameters.ribletEndRib = 6;
  ribletParameters.ribletsPerBay = 2;
  auto ribletWing =
      designrc::domain::applyWingStructure(ribs, ribletParameters);
  for (std::size_t index = 0; index < ribletWing.ribs.size(); ++index)
    ribletWing.ribs[index].name = "R" + std::to_string(index + 1);
  designrc::domain::addRiblets(ribletWing, ribletParameters);
  assert(ribletWing.riblets.size() == 8);
  assert(ribletWing.riblets.front().name == "R2a");
  assert(ribletWing.riblets[1].name == "R2b");
  assert(ribletWing.riblets.back().name == "R5b");
  assert(ribletWing.riblets.front().rib.spanPosition >
         ribletWing.ribs[1].rib.spanPosition);
  assert(ribletWing.riblets.front().rib.spanPosition <
         ribletWing.ribs[2].rib.spanPosition);
  assert(!ribletWing.riblets.front().booleanHoles.empty());
  assert(!ribletWing.riblets.front().partOutlineSegments.empty());
  ribletParameters.ribLighteningHoles = true;
  ribletParameters.ribLighteningStartRib = 2;
  ribletParameters.ribLighteningStopRib = 6;
  ribletParameters.ribLighteningMinimumWoodMargin = 2.0;
  ribletParameters.ribLighteningMinimumHoleDistance = 2.0;
  designrc::domain::addRibLighteningHoles(
      ribletWing, ribletParameters);
  assert(std::any_of(
      ribletWing.riblets.begin(), ribletWing.riblets.end(),
      [](const auto& riblet) {
        return !riblet.internalCutouts.empty();
      }));
  return 0;
}
