#pragma once

#include "domain/WingDesign.h"

#include <string>
#include <array>
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

struct Point3 {
  double x{};
  double y{};
  double z{};
};

struct StructureParameters {
  double ribThickness{3.0};
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
  std::string name;
};

struct SpanMember {
  std::string name;
  SpanMemberKind kind{SpanMemberKind::Rectangular};
  double width{};
  double height{};
  double innerDiameter{};
  std::vector<Point2> centers;
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
  Point3 innerEndpoint;
  Point3 outerEndpoint;
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
  std::vector<SpanMember> members;
  std::vector<ProfiledSpanMember> profiledMembers;
  std::vector<ControlSurfacePart> controlSurfaces;
  std::vector<SheetStockPart> sheetStockParts;
  std::vector<ShearWebPart> shearWebs;
  std::vector<JoinerPart> joiners;
  std::vector<SheetingPart> sheeting;
};

[[nodiscard]] StructuredWing applyWingStructure(
    const std::vector<RibDefinition>& ribs,
    const StructureParameters& parameters);

} // namespace designrc::domain
