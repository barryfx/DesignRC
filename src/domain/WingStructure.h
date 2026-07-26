#pragma once

#include "domain/WingDesign.h"

#include <string>
#include <array>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace designrc::domain {

class EdgeHeightError final : public std::invalid_argument {
public:
  EdgeHeightError(std::string edgeName, std::size_t ribIndex,
                  double cutHeightMm, double specifiedHeightMm);
  [[nodiscard]] const std::string& edgeName() const { return edgeName_; }
  [[nodiscard]] std::size_t ribIndex() const { return ribIndex_; }
  [[nodiscard]] double cutHeightMm() const { return cutHeightMm_; }
  [[nodiscard]] double specifiedHeightMm() const { return specifiedHeightMm_; }
private:
  std::string edgeName_;
  std::size_t ribIndex_{};
  double cutHeightMm_{};
  double specifiedHeightMm_{};
};

enum class SpanMemberKind { Rectangular, Tube, Rod, Turbulator };

struct SparParameters {
  int chordLocationPercent{25};
  int verticalLocation{2}; // 0 top, 1 bottom, 2 mid
  int material{1}; // 0 wood, 1 carbon fiber
  int type{0}; // 0 tube, 1 rod, 2 strip
  double woodHeight{5.0};
  double woodWidth{9.0};
  double tubeOd{6.0};
  double tubeId{5.0};
  double rodOd{6.0};
  double stripWidth{6.0};
  double stripThickness{1.0};
};

struct Point3 {
  double x{};
  double y{};
  double z{};
};

struct RibOutlineSegment {
  std::vector<Point2> points;
  bool spline{false};
};

struct StructureParameters {
  double ribThickness{3.0};
  std::vector<SparParameters> spars;
  bool sparShearWebs{false};
  double sparShearWebThickness{3.0};
  bool topSpar{false};
  double topSparHeight{5.0};
  double topSparWidth{10.0};
  bool bottomSpar{false};
  double bottomSparHeight{5.0};
  double bottomSparWidth{10.0};
  bool shearWebs{false};
  double shearWebThickness{3.0};
  int carbonSpar{0}; // 0 none, 1 tube, 2 rod
  double cfTubeOd{6.0};
  double cfTubeId{5.0};
  double cfRodOd{6.0};
  bool leTopSheet{false};
  double leTopSheetThickness{2.0};
  int leTopSheetStopRib{2};
  bool leBottomSheet{false};
  double leBottomSheetThickness{2.0};
  int leBottomSheetStopRib{2};
  bool teTopSheet{false};
  double teTopSheetThickness{2.0};
  int teTopSheetStopRib{2};
  bool teBottomSheet{false};
  double teBottomSheetThickness{2.0};
  int teBottomSheetStopRib{2};
  bool turbulators{false};
  int turbulatorCount{1};
  double turbulatorHeight{2.0};
  double turbulatorWidth{2.0};
  bool topRearSpar{false};
  double topRearSparHeight{4.0};
  double topRearSparWidth{4.0};
  bool bottomRearSpar{false};
  double bottomRearSparHeight{4.0};
  double bottomRearSparWidth{4.0};
  int leadingEdgeType{0}; // 0 none, 2 block, 3 tube, 4 rod
  double leadingEdgeWidth{5.0};
  double leadingEdgeHeight{7.0};
  double leadingEdgeTubeOd{2.0};
  double leadingEdgeTubeId{1.0};
  double leadingEdgeRodOd{2.0};
  int trailingEdgeType{0}; // 0 none, 2 sheet
  double trailingEdgeWidth{20.0};
  double trailingEdgeHeight{3.0};
  bool trailingEdgeSlotted{false};
  double trailingEdgeSlotDepth{6.0};
  bool ailerons{false};
  double aileronWidth{35.0};
  double aileronHeight{10.0};
  double aileronHingePostWidth{6.0};
  double aileronHingePostHeight{10.0};
  int aileronStartRib{1};
  int aileronStopRib{9};
  bool flaps{false};
  double flapWidth{40.0};
  double flapHeight{10.0};
  double flapHingePostWidth{6.0};
  double flapHingePostHeight{10.0};
  int flapStartRib{1};
  int flapStopRib{5};
  double controlSurfaceGap{1.5};
  bool spoilers{false};
  int spoilerStartRib{3};
  int spoilerEndRib{7};
  int spoilerChordLocationPercent{30};
  double spoilerWidth{25.4};
  double spoilerThickness{3.0};
  double spoilerFrameRailWidth{6.0};
  double spoilerSupportRailHeight{3.0};
  bool spoilerLighteningHoles{false};
  double spoilerMinimumWoodMargin{6.0};
  double spoilerMinimumCircleDistance{12.0};
  bool ribLighteningHoles{false};
  int ribLighteningStartRib{3};
  int ribLighteningStopRib{7};
  double ribLighteningMinimumWoodMargin{6.0};
  double ribLighteningMinimumHoleDistance{12.0};
  bool riblets{false};
  int ribletStartRib{2};
  int ribletEndRib{8};
  int ribletsPerBay{2};
  bool wiringHoles{false};
  int wiringHoleStartRib{2};
  int wiringHoleEndRib{9};
  int wiringHoleChordLocationPercent{50};
  double wiringHoleWidth{9.525};
  double wiringHoleHeight{6.35};
  bool rib1aPresent{false};
  bool centerSparWoodJoiner{false};
  bool behindSparJoiner{false};
  int behindSparJoinerType{0}; // 0 none, 1 CF rod, 2 CF tube, 3 aluminum tube
  double behindSparJoinerOd{6.0};
  double behindSparJoinerId{5.0};
  bool fiftyPercentJoiner{false};
  int fiftyPercentJoinerType{0};
  double fiftyPercentJoinerOd{6.0};
  double fiftyPercentJoinerId{5.0};
  double joinerAxisAngleDegrees{0.0};
  double circularJoinerAxisAngleDegrees{0.0};
  bool circularJoinerSpansJoint{true};
  double joinerMirrorAngleDegrees{0.0};
  double joinerDihedralDegrees{0.0};
};

struct StructuredRib {
  RibDefinition rib;
  std::vector<Point2> outerOutline;
  std::vector<std::vector<Point2>> holes;
  std::vector<std::vector<Point2>> booleanCutouts;
  std::vector<std::vector<Point2>> booleanHoles;
  // A center Sleeve/Rod joint can use different ODs on the two wing halves.
  // These openings are kept out of booleanHoles so the mirrored ribs do not
  // receive both concentric cuts.
  std::vector<std::vector<Point2>> positiveHalfBooleanHoles;
  std::vector<std::vector<Point2>> negativeHalfBooleanHoles;
  bool uniqueHalfPartVariants{};
  std::string name;
  // Unique manufacturing names for asymmetric center ribs. Empty for ribs
  // that remain a single mirrored part definition.
  std::string positiveHalfName;
  std::string negativeHalfName;
  // Closed internal polygons cut after extrusion. Kept separate from
  // booleanCutouts because those may be full-height wood-joiner split slots.
  std::vector<std::vector<Point2>> internalCutouts;
  std::vector<RibOutlineSegment> outlineSegments;
  // Finished 2D manufacturing contour when an open Boolean feature, such as
  // an exposed carbon leading edge, changes the extruded outer outline.
  std::vector<Point2> partOutline;
  std::vector<RibOutlineSegment> partOutlineSegments;
};

struct SpanMember {
  std::string name;
  SpanMemberKind kind{SpanMemberKind::Rectangular};
  double width{};
  double height{};
  double innerDiameter{};
  std::vector<Point2> centers;
  bool carbonFiber{false};
  int verticalLocation{2}; // 0 top, 1 bottom, 2 middle
  bool cutsSheeting{false};
};

struct ShearWebPart {
  std::string name;
  std::size_t bayIndex{};
  double thickness{};
  std::vector<Point2> outline;
  std::vector<Point2> stationCorners; // bottom root, bottom tip, top tip, top root
};

struct ProfiledSpanMember {
  std::string name;
  std::vector<std::vector<Point2>> profiles;
  std::vector<std::vector<Point2>> slotProfiles;
  std::vector<std::pair<std::size_t, std::size_t>> activeRanges;
};

struct ControlSurfacePart {
  std::string name;
  std::size_t startRibIndex{};
  std::size_t stopRibIndex{};
  double width{};
  double gap{};
  double hingePostWidth{};
  double hingePostHeight{};
  std::vector<std::vector<Point2>> profiles;
  std::vector<Point2> hingePostCenters;
  bool cutStartRib{};
  bool cutStopRib{};
  bool extendThroughStopRib{};
};

struct SheetStockPart {
  std::string name;
  std::vector<Point2> outline;
  std::vector<std::vector<Point2>> slots;
};

struct SpoilerPart {
  std::string name{"Spoiler"};
  std::size_t startRibIndex{};
  std::size_t endRibIndex{};
  double chordLocationPercent{30.0};
  double width{};
  double thickness{};
  double frameRailWidth{};
  double supportRailHeight{};
  double gap{1.5875};
  bool spansCenter{};
  double minimumWoodMargin{};
  std::vector<std::array<Point2, 4>> spoilerProfiles;
  std::vector<std::array<Point2, 4>> forwardRailProfiles;
  std::vector<std::array<Point2, 4>> aftRailProfiles;
  std::vector<std::array<Point2, 4>> supportProfiles;
  std::vector<Point2> dxfOutline;
  std::vector<std::vector<Point2>> lighteningHoleOutlines;
};

struct WiringHoleCut {
  std::string name;
  std::size_t ribIndex{};
  std::vector<Point2> outline;
};

struct JoinerPart {
  std::string name;
  SpanMemberKind kind{SpanMemberKind::Rod};
  double outerDiameter{};
  double innerDiameter{};
  std::size_t stopRibIndex{};
  std::vector<Point2> centers;
  std::vector<std::array<Point2, 4>> rectangularProfiles;
  std::vector<std::array<Point3, 4>> innerRectangularProfiles;
  std::vector<Point2> dxfOutline;
  bool spansJoint{true};
  double mirrorPlaneAngleDegrees{};
  double axisAngleDegrees{};
  bool hasExplicitEndpoints{false};
  bool mirrorInAssembly{true};
  Point3 innerEndpoint;
  Point3 outerEndpoint;
  std::string annotationName;
  bool annotateOnBothPlanHalves{false};
  bool annotateOnMirroredPlanHalf{false};
};

struct SheetingPart {
  std::string name;
  std::size_t stopRibIndex{};
  std::vector<std::vector<Point2>> profiles;
  std::vector<std::vector<Point2>> fullProfiles;
  std::vector<std::vector<Point2>> controlProfiles;
  std::vector<bool> controlBays;
};

struct StructuredWing {
  std::vector<StructuredRib> ribs;
  std::vector<StructuredRib> riblets;
  std::vector<SpanMember> members;
  std::vector<ProfiledSpanMember> profiledMembers;
  std::vector<ControlSurfacePart> controlSurfaces;
  std::vector<SheetStockPart> sheetStockParts;
  std::vector<ShearWebPart> shearWebs;
  std::vector<JoinerPart> joiners;
  std::vector<SheetingPart> sheeting;
  std::vector<SpoilerPart> spoilers;
  std::vector<WiringHoleCut> wiringHoles;
};

[[nodiscard]] StructuredWing applyWingStructure(
    const std::vector<RibDefinition>& ribs,
    const StructureParameters& parameters);

void addRiblets(StructuredWing& wing,
                const StructureParameters& parameters);

using RibLighteningProgressCallback =
    std::function<void(std::size_t, std::size_t)>;

void addRibLighteningHoles(
    StructuredWing& wing, const StructureParameters& parameters,
    const RibLighteningProgressCallback& progress = {},
    std::size_t maximumWorkers = 0);

[[nodiscard]] std::size_t ribLighteningHoleWorkerCount(
    std::size_t ribCount, std::size_t maximumWorkers = 0);

[[nodiscard]] std::vector<RibOutlineSegment> makeRibOutlineSegments(
    const std::vector<Point2>& closedOutline);

} // namespace designrc::domain
