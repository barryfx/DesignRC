#include "gui/MainWindow.h"

#include "domain/DxfExporter.h"
#include "geometry/OcctRibBuilder.h"
#include "geometry/StepExporter.h"
#include "gui/OcctViewport.h"
#include "gui/PartPdfExporter.h"
#include "gui/PlanViewport.h"
#include "gui/TechnicalDrawing.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStringList>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPointer>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>

#include <filesystem>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>

namespace designrc::gui {
namespace {

const char* occtExceptionMessage(const Standard_Failure& exception) {
#if OCC_VERSION_HEX >= 0x080000
  return exception.what();
#else
  return exception.GetMessageString();
#endif
}

class BusyCursor final {
public:
  BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
  ~BusyCursor() { QApplication::restoreOverrideCursor(); }
};

std::size_t maximumGeometryThreadCount() {
  const unsigned processors = std::thread::hardware_concurrency();
  return processors > 1 ? static_cast<std::size_t>(processors - 1) : 1;
}

std::size_t cpuWorkerCount(
    const std::size_t taskCount, const std::size_t maximumWorkers = 0) {
  if (taskCount == 0) return 0;
  const std::size_t available = maximumWorkers > 0
      ? std::min(maximumWorkers, maximumGeometryThreadCount())
      : maximumGeometryThreadCount();
  return std::min(
      taskCount, std::max<std::size_t>(1, available));
}

template <typename Function>
void runCpuParallelTasks(
    const std::size_t taskCount, const std::size_t maximumWorkers,
    Function&& function) {
  const std::size_t workerCount =
      cpuWorkerCount(taskCount, maximumWorkers);
  std::atomic_size_t nextTask{0};
  std::vector<std::future<void>> workers;
  workers.reserve(workerCount);
  for (std::size_t worker = 0; worker < workerCount; ++worker)
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t index = nextTask.fetch_add(1);
        if (index >= taskCount) return;
        function(index);
      }
    }));
  for (auto& worker : workers) worker.get();
}

QString projectFilter() { return "DesignRC project (*.designrc)"; }

DisplayUnit effectiveParameterUnit(const WingPanelData& panel,
                                   const QString& key,
                                   const DisplayUnit globalUnit) {
  const auto override = panel.unitOverrides.value(key, UnitOverride::Global);
  if (override == UnitOverride::Inches) return DisplayUnit::Inches;
  if (override == UnitOverride::Millimeters) return DisplayUnit::Millimeters;
  return globalUnit;
}

void migrateLegacyLeadingEdgeWidthUnit(WingPanelData& panel) {
  if (panel.unitOverrides.value("leadingEdgeWidth", UnitOverride::Global) !=
      UnitOverride::Inches)
    return;
  constexpr double tolerance = 1.0e-8;
  const bool isLegacyInstalledValue =
      std::abs(panel.leadingEdgeWidth - 5.0) < tolerance ||
      std::abs(panel.leadingEdgeWidth - 4.7625) < tolerance;
  if (isLegacyInstalledValue)
    panel.unitOverrides.insert("leadingEdgeWidth", UnitOverride::Global);
}

void migrateLegacyDuplicateDefaultWoodJoiner(WingPanelData& panel) {
  if (panel.fixedJoiners.size() < 2) return;
  const auto& first = panel.fixedJoiners[0];
  const auto& second = panel.fixedJoiners[1];
  constexpr double tolerance = 1.0e-8;
  if (first.material == 0 && second.material == 0 &&
      first.carbonType == 0 && second.carbonType == 1 &&
      std::abs(first.chordLocationPercent - second.chordLocationPercent) <
          tolerance &&
      std::abs(first.woodThickness - second.woodThickness) < tolerance)
    panel.fixedJoiners.erase(panel.fixedJoiners.begin() + 1);
}

QString compactNumber(const double value, const int decimals) {
  QString result = QString::number(value, 'f', decimals);
  while (result.contains('.') && result.endsWith('0')) result.chop(1);
  if (result.endsWith('.')) result.chop(1);
  return result;
}

QString formatParameterLength(const double millimeters, const DisplayUnit unit) {
  return unit == DisplayUnit::Inches
      ? compactNumber(millimeters / 25.4, 3) + " in"
      : compactNumber(millimeters, 2) + " mm";
}

QString formatEdgeHeightError(const domain::EdgeHeightError& error,
                              const WingPanelData& panel,
                              const DisplayUnit globalUnit,
                              const std::size_t panelIndex) {
  const QString edge = QString::fromStdString(error.edgeName());
  const QString parameterKey = edge == "LE"
      ? QString{"leadingEdgeHeight"} : QString{"trailingEdgeHeight"};
  const DisplayUnit unit = effectiveParameterUnit(panel, parameterKey, globalUnit);
  return QString{"Panel %1: %2 cut edge at rib %3 is %4, not smaller than "
                 "the specified %2 Height of %5"}
      .arg(static_cast<qulonglong>(panelIndex + 1)).arg(edge)
      .arg(static_cast<qulonglong>(error.ribIndex()))
      .arg(formatParameterLength(error.cutHeightMm(), unit))
      .arg(formatParameterLength(error.specifiedHeightMm(), unit));
}

class PanelEdgeHeightError final : public std::runtime_error {
public:
  PanelEdgeHeightError(const QString& message, const std::size_t panelIndex,
                       std::string edgeName, const double cutHeightMm)
      : std::runtime_error(message.toStdString()), panelIndex_{panelIndex},
        edgeName_{std::move(edgeName)}, cutHeightMm_{cutHeightMm} {}
  [[nodiscard]] std::size_t panelIndex() const { return panelIndex_; }
  [[nodiscard]] const std::string& edgeName() const { return edgeName_; }
  [[nodiscard]] double cutHeightMm() const { return cutHeightMm_; }
private:
  std::size_t panelIndex_{};
  std::string edgeName_;
  double cutHeightMm_{};
};

struct EdgeHeightCorrection {
  std::size_t panelIndex{};
  bool leadingEdge{};
  double heightMm{};
};

domain::StructureParameters structureParametersFor(const WingPanelData& d,
                                                    const DisplayUnit unit,
                                                    const double joinerAxisAngle,
                                                    const double joinerMirrorAngle,
                                                    const double circularJoinerAxisAngle,
                                                    const bool circularJoinerSpansJoint) {
  domain::StructureParameters s;
  s.ribThickness = d.ribThickness;
  s.spars.reserve(d.spars.size());
  for (const auto& spar : d.spars)
    s.spars.push_back({spar.chordLocationPercent, spar.verticalLocation,
        spar.material, spar.type, spar.woodHeight, spar.woodWidth,
        spar.tubeOd, spar.tubeId, spar.rodOd, spar.stripWidth,
        spar.stripThickness, spar.tipChordLocationPercent >= 0.0
            ? spar.tipChordLocationPercent : spar.chordLocationPercent});
  s.sparShearWebs = d.sparShearWebs;
  s.sparShearWebThickness = d.sparDefaults.shearWebThickness;
  const bool useLegacySpars = d.spars.empty();
  s.topSpar = useLegacySpars && d.topSpar; s.topSparHeight = d.topSparHeight; s.topSparWidth = d.topSparWidth;
  s.bottomSpar = useLegacySpars && d.bottomSpar; s.bottomSparHeight = d.bottomSparHeight; s.bottomSparWidth = d.bottomSparWidth;
  s.shearWebs = useLegacySpars && d.shearWebs; s.shearWebThickness = d.shearWebWidth;
  s.carbonSpar = useLegacySpars ? d.carbonSpar : 0; s.cfTubeOd = d.cfTubeOd; s.cfTubeId = d.cfTubeId; s.cfRodOd = d.cfRodOd;
  s.leTopSheet = d.leTopSheet; s.leTopSheetThickness = d.leTopSheetThickness;
  s.leTopSheetStopChordPercent = d.leTopSheetStopChordPercent;
  s.leTopSheetUpToSpar = d.leTopSheetUpToSpar;
  s.leBottomSheet = d.leBottomSheet; s.leBottomSheetThickness = d.leBottomSheetThickness;
  s.leBottomSheetStopChordPercent = d.leBottomSheetStopChordPercent;
  s.leBottomSheetUpToSpar = d.leBottomSheetUpToSpar;
  s.teTopSheet = d.teTopSheet; s.teTopSheetThickness = d.teTopSheetThickness;
  s.teBottomSheet = d.teBottomSheet; s.teBottomSheetThickness = d.teBottomSheetThickness;
  s.turbulators = d.turbulators; s.turbulatorCount = d.turbulatorCount;
  s.turbulatorHeight = d.turbulatorHeight; s.turbulatorWidth = d.turbulatorWidth;
  s.topRearSpar = useLegacySpars && d.topRearSpar; s.topRearSparHeight = d.topRearSparHeight; s.topRearSparWidth = d.topRearSparWidth;
  s.bottomRearSpar = useLegacySpars && d.bottomRearSpar; s.bottomRearSparHeight = d.bottomRearSparHeight; s.bottomRearSparWidth = d.bottomRearSparWidth;
  s.leadingEdgeType = d.leadingEdgeType; s.leadingEdgeWidth = d.leadingEdgeWidth; s.leadingEdgeHeight = d.leadingEdgeHeight;
  s.leadingEdgeTubeOd = d.leadingEdgeTubeOd; s.leadingEdgeTubeId = d.leadingEdgeTubeId; s.leadingEdgeRodOd = d.leadingEdgeRodOd;
  s.trailingEdgeType = d.trailingEdgeType; s.trailingEdgeWidth = d.trailingEdgeWidth; s.trailingEdgeHeight = d.trailingEdgeHeight;
  s.trailingEdgeSlotted = d.slottedForRibs;
  s.trailingEdgeSlotDepth = unit == DisplayUnit::Inches ? 25.4 / 4.0 : 6.0;
  s.topTeSheeting = d.topTeSheeting;
  s.topTeSheetingWidth = d.topTeSheetingWidth;
  s.topTeSheetingThickness = d.topTeSheetingThickness;
  s.topTeSheetingTaper = d.topTeSheetingTaper;
  s.topTeSheetingTaperStartLocationPercent =
      d.topTeSheetingTaperStartLocationPercent;
  s.bottomTeSheeting = d.bottomTeSheeting;
  s.bottomTeSheetingWidth = d.bottomTeSheetingWidth;
  s.bottomTeSheetingThickness = d.bottomTeSheetingThickness;
  s.bottomTeSheetingTaper = d.bottomTeSheetingTaper;
  s.bottomTeSheetingTaperStartLocationPercent =
      d.bottomTeSheetingTaperStartLocationPercent;
  const auto station = [&d](const int number) { return d.addRib1a && number >= 2 ? number + 1 : number; };
  s.leTopSheetStopRib = station(d.leTopSheetStopRib); s.leBottomSheetStopRib = station(d.leBottomSheetStopRib);
  s.teTopSheetStopRib = station(d.teTopSheetStopRib); s.teBottomSheetStopRib = station(d.teBottomSheetStopRib);
  s.ailerons = d.ailerons; s.aileronWidth = d.aileronWidth; s.aileronHeight = d.aileronHeight;
  s.aileronHingePostWidth = d.aileronHingePostWidth; s.aileronHingePostHeight = d.aileronHingePostHeight;
  s.aileronStartRib = station(d.aileronStartRib); s.aileronStopRib = station(d.aileronStopRib);
  s.flaps = d.flaps; s.flapWidth = d.flapWidth; s.flapHeight = d.flapHeight;
  s.flapHingePostWidth = d.flapHingePostWidth; s.flapHingePostHeight = d.flapHingePostHeight;
  s.flapStartRib = station(d.flapStartRib); s.flapStopRib = station(d.flapStopRib);
  s.controlSurfaceGap = unit == DisplayUnit::Inches ? 25.4 / 16.0 : 1.5;
  s.spoilers = d.spoilers;
  s.spoilerStartRib = station(d.spoilerStartRib);
  s.spoilerEndRib = station(d.spoilerEndRib);
  s.spoilerChordLocationPercent = d.spoilerChordLocationPercent;
  s.spoilerImmediatelyBehindSpar = d.spoilerImmediatelyBehindSpar;
  s.spoilerWidth = d.spoilerWidth;
  s.spoilerThickness = d.spoilerThickness;
  s.spoilerFrameRailWidth = d.spoilerFrameRailWidth;
  s.spoilerSupportRailHeight = d.spoilerSupportRailHeight;
  s.spoilerLighteningHoles = d.spoilerLighteningHoles;
  s.spoilerMinimumWoodMargin = d.spoilerMinimumWoodMargin;
  s.spoilerMinimumCircleDistance = d.spoilerMinimumCircleDistance;
  s.ribLighteningHoles = d.ribLighteningHoles;
  s.ribLighteningStartRib = station(d.ribLighteningStartRib);
  s.ribLighteningStopRib = station(d.ribLighteningStopRib > 0
      ? d.ribLighteningStopRib : std::max(1, d.ribCount - 2));
  s.ribLighteningMinimumWoodMargin =
      d.ribLighteningMinimumWoodMargin;
  s.ribLighteningMinimumHoleDistance =
      d.ribLighteningMinimumHoleDistance;
  s.riblets = d.riblets;
  s.ribletStartRib = station(d.ribletStartRib);
  s.ribletEndRib = station(d.ribletEndRib > 0
      ? d.ribletEndRib : d.ribCount);
  s.ribletsPerBay = d.ribletsPerBay;
  s.wiringHoles = d.wiringHoles;
  // Wiring selectors use generated station order directly so station 2 can
  // represent R1a when that optional rib is present.
  s.wiringHoleStartRib = d.wiringHoleStartRib;
  s.wiringHoleEndRib = d.wiringHoleEndRib > 0
      ? d.wiringHoleEndRib
      : d.ribCount + (d.addRib1a ? 1 : 0);
  s.wiringHoleChordLocationPercent = d.wiringHoleChordLocationPercent;
  s.wiringHoleWidth = d.wiringHoleWidth;
  s.wiringHoleHeight = d.wiringHoleHeight;
  s.rib1aPresent = d.addRib1a;
  const bool useLegacyJoiners = d.joinerPanelMode < 0;
  s.centerSparWoodJoiner = useLegacyJoiners && d.centerSparWoodJoiner;
  s.behindSparJoiner = useLegacyJoiners && d.behindSparJoiner; s.behindSparJoinerType = d.behindSparJoinerType;
  s.behindSparJoinerOd = d.behindSparJoinerOd; s.behindSparJoinerId = d.behindSparJoinerId;
  s.fiftyPercentJoiner = useLegacyJoiners && d.fiftyPercentJoiner; s.fiftyPercentJoinerType = d.fiftyPercentJoinerType;
  s.fiftyPercentJoinerOd = d.fiftyPercentJoinerOd; s.fiftyPercentJoinerId = d.fiftyPercentJoinerId;
  s.joinerAxisAngleDegrees = joinerAxisAngle;
  s.circularJoinerAxisAngleDegrees = circularJoinerAxisAngle;
  s.circularJoinerSpansJoint = circularJoinerSpansJoint;
  s.joinerMirrorAngleDegrees = joinerMirrorAngle;
  s.joinerDihedralDegrees = d.dihedral;
  return s;
}

struct ModelPoint {
  double x{};
  double y{};
  double z{};
};

ModelPoint modelSectionPoint(const domain::RibDefinition& rib,
                             const domain::Point2 point,
                             const double normalOffset = 0.0) {
  const double twist = rib.twistDegrees * std::numbers::pi / 180.0;
  const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
  const double sectionX = std::cos(twist) * point.x - std::sin(twist) * point.y;
  const double sectionZ = std::sin(twist) * point.x + std::cos(twist) * point.y;
  return {rib.leadingEdgeOffset + sectionX,
          rib.spanPosition - std::sin(plane) * sectionZ +
              std::cos(plane) * normalOffset,
          rib.dihedralHeight + std::cos(plane) * sectionZ +
              std::sin(plane) * normalOffset};
}

domain::Point2 localSectionPoint(const domain::RibDefinition& rib, const ModelPoint point) {
  const double twist = rib.twistDegrees * std::numbers::pi / 180.0;
  const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
  const double sectionX = point.x - rib.leadingEdgeOffset;
  const double sectionZ = -std::sin(plane) * (point.y - rib.spanPosition) +
      std::cos(plane) * (point.z - rib.dihedralHeight);
  return {std::cos(twist) * sectionX + std::sin(twist) * sectionZ,
          -std::sin(twist) * sectionX + std::cos(twist) * sectionZ};
}

std::vector<domain::Point2> circularCut(const domain::Point2 center, const double diameter) {
  std::vector<domain::Point2> points;
  constexpr int samples = 48;
  for (int i = 0; i < samples; ++i) {
    const double angle = -2.0 * std::numbers::pi * static_cast<double>(i) / samples;
    points.push_back({center.x + std::cos(angle) * diameter * 0.5,
                      center.y + std::sin(angle) * diameter * 0.5});
  }
  return points;
}

void addInnerPanelJoinerCuts(domain::StructuredWing& inner,
                             domain::StructuredWing& outer,
                             const double innerRibThickness,
                             const double outerRibThickness) {
  if (inner.ribs.empty() || outer.ribs.empty()) return;
  for (auto& joiner : outer.joiners) {
    const auto& outerRootRib = outer.ribs.front().rib;
    const auto& outerEndRib = outer.ribs[joiner.stopRibIndex].rib;
    ModelPoint rootModel;
    ModelPoint endModel;
    double width = joiner.outerDiameter;
    std::array<ModelPoint, 4> rootCorners{};
    std::array<ModelPoint, 4> endCorners{};
    if (joiner.kind == domain::SpanMemberKind::Rectangular) {
      const auto& rootProfile = joiner.rectangularProfiles.front();
      const auto& endProfile = joiner.rectangularProfiles.back();
      for (std::size_t corner = 0; corner < 4; ++corner) {
        rootCorners[corner] = modelSectionPoint(outerRootRib, rootProfile[corner]);
        endCorners[corner] = modelSectionPoint(outerEndRib, endProfile[corner]);
      }
      rootModel = modelSectionPoint(outerRootRib,
          {0.5 * (rootProfile[0].x + rootProfile[2].x),
           0.5 * (rootProfile[0].y + rootProfile[2].y)});
      endModel = modelSectionPoint(outerEndRib,
          {0.5 * (endProfile[0].x + endProfile[2].x),
           0.5 * (endProfile[0].y + endProfile[2].y)});
    } else {
      rootModel = modelSectionPoint(outerRootRib, joiner.centers.front());
      endModel = modelSectionPoint(outerEndRib, joiner.centers.back());
    }
    double deltaY = endModel.y - rootModel.y;
    if (std::abs(deltaY) < 1.0e-9) continue;
    ModelPoint direction{endModel.x - rootModel.x,
                         deltaY, endModel.z - rootModel.z};
    if (joiner.kind != domain::SpanMemberKind::Rectangular) {
      direction.x = 0.0;
      direction.z = std::tan(joiner.axisAngleDegrees * std::numbers::pi / 180.0) * deltaY;
      endModel = {rootModel.x + direction.x, rootModel.y + direction.y,
                  rootModel.z + direction.z};
    }
    const domain::SpanMember* topSpar = nullptr;
    const domain::SpanMember* bottomSpar = nullptr;
    for (const auto& member : inner.members) {
      if (member.name == "Top spar") topSpar = &member;
      if (member.name == "Bottom spar") bottomSpar = &member;
    }
    std::size_t firstInnerWoodJoinerRib = inner.ribs.size();
    for (std::size_t reverse = 0; reverse < inner.ribs.size(); ++reverse) {
      if (reverse > 1) break;
      const std::size_t i = inner.ribs.size() - 1 - reverse;
      const auto& rib = inner.ribs[i].rib;
      const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
      const double denominator = std::cos(plane) * direction.y +
          std::sin(plane) * direction.z;
      if (std::abs(denominator) < 1.0e-9) continue;
      const double t = (std::cos(plane) * (rib.spanPosition - rootModel.y) +
          std::sin(plane) * (rib.dihedralHeight - rootModel.z)) / denominator;
      const auto center = localSectionPoint(rib,
          {rootModel.x + direction.x * t,
           rootModel.y + direction.y * t,
           rootModel.z + direction.z * t});
      if (joiner.kind == domain::SpanMemberKind::Rectangular) {
        firstInnerWoodJoinerRib = std::min(firstInnerWoodJoinerRib, i);
        if (reverse == 0) {
          std::vector<domain::Point2> jointCut;
          for (const auto& corner : rootCorners)
            jointCut.push_back(localSectionPoint(rib, corner));
          inner.ribs[i].ribSplitCutouts.push_back(jointCut);
          inner.ribs[i].booleanCutouts.push_back(std::move(jointCut));
        }
      } else {
        const double directionLength = std::sqrt(direction.x * direction.x +
            direction.y * direction.y + direction.z * direction.z);
        const double normalProjection = std::abs(
            (std::cos(plane) * direction.y + std::sin(plane) * direction.z) /
            directionLength);
        // Cross-panel joiner holes are applied after the rib face has been
        // extruded. This avoids OCCT's unstable multi-wire face construction
        // when a thin rib already contains a close-fitting CF leading-edge hole.
        inner.ribs[i].booleanHoles.push_back(circularCut(center,
            joiner.outerDiameter / std::max(0.25, normalProjection)));
        if (reverse == 1) {
          const ModelPoint innerCenter{rootModel.x + direction.x * t,
                                       rootModel.y + direction.y * t,
                                       rootModel.z + direction.z * t};
          const ModelPoint unit{direction.x / directionLength,
                                direction.y / directionLength,
                                direction.z / directionLength};
          const double innerExtension = innerRibThickness * 0.5 /
              std::max(0.25, normalProjection);
          const double outerPlane = outerEndRib.ribPlaneAngleDegrees *
              std::numbers::pi / 180.0;
          const double outerProjection = std::abs(
              unit.y * std::cos(outerPlane) + unit.z * std::sin(outerPlane));
          const double outerExtension = outerRibThickness * 0.5 /
              std::max(0.25, outerProjection);
          joiner.innerEndpoint = {innerCenter.x - unit.x * innerExtension,
                                  innerCenter.y - unit.y * innerExtension,
                                  innerCenter.z - unit.z * innerExtension};
          joiner.outerEndpoint = {endModel.x + unit.x * outerExtension,
                                  endModel.y + unit.y * outerExtension,
                                  endModel.z + unit.z * outerExtension};
          joiner.hasExplicitEndpoints = true;
        }
      }
    }
    if (joiner.kind == domain::SpanMemberKind::Rectangular &&
        firstInnerWoodJoinerRib + 1 < inner.ribs.size()) {
      inner.shearWebs.erase(std::remove_if(inner.shearWebs.begin(), inner.shearWebs.end(),
          [firstInnerWoodJoinerRib](const domain::ShearWebPart& web) {
            return web.bayIndex > firstInnerWoodJoinerRib;
          }), inner.shearWebs.end());
    }
    if (joiner.kind == domain::SpanMemberKind::Rectangular && topSpar && bottomSpar) {
      const double mirrorAngle = joiner.mirrorPlaneAngleDegrees *
          std::numbers::pi / 180.0;
      const double normalY = std::cos(mirrorAngle);
      const double normalZ = std::sin(mirrorAngle);
      const auto reflect = [&](const ModelPoint point) {
        const double distance = normalY * (point.y - rootModel.y) +
            normalZ * (point.z - rootModel.z);
        return ModelPoint{point.x, point.y - 2.0 * normalY * distance,
                          point.z - 2.0 * normalZ * distance};
      };
      std::array<ModelPoint, 4> reflectedEnd{};
      for (std::size_t corner = 0; corner < 4; ++corner) {
        reflectedEnd[corner] = reflect(endCorners[corner]);
      }
      const std::size_t adjacentIndex = inner.ribs.size() - 2;
      const auto& adjacentRib = inner.ribs[adjacentIndex].rib;
      const double adjacentTipFace =
          (adjacentRib.ribThicknessStartFactor + 1.0) * innerRibThickness;
      const auto boundaryPoint = [&](const domain::SpanMember& spar,
                                     const bool topBoundary) {
        auto local = spar.centers[adjacentIndex];
        local.y += topBoundary ? -spar.height * 0.5 : spar.height * 0.5;
        return modelSectionPoint(adjacentRib, local, adjacentTipFace);
      };
      const auto innerTop = boundaryPoint(*topSpar, true);
      const auto innerBottom = boundaryPoint(*bottomSpar, false);
      double endpointX = 0.0;
      for (const auto& point : reflectedEnd) endpointX += point.x * 0.25;
      const double halfWidth = width * 0.5;
      joiner.innerRectangularProfiles = {{
          domain::Point3{endpointX - halfWidth, innerBottom.y, innerBottom.z},
          domain::Point3{endpointX + halfWidth, innerBottom.y, innerBottom.z},
          domain::Point3{endpointX + halfWidth, innerTop.y, innerTop.z},
          domain::Point3{endpointX - halfWidth, innerTop.y, innerTop.z}}, {
          domain::Point3{rootCorners[0].x, rootCorners[0].y, rootCorners[0].z},
          domain::Point3{rootCorners[1].x, rootCorners[1].y, rootCorners[1].z},
          domain::Point3{rootCorners[2].x, rootCorners[2].y, rootCorners[2].z},
          domain::Point3{rootCorners[3].x, rootCorners[3].y, rootCorners[3].z}}};
      joiner.spansJoint = false;
    }
  }
}

double joinerSurfaceY(const domain::RibDefinition& rib, const double x,
                      const bool upperSurface) {
  const auto outline = rib.profile.resampled(81);
  const std::size_t leading = outline.size() / 2;
  std::vector<domain::Point2> surface;
  if (upperSurface) {
    for (std::size_t i = 0; i <= leading; ++i) {
      const auto& point = outline[leading - i];
      surface.push_back({point.x * rib.chord, point.y * rib.chord});
    }
  } else {
    for (std::size_t i = leading; i < outline.size(); ++i) {
      const auto& point = outline[i];
      surface.push_back({point.x * rib.chord, point.y * rib.chord});
    }
  }
  if (x <= surface.front().x) return surface.front().y;
  if (x >= surface.back().x) return surface.back().y;
  const auto after = std::lower_bound(surface.begin(), surface.end(), x,
      [](const domain::Point2 point, const double value) { return point.x < value; });
  const auto before = std::prev(after);
  const double t = (x - before->x) / (after->x - before->x);
  return before->y + t * (after->y - before->y);
}

double joinerCamberY(const domain::RibDefinition& rib, const double x) {
  return 0.5 * (joinerSurfaceY(rib, x, true) +
                joinerSurfaceY(rib, x, false));
}

domain::Point2 joinerCamberCenter(const domain::RibDefinition& rib,
                                  const double fraction) {
  const double x = fraction * rib.chord;
  return {x, joinerCamberY(rib, x)};
}

void addConfiguredJoiners(const std::vector<WingPanelData>& panelData,
                          std::vector<domain::StructuredWing>& panels,
                          const std::vector<double>& ribThicknesses) {
  if (panelData.size() != panels.size() || panels.size() != ribThicknesses.size())
    throw std::invalid_argument("Joiner assembly requires matching panel data");
  const auto materialName = [](const int material) {
    switch (material) {
      case 0: return std::string{"CF"};
      case 1: return std::string{"Aluminum"};
      case 2: return std::string{"Steel"};
      default: return std::string{"Fiberglass"};
    }
  };
  enum class JoinerHoleHalf { Both, Positive, Negative };
  const auto axisIntersection = [](const domain::RibDefinition& rib,
                                   const ModelPoint origin,
                                   const ModelPoint unit) {
    const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const double projection = unit.y * std::cos(plane) + unit.z * std::sin(plane);
    if (std::abs(projection) < 1.0e-9)
      throw std::invalid_argument("Joiner axis is parallel to a rib");
    const double t = (std::cos(plane) * (rib.spanPosition - origin.y) +
        std::sin(plane) * (rib.dihedralHeight - origin.z)) / projection;
    return localSectionPoint(rib,
        {origin.x + unit.x * t, origin.y + unit.y * t, origin.z + unit.z * t});
  };
  const auto addHole = [&](domain::StructuredWing& wing, const std::size_t ribIndex,
                           const ModelPoint origin, const ModelPoint unit,
                           const double diameter,
                           const JoinerHoleHalf half = JoinerHoleHalf::Both) {
    auto& structuredRib = wing.ribs[ribIndex];
    const auto center = axisIntersection(structuredRib.rib, origin, unit);
    const double plane = structuredRib.rib.ribPlaneAngleDegrees *
        std::numbers::pi / 180.0;
    const double projection =
        unit.y * std::cos(plane) + unit.z * std::sin(plane);
    auto hole = circularCut(
        center, diameter / std::max(0.25, std::abs(projection)));
    if (half == JoinerHoleHalf::Positive)
      structuredRib.positiveHalfBooleanHoles.push_back(std::move(hole));
    else if (half == JoinerHoleHalf::Negative)
      structuredRib.negativeHalfBooleanHoles.push_back(std::move(hole));
    else
      structuredRib.booleanHoles.push_back(std::move(hole));
  };
  const auto balancedJointPoint =
      [&](const domain::StructuredWing& wing,
          const std::size_t jointRibIndex,
          const std::size_t adjoiningRibIndex,
          const ModelPoint basePoint,
          const ModelPoint unit) {
    const auto& jointRib = wing.ribs[jointRibIndex].rib;
    const auto& adjoiningRib = wing.ribs[adjoiningRibIndex].rib;
    const auto baseLocal = localSectionPoint(jointRib, basePoint);
    const auto raisedPoint = modelSectionPoint(
        jointRib, {baseLocal.x, baseLocal.y + 1.0});
    const ModelPoint raise{
        raisedPoint.x - basePoint.x,
        raisedPoint.y - basePoint.y,
        raisedPoint.z - basePoint.z};
    const auto pointAt = [&](const double offset) {
      return ModelPoint{
          basePoint.x + raise.x * offset,
          basePoint.y + raise.y * offset,
          basePoint.z + raise.z * offset};
    };
    const auto imbalance = [&](const double offset) {
      const auto candidate = pointAt(offset);
      const auto jointCenter =
          axisIntersection(jointRib, candidate, unit);
      const auto adjoiningCenter =
          axisIntersection(adjoiningRib, candidate, unit);
      const double jointOffset =
          jointCenter.y - joinerCamberY(jointRib, jointCenter.x);
      const double adjoiningOffset =
          adjoiningCenter.y -
          joinerCamberY(adjoiningRib, adjoiningCenter.x);
      return jointOffset + adjoiningOffset;
    };
    double offset = 0.0;
    for (int iteration = 0; iteration < 4; ++iteration) {
      const double value = imbalance(offset);
      constexpr double probe = 0.1;
      const double derivative =
          (imbalance(offset + probe) - value) / probe;
      if (std::abs(derivative) < 1.0e-9) break;
      offset -= value / derivative;
    }
    return pointAt(offset);
  };
  const auto averagePoint = [](const ModelPoint first,
                               const ModelPoint second) {
    return ModelPoint{
        0.5 * (first.x + second.x),
        0.5 * (first.y + second.y),
        0.5 * (first.z + second.z)};
  };
  const auto addCircularSide = [&](domain::StructuredWing& wing,
                                   const std::size_t jointRib,
                                   const std::size_t secondRib,
                                   const double ribThickness,
                                   const ModelPoint jointPoint,
                                   const ModelPoint unit,
                                   const double od, const double id,
                                   const domain::SpanMemberKind kind,
                                   const std::string& name,
                                   const std::string& annotationName,
                                   const double endExtension,
                                   const bool mirrorInAssembly,
                                   const bool reflectAcrossCenter = false,
                                   const bool annotateOnBothPlanHalves = false,
                                   const bool annotateOnMirroredPlanHalf = false,
                                   const JoinerHoleHalf holeHalf =
                                       JoinerHoleHalf::Both) {
    if (od <= 0.0 || id < 0.0 || (kind == domain::SpanMemberKind::Tube && id >= od))
      throw std::invalid_argument(name + " requires OD greater than ID");
    const auto firstCutRib = std::min(jointRib, secondRib);
    const auto lastCutRib = std::max(jointRib, secondRib);
    for (std::size_t ribIndex = firstCutRib; ribIndex <= lastCutRib; ++ribIndex)
      addHole(wing, ribIndex, jointPoint, unit, od, holeHalf);
    const auto& second = wing.ribs[secondRib].rib;
    const double plane = second.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const double projection = unit.y * std::cos(plane) + unit.z * std::sin(plane);
    const double centerT = (std::cos(plane) * (second.spanPosition - jointPoint.y) +
        std::sin(plane) * (second.dihedralHeight - jointPoint.z)) / projection;
    const double startOffset = second.ribThicknessStartFactor * ribThickness;
    const double endOffset = (second.ribThicknessStartFactor + 1.0) * ribThickness;
    const double farT = std::max(centerT + startOffset / projection,
                                 centerT + endOffset / projection);
    const auto& joint = wing.ribs[jointRib].rib;
    const double jointPlane = joint.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const double jointProjection = unit.y * std::cos(jointPlane) +
        unit.z * std::sin(jointPlane);
    const double jointCenterT = (std::cos(jointPlane) * (joint.spanPosition - jointPoint.y) +
        std::sin(jointPlane) * (joint.dihedralHeight - jointPoint.z)) / jointProjection;
    const double jointStartT = jointCenterT +
        joint.ribThicknessStartFactor * ribThickness / jointProjection;
    const double jointEndT = jointCenterT +
        (joint.ribThicknessStartFactor + 1.0) * ribThickness / jointProjection;
    const double nearT = std::min(jointStartT, jointEndT);
    domain::JoinerPart joiner;
    joiner.name = name;
    joiner.annotationName = annotationName;
    joiner.annotateOnBothPlanHalves = annotateOnBothPlanHalves;
    joiner.annotateOnMirroredPlanHalf = annotateOnMirroredPlanHalf;
    joiner.kind = kind;
    joiner.outerDiameter = od;
    joiner.innerDiameter = kind == domain::SpanMemberKind::Tube ? id : 0.0;
    joiner.hasExplicitEndpoints = true;
    joiner.mirrorInAssembly = mirrorInAssembly;
    joiner.innerEndpoint = {jointPoint.x + unit.x * (nearT - endExtension),
                            jointPoint.y + unit.y * (nearT - endExtension),
                            jointPoint.z + unit.z * (nearT - endExtension)};
    joiner.outerEndpoint = {jointPoint.x + unit.x * (farT + endExtension),
                            jointPoint.y + unit.y * (farT + endExtension),
                            jointPoint.z + unit.z * (farT + endExtension)};
    if (reflectAcrossCenter) {
      joiner.innerEndpoint.y = -joiner.innerEndpoint.y;
      joiner.outerEndpoint.y = -joiner.outerEndpoint.y;
    }
    wing.joiners.push_back(std::move(joiner));
  };
  const auto addStraightCenterFixed = [&](domain::StructuredWing& wing,
                                          const double ribThickness,
                                          const std::size_t secondRibIndex,
                                          const double fraction,
                                          const double od, const double id,
                                          const domain::SpanMemberKind kind,
                                          const std::string& name) {
    if (od <= 0.0 || id < 0.0 || (kind == domain::SpanMemberKind::Tube && id >= od))
      throw std::invalid_argument(name + " requires OD greater than ID");
    if (secondRibIndex >= wing.ribs.size())
      throw std::invalid_argument("A center fixed joiner requires two ribs");
    const auto& joint = wing.ribs.front().rib;
    const auto& second = wing.ribs[secondRibIndex].rib;
    const ModelPoint axis{0.0, 1.0, 0.0};
    const auto jointPoint = balancedJointPoint(
        wing, 0, secondRibIndex,
        modelSectionPoint(joint, joinerCamberCenter(joint, fraction)), axis);
    for (std::size_t ribIndex = 0; ribIndex <= secondRibIndex; ++ribIndex)
      addHole(wing, ribIndex, jointPoint, axis, od);
    const double plane = second.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const double projection = std::cos(plane);
    const double centerT =
        (std::cos(plane) * (second.spanPosition - jointPoint.y) +
         std::sin(plane) * (second.dihedralHeight - jointPoint.z)) /
        projection;
    const double startOffset = second.ribThicknessStartFactor * ribThickness;
    const double endOffset = (second.ribThicknessStartFactor + 1.0) * ribThickness;
    const double farT = std::max(centerT + startOffset / projection,
                                 centerT + endOffset / projection);
    const double halfLength = std::abs(jointPoint.y + farT);
    domain::JoinerPart joiner;
    joiner.name = name;
    joiner.kind = kind;
    joiner.outerDiameter = od;
    joiner.innerDiameter = kind == domain::SpanMemberKind::Tube ? id : 0.0;
    joiner.hasExplicitEndpoints = true;
    joiner.mirrorInAssembly = false;
    joiner.annotateOnBothPlanHalves = true;
    joiner.innerEndpoint = {jointPoint.x, -halfLength, jointPoint.z};
    joiner.outerEndpoint = {jointPoint.x, halfLength, jointPoint.z};
    wing.joiners.push_back(std::move(joiner));
  };
  const auto addAcrossJointPin = [&](const std::size_t panelIndex,
                                     const ModelPoint jointPoint,
                                     const ModelPoint unit,
                                     const double diameter,
                                     const std::string& name,
                                     const std::string& annotationName) {
    auto& outer = panels[panelIndex];
    const auto faceRange = [&](const domain::RibDefinition& rib,
                               const double thickness) {
      const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
      const double projection = unit.y * std::cos(plane) + unit.z * std::sin(plane);
      if (std::abs(projection) < 1.0e-9)
        throw std::invalid_argument("Alignment pin axis is parallel to a joint rib");
      const double centerT = (std::cos(plane) * (rib.spanPosition - jointPoint.y) +
          std::sin(plane) * (rib.dihedralHeight - jointPoint.z)) / projection;
      const double first = centerT + rib.ribThicknessStartFactor * thickness / projection;
      const double second = centerT +
          (rib.ribThicknessStartFactor + 1.0) * thickness / projection;
      return std::pair{std::min(first, second), std::max(first, second)};
    };
    addHole(outer, 0, jointPoint, unit, diameter);
    auto range = faceRange(outer.ribs[0].rib, ribThicknesses[panelIndex]);
    if (panelIndex == 0) {
      range = {std::min(range.first, -range.second),
               std::max(range.second, -range.first)};
    } else {
      auto& inner = panels[panelIndex - 1];
      const std::size_t jointRib = inner.ribs.size() - 1;
      addHole(inner, jointRib, jointPoint, unit, diameter);
      const auto innerRange = faceRange(inner.ribs[jointRib].rib,
          ribThicknesses[panelIndex - 1]);
      range = {std::min(range.first, innerRange.first),
               std::max(range.second, innerRange.second)};
    }
    domain::JoinerPart pin;
    pin.name = name;
    pin.annotationName = annotationName;
    pin.annotateOnBothPlanHalves = panelIndex == 0;
    pin.kind = domain::SpanMemberKind::Rod;
    pin.outerDiameter = diameter;
    pin.hasExplicitEndpoints = true;
    pin.mirrorInAssembly = panelIndex != 0;
    pin.innerEndpoint = {jointPoint.x + unit.x * (range.first - 5.0),
                         jointPoint.y + unit.y * (range.first - 5.0),
                         jointPoint.z + unit.z * (range.first - 5.0)};
    pin.outerEndpoint = {jointPoint.x + unit.x * (range.second + 5.0),
                         jointPoint.y + unit.y * (range.second + 5.0),
                         jointPoint.z + unit.z * (range.second + 5.0)};
    outer.joiners.push_back(std::move(pin));
  };
  const auto addJointSideComponent = [&](domain::StructuredWing& wing,
                                         const std::size_t jointRib,
                                         const double ribThickness,
                                         const ModelPoint jointPoint,
                                         const ModelPoint inward,
                                         const double od, const double id,
                                         const domain::SpanMemberKind kind,
                                         const std::string& name,
                                         const std::string& annotationName,
                                         const bool mirrorInAssembly,
                                         const bool reflectAcrossCenter = false,
                                         const bool annotateOnBothPlanHalves = false,
                                         const bool annotateOnMirroredPlanHalf = false,
                                         const JoinerHoleHalf holeHalf =
                                             JoinerHoleHalf::Both) {
    if (od <= 0.0 || id < 0.0 || (kind == domain::SpanMemberKind::Tube && id >= od))
      throw std::invalid_argument(name + " requires OD greater than ID");
    addHole(wing, jointRib, jointPoint, inward, od, holeHalf);
    const auto& rib = wing.ribs[jointRib].rib;
    const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const double projection = inward.y * std::cos(plane) + inward.z * std::sin(plane);
    if (std::abs(projection) < 1.0e-9)
      throw std::invalid_argument(name + " axis is parallel to its joint rib");
    const double first = rib.ribThicknessStartFactor * ribThickness / projection;
    const double second = (rib.ribThicknessStartFactor + 1.0) *
        ribThickness / projection;
    const double farFace = std::max(first, second);
    domain::JoinerPart component;
    component.name = name;
    component.annotationName = annotationName;
    component.annotateOnBothPlanHalves = annotateOnBothPlanHalves;
    component.annotateOnMirroredPlanHalf = annotateOnMirroredPlanHalf;
    component.kind = kind;
    component.outerDiameter = od;
    component.innerDiameter = kind == domain::SpanMemberKind::Tube ? id : 0.0;
    component.hasExplicitEndpoints = true;
    component.mirrorInAssembly = mirrorInAssembly;
    component.innerEndpoint = {jointPoint.x, jointPoint.y, jointPoint.z};
    component.outerEndpoint = {jointPoint.x + inward.x * (farFace + 5.0),
                               jointPoint.y + inward.y * (farFace + 5.0),
                               jointPoint.z + inward.z * (farFace + 5.0)};
    if (reflectAcrossCenter) {
      component.innerEndpoint.y = -component.innerEndpoint.y;
      component.outerEndpoint.y = -component.outerEndpoint.y;
    }
    wing.joiners.push_back(std::move(component));
  };
  const auto findWoodSpar = [](const domain::StructuredWing& wing,
                               const int verticalLocation,
                               const double fraction) -> const domain::SpanMember* {
    for (const auto& member : wing.members) {
      if (member.kind != domain::SpanMemberKind::Rectangular || member.carbonFiber ||
          member.verticalLocation != verticalLocation ||
          !member.name.starts_with("Spar ") || member.centers.empty())
        continue;
      const double memberFraction = member.centers.front().x /
          wing.ribs.front().rib.chord;
      if (std::abs(memberFraction - fraction) < 1.0e-6) return &member;
    }
    return nullptr;
  };
  const auto woodProfile = [](const domain::StructuredWing& wing,
                              const domain::SpanMember& top,
                              const domain::SpanMember& bottom,
                              const std::size_t ribIndex,
                              const double fraction,
                              const double thickness) {
    const double centerX = fraction * wing.ribs[ribIndex].rib.chord;
    const double bottomY = bottom.centers[ribIndex].y + bottom.height * 0.5;
    const double topY = top.centers[ribIndex].y - top.height * 0.5;
    const double half = thickness * 0.5;
    return std::array<domain::Point2, 4>{{
        {centerX - half, bottomY}, {centerX + half, bottomY},
        {centerX + half, topY}, {centerX - half, topY}}};
  };
  const auto globalProfile = [](const domain::RibDefinition& rib,
                                const std::array<domain::Point2, 4>& profile,
                                const double offset) {
    std::array<domain::Point3, 4> result{};
    for (std::size_t i = 0; i < profile.size(); ++i) {
      const auto point = modelSectionPoint(rib, profile[i], offset);
      result[i] = {point.x, point.y, point.z};
    }
    return result;
  };
  const auto removeJointWeb = [](domain::StructuredWing& wing, const bool atRoot) {
    if (wing.ribs.size() < 2) return;
    const std::size_t bay = atRoot ? 1 : wing.ribs.size() - 1;
    wing.shearWebs.erase(std::remove_if(wing.shearWebs.begin(), wing.shearWebs.end(),
        [bay](const domain::ShearWebPart& web) { return web.bayIndex == bay; }),
        wing.shearWebs.end());
  };
  const auto addWood = [&](const std::size_t panelIndex,
                           const FixedJoinerData& data,
                           const std::size_t number) {
    auto& outer = panels[panelIndex];
    if (outer.ribs.size() < 2) throw std::invalid_argument("Wood fixed joiner requires two ribs");
    const std::size_t secondRibIndex =
        panelIndex == 0 && panelData[0].addRib1a ? 2 : 1;
    if (secondRibIndex >= outer.ribs.size())
      throw std::invalid_argument("Wood fixed joiner requires Rib 2");
    const double fraction = data.chordLocationPercent / 100.0;
    const auto* outerTop = findWoodSpar(outer, 0, fraction);
    const auto* outerBottom = findWoodSpar(outer, 1, fraction);
    if (!outerTop || !outerBottom)
      throw std::invalid_argument("Joiner " + std::to_string(number) +
          " requires matching top and bottom wood spars");
    domain::JoinerPart joiner;
    joiner.name = "Joiner " + std::to_string(number);
    joiner.kind = domain::SpanMemberKind::Rectangular;
    joiner.outerDiameter = data.woodThickness;
    joiner.stopRibIndex = secondRibIndex;
    joiner.annotateOnBothPlanHalves = panelIndex == 0;
    for (std::size_t ribIndex = 0; ribIndex <= secondRibIndex; ++ribIndex) {
      joiner.rectangularProfiles.push_back(woodProfile(
          outer, *outerTop, *outerBottom, ribIndex, fraction, data.woodThickness));
      // The joiner passes through the joint rib and, when present at the
      // center, Rib 1a. It terminates against the inner face of Rib 2, so the
      // terminal rib must remain whole.
      if (ribIndex < secondRibIndex) {
        const auto& profile = joiner.rectangularProfiles.back();
        std::vector<domain::Point2> splitCutout{
            profile.begin(), profile.end()};
        outer.ribs[ribIndex].ribSplitCutouts.push_back(splitCutout);
        outer.ribs[ribIndex].booleanCutouts.push_back(
            std::move(splitCutout));
      }
    }
    removeJointWeb(outer, true);
    if (panelIndex == 0) {
      const auto second = globalProfile(outer.ribs[secondRibIndex].rib,
          joiner.rectangularProfiles[secondRibIndex],
          (outer.ribs[secondRibIndex].rib.ribThicknessStartFactor + 1.0) *
              ribThicknesses[0]);
      const auto joint = globalProfile(outer.ribs[0].rib, joiner.rectangularProfiles[0], 0.0);
      std::array<domain::Point3, 4> mirroredSecond{};
      std::array<domain::Point3, 4> mirroredJoint{};
      for (std::size_t i = 0; i < 4; ++i) {
        mirroredSecond[i] = {second[i].x, -second[i].y, second[i].z};
        mirroredJoint[i] = {joint[i].x, -joint[i].y, joint[i].z};
      }
      joiner.innerRectangularProfiles = {mirroredSecond, mirroredJoint};
      joiner.mirrorInAssembly = false;
      const auto bottom = [](const std::array<domain::Point3, 4>& profile) {
        return 0.5 * (profile[0].z + profile[1].z);
      };
      const auto top = [](const std::array<domain::Point3, 4>& profile) {
        return 0.5 * (profile[2].z + profile[3].z);
      };
      joiner.dxfOutline = {{mirroredSecond[0].y, bottom(mirroredSecond)},
          {joint[0].y, bottom(joint)}, {second[0].y, bottom(second)},
          {second[0].y, top(second)}, {joint[0].y, top(joint)},
          {mirroredSecond[0].y, top(mirroredSecond)}};
    } else {
      auto& inner = panels[panelIndex - 1];
      const auto* innerTop = findWoodSpar(inner, 0, fraction);
      const auto* innerBottom = findWoodSpar(inner, 1, fraction);
      if (!innerTop || !innerBottom)
        throw std::invalid_argument("Joiner " + std::to_string(number) +
            " requires matching adjoining-panel wood spars");
      const std::size_t jointIndex = inner.ribs.size() - 1;
      const std::size_t secondIndex = inner.ribs.size() - 2;
      const auto jointProfile = woodProfile(inner, *innerTop, *innerBottom,
          jointIndex, fraction, data.woodThickness);
      const auto secondProfile = woodProfile(inner, *innerTop, *innerBottom,
          secondIndex, fraction, data.woodThickness);
      std::vector<domain::Point2> splitCutout{
          jointProfile.begin(), jointProfile.end()};
      inner.ribs[jointIndex].ribSplitCutouts.push_back(splitCutout);
      inner.ribs[jointIndex].booleanCutouts.push_back(
          std::move(splitCutout));
      removeJointWeb(inner, false);
      const auto innerSecond = globalProfile(inner.ribs[secondIndex].rib, secondProfile,
          (inner.ribs[secondIndex].rib.ribThicknessStartFactor + 1.0) *
              ribThicknesses[panelIndex - 1]);
      const auto innerJoint = globalProfile(inner.ribs[jointIndex].rib, jointProfile, 0.0);
      const auto outerSecond = globalProfile(outer.ribs[secondRibIndex].rib,
          joiner.rectangularProfiles[secondRibIndex],
          outer.ribs[secondRibIndex].rib.ribThicknessStartFactor *
              ribThicknesses[panelIndex]);
      joiner.innerRectangularProfiles = {innerSecond, innerJoint};
      joiner.mirrorInAssembly = true;
      const auto bottom = [](const std::array<domain::Point3, 4>& profile) {
        return 0.5 * (profile[0].z + profile[1].z);
      };
      const auto top = [](const std::array<domain::Point3, 4>& profile) {
        return 0.5 * (profile[2].z + profile[3].z);
      };
      joiner.dxfOutline = {{innerSecond[0].y, bottom(innerSecond)},
          {innerJoint[0].y, bottom(innerJoint)},
          {outerSecond[0].y, bottom(outerSecond)},
          {outerSecond[0].y, top(outerSecond)},
          {innerJoint[0].y, top(innerJoint)},
          {innerSecond[0].y, top(innerSecond)}};
    }
    outer.joiners.push_back(std::move(joiner));
  };

  std::size_t fixedNumber = 0;
  std::size_t woodNumber = 0;
  std::size_t removableNumber = 0;
  std::size_t alignmentNumber = 0;
  for (std::size_t panelIndex = 0; panelIndex < panels.size(); ++panelIndex) {
    const auto& data = panelData[panelIndex];
    if (data.joinerPanelMode < 0) continue;
    auto& outer = panels[panelIndex];
    if (outer.ribs.size() < 2) throw std::invalid_argument("Joiners require at least two ribs");
    const auto& rootRib = outer.ribs.front().rib;
    const double plane = rootRib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const ModelPoint outward{0.0, std::cos(plane), std::sin(plane)};
    const bool centerJoint = panelIndex == 0;
    const std::size_t structuralSecondRib =
        centerJoint && data.addRib1a ? 2 : 1;
    if (structuralSecondRib >= outer.ribs.size())
      throw std::invalid_argument("Center joiners require Rib 2");
    const auto addPair = [&](const double fraction,
                             const domain::SpanMemberKind thisKind,
                             const double thisOd, const double thisId,
                             const std::string& thisName,
                             const domain::SpanMemberKind adjoiningKind,
                             const double adjoiningOd, const double adjoiningId,
                             const std::string& adjoiningName,
                             const std::string& thisAnnotation,
                             const std::string& adjoiningAnnotation,
                             const double extension,
                             const bool mirrorIdenticalCenterPair = false) {
      const auto localCenter = joinerCamberCenter(rootRib, fraction);
      const auto baseJoint = modelSectionPoint(rootRib, localCenter);
      // At the wing center the two panel halves rise away from a horizontal
      // joiner. Use one cross-wing axis, as fixed joiners do, rather than
      // reflecting the panel-normal axis into a V-shaped pair.
      const ModelPoint pairAxis = centerJoint
          ? ModelPoint{0.0, 1.0, 0.0} : outward;
      ModelPoint joint;
      if (centerJoint) {
        joint = balancedJointPoint(
            outer, 0, structuralSecondRib, baseJoint, pairAxis);
      } else {
        auto& inner = panels[panelIndex - 1];
        const ModelPoint inward{-outward.x, -outward.y, -outward.z};
        joint = averagePoint(
            balancedJointPoint(
                outer, 0, structuralSecondRib, baseJoint, outward),
            balancedJointPoint(
                inner, inner.ribs.size() - 1, inner.ribs.size() - 2,
                baseJoint, inward));
      }
      if (centerJoint && !mirrorIdenticalCenterPair)
        for (std::size_t ribIndex = 0;
             ribIndex <= structuralSecondRib; ++ribIndex)
          outer.ribs[ribIndex].uniqueHalfPartVariants = true;
      addCircularSide(outer, 0, structuralSecondRib, ribThicknesses[panelIndex],
          joint, pairAxis,
          thisOd, thisId, thisKind, thisName, thisAnnotation, extension,
          !centerJoint || mirrorIdenticalCenterPair, false, false, false,
          centerJoint && !mirrorIdenticalCenterPair
              ? JoinerHoleHalf::Positive : JoinerHoleHalf::Both);
      if (centerJoint && mirrorIdenticalCenterPair) return;
      if (centerJoint) {
        const std::size_t thisPartIndex = outer.joiners.size() - 1;
        addCircularSide(outer, 0, structuralSecondRib,
            ribThicknesses[panelIndex], joint, pairAxis,
            adjoiningOd, adjoiningId, adjoiningKind, adjoiningName,
            adjoiningAnnotation, extension, false, true, false, true,
            JoinerHoleHalf::Negative);
        auto& thisPart = outer.joiners[thisPartIndex];
        auto& adjoiningPart = outer.joiners.back();
        if (thisKind == domain::SpanMemberKind::Rod &&
            adjoiningKind == domain::SpanMemberKind::Tube)
          thisPart.innerEndpoint = adjoiningPart.outerEndpoint;
        else if (adjoiningKind == domain::SpanMemberKind::Rod &&
                 thisKind == domain::SpanMemberKind::Tube)
          adjoiningPart.innerEndpoint = thisPart.outerEndpoint;
      } else {
        auto& inner = panels[panelIndex - 1];
        const ModelPoint inward{-outward.x, -outward.y, -outward.z};
        const std::size_t thisPartIndex = outer.joiners.size() - 1;
        addCircularSide(inner, inner.ribs.size() - 1, inner.ribs.size() - 2,
            ribThicknesses[panelIndex - 1], joint, inward,
            adjoiningOd, adjoiningId, adjoiningKind, adjoiningName,
            adjoiningAnnotation, extension, true);
        auto& thisPart = outer.joiners[thisPartIndex];
        auto& adjoiningPart = inner.joiners.back();
        if (thisKind == domain::SpanMemberKind::Rod &&
            adjoiningKind == domain::SpanMemberKind::Tube)
          thisPart.innerEndpoint = adjoiningPart.outerEndpoint;
        else if (adjoiningKind == domain::SpanMemberKind::Rod &&
                 thisKind == domain::SpanMemberKind::Tube)
          adjoiningPart.innerEndpoint = thisPart.outerEndpoint;
      }
    };
    if (data.joinerPanelMode == 1) {
      for (const auto& fixed : data.fixedJoiners) {
        ++fixedNumber;
        if (fixed.material == 0) {
          addWood(panelIndex, fixed, ++woodNumber);
          continue;
        }
        const double fraction = fixed.chordLocationPercent / 100.0;
        const bool tube = fixed.material == 1 && fixed.carbonType == 0;
        const double od = fixed.material == 2 ? fixed.steelRodOd :
            tube ? fixed.carbonTubeOd : fixed.carbonRodOd;
        const double id = tube ? fixed.carbonTubeId : 0.0;
        const auto kind = tube ? domain::SpanMemberKind::Tube : domain::SpanMemberKind::Rod;
        const std::string name = "Fixed Joiner " + std::to_string(fixedNumber) + " " +
            (fixed.material == 2 ? "Steel" : "CF") + (tube ? " Tube" : " Rod");
        if (centerJoint)
          addStraightCenterFixed(outer, ribThicknesses[panelIndex],
              structuralSecondRib, fraction, od, id, kind, name);
        else {
          const auto localCenter = joinerCamberCenter(rootRib, fraction);
          const auto baseJoint = modelSectionPoint(rootRib, localCenter);
          auto& inner = panels[panelIndex - 1];
          const ModelPoint inward{-outward.x, -outward.y, -outward.z};
          const auto joint = averagePoint(
              balancedJointPoint(outer, 0, 1, baseJoint, outward),
              balancedJointPoint(
                  inner, inner.ribs.size() - 1, inner.ribs.size() - 2,
                  baseJoint, inward));
          const std::size_t outerCount = outer.joiners.size();
          addCircularSide(outer, 0, 1, ribThicknesses[panelIndex],
              joint, outward, od, id, kind, name, {}, 0.0, true);
          auto outerHalf = std::move(outer.joiners.back());
          outer.joiners.resize(outerCount);

          const std::size_t innerCount = inner.joiners.size();
          addCircularSide(inner, inner.ribs.size() - 1,
              inner.ribs.size() - 2, ribThicknesses[panelIndex - 1],
              joint, inward, od, id, kind, name, {}, 0.0, true);
          auto innerHalf = std::move(inner.joiners.back());
          inner.joiners.resize(innerCount);

          outerHalf.innerEndpoint = innerHalf.outerEndpoint;
          outerHalf.name = name;
          outerHalf.annotationName.clear();
          outerHalf.mirrorInAssembly = true;
          outer.joiners.push_back(std::move(outerHalf));
        }
      }
      continue;
    }
    for (const auto& removable : data.removableJoiners) {
      const double fraction = removable.chordLocationPercent / 100.0;
      if (removable.kind == 0 || removable.alignmentMode == 0) {
        const bool alignment = removable.kind == 1;
        const std::size_t number = alignment ? ++alignmentNumber : ++removableNumber;
        const bool thisSleeve = removable.thisPanelPart == 0;
        const bool adjoiningSleeve = alignment
            ? !thisSleeve : removable.adjoiningPanelPart == 0;
        const double thisOd = thisSleeve ? removable.thisSleeveOd : removable.thisRodOd;
        const double adjoiningOd = adjoiningSleeve
            ? removable.adjoiningSleeveOd : removable.adjoiningRodOd;
        const double thisId = thisSleeve
            ? (adjoiningSleeve ? removable.thisRodOd : adjoiningOd) : 0.0;
        const double adjoiningId = adjoiningSleeve
            ? (thisSleeve ? removable.adjoiningRodOd : thisOd) : 0.0;
        const int thisMaterial = thisSleeve ? removable.thisSleeveMaterial : removable.thisRodMaterial == 0 ? 0 : 2;
        const int adjoiningMaterial = adjoiningSleeve
            ? removable.adjoiningSleeveMaterial
            : removable.adjoiningRodMaterial == 0 ? 0 : 2;
        const std::string prefix = alignment ? "Alignment Pin " : "Removable Joiner ";
        const auto thisKind = thisSleeve
            ? domain::SpanMemberKind::Tube : domain::SpanMemberKind::Rod;
        const auto adjoiningKind = adjoiningSleeve
            ? domain::SpanMemberKind::Tube : domain::SpanMemberKind::Rod;
        const auto thisName = prefix + std::to_string(number) +
            " This Panel " + materialName(thisMaterial);
        const auto adjoiningName = prefix + std::to_string(number) +
            " Adjoining Panel " + materialName(adjoiningMaterial);
        const std::string annotationPrefix = alignment
            ? "Alignment Pin " : "Joiner ";
        const std::string thisPart = alignment
            ? (thisSleeve ? "Sleeve" : "Pin")
            : (thisSleeve ? "Sleeve" : "Rod");
        const std::string adjoiningPart = alignment
            ? (adjoiningSleeve ? "Sleeve" : "Pin")
            : (adjoiningSleeve ? "Sleeve" : "Rod");
        const auto thisAnnotation = annotationPrefix + std::to_string(number) +
            "\n" + materialName(thisMaterial) + " " + thisPart;
        const auto adjoiningAnnotation = annotationPrefix +
            std::to_string(number) + "\n" +
            materialName(adjoiningMaterial) + " " + adjoiningPart;
        const auto jointPoint = modelSectionPoint(rootRib,
            joinerCamberCenter(rootRib, fraction));
        if (!alignment) {
          addPair(fraction, thisKind, thisOd, thisId, thisName,
              adjoiningKind, adjoiningOd, adjoiningId, adjoiningName,
              thisAnnotation, adjoiningAnnotation, 0.0);
        } else if (centerJoint) {
          for (std::size_t ribIndex = 0;
               ribIndex <= structuralSecondRib; ++ribIndex)
            outer.ribs[ribIndex].uniqueHalfPartVariants = true;
          addJointSideComponent(outer, 0, ribThicknesses[panelIndex], jointPoint,
              outward, thisOd, thisId, thisKind, thisName, thisAnnotation,
              false, false, false, false, JoinerHoleHalf::Positive);
          addJointSideComponent(outer, 0, ribThicknesses[panelIndex], jointPoint,
              outward, adjoiningOd, adjoiningId, adjoiningKind,
              adjoiningName, adjoiningAnnotation, false, true, false, true,
              JoinerHoleHalf::Negative);
        } else {
          addJointSideComponent(outer, 0, ribThicknesses[panelIndex], jointPoint,
              outward, thisOd, thisId, thisKind, thisName, thisAnnotation, true);
          auto& inner = panels[panelIndex - 1];
          const ModelPoint inward{-outward.x, -outward.y, -outward.z};
          addJointSideComponent(inner, inner.ribs.size() - 1,
              ribThicknesses[panelIndex - 1], jointPoint, inward,
              adjoiningOd, adjoiningId, adjoiningKind, adjoiningName,
              adjoiningAnnotation, true);
        }
      } else {
        ++alignmentNumber;
        const auto center = modelSectionPoint(rootRib, joinerCamberCenter(rootRib, fraction));
        const int pinMaterial = removable.pinMaterial == 0 ? 0 : 2;
        const std::string name = "Alignment Pin " + std::to_string(alignmentNumber) + " " +
            materialName(pinMaterial);
        const std::string annotation = "Alignment Pin " +
            std::to_string(alignmentNumber) + "\n" +
            materialName(pinMaterial) + " Pin";
        addAcrossJointPin(
            panelIndex, center, outward, removable.pinOd, name, annotation);
      }
    }
  }
}

struct PreviewComputation {
  std::vector<domain::StructuredWing> structuredPanels;
  std::vector<std::vector<domain::RibDefinition>> ribSets;
  std::vector<double> dihedrals;
  std::vector<double> thicknesses;
  std::vector<geometry::PanelBuildTimings> panelBuildTimings;
  geometry::MaterialShapeSet materialShapes;
  double fullSpan{};
  double fullArea{};
  double aspectRatio{};
  double taperRatio{};
  double panelPreparationMs{};
  double jointAndNamingMs{};
  double panelGeometryMs{};
  double mirrorAssemblyMs{};
  double finalizationMs{};
  double measuredWorkerMs{};
  double workerTotalMs{};
  double workerReturnOverheadMs{};
  std::chrono::steady_clock::time_point queuedForGuiAt{};
};

struct UpdateCancelled final {};

PreviewComputation computePreview(const std::vector<WingPanelData>& panels,
                                  const DisplayUnit unit,
                                  const std::shared_ptr<std::atomic_bool>& cancellation,
                                  const std::function<void(int, const QString&)>& progress,
                                  const std::size_t requestedWorkerThreads = 0) {
  const auto alignmentError = woodJoinerSparAlignmentError(panels);
  if (!alignmentError.isEmpty())
    throw std::invalid_argument(alignmentError.toStdString());
  const auto checkpoint = [&] {
    if (cancellation->load()) throw UpdateCancelled{};
  };
  const auto millisecondsSince = [](const std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
  };
  const auto totalStart = std::chrono::steady_clock::now();
  PreviewComputation result;
  result.dihedrals.reserve(panels.size());
  for (const auto& panel : panels) result.dihedrals.push_back(panel.dihedral);
  progress(2, "Calculating panel assembly angles...");
  const auto assemblyAngles = domain::calculatePanelAssemblyAngles(result.dihedrals);
  std::vector<double> panelTwists;
  panelTwists.reserve(panels.size());
  for (const auto& panel : panels) panelTwists.push_back(panel.twist);
  const auto twistRanges = domain::calculatePanelTwistRanges(panelTwists);
  struct PanelOrigin { double x{}, y{}, z{}; };
  std::vector<PanelOrigin> origins(panels.size());
  double originX = 0.0, originY = 0.0, originZ = 0.0;
  double halfArea = 0.0;
  for (std::size_t panelIndex = 0; panelIndex < panels.size(); ++panelIndex) {
    const auto& d = panels[panelIndex];
    const auto& angles = assemblyAngles[panelIndex];
    origins[panelIndex] = {originX, originY, originZ};
    const double radians = angles.panelInclinationDegrees * std::numbers::pi / 180.0;
    halfArea += d.panelSpan * (d.rootChord + d.tipChord) * 0.5;
    originX += d.sweep;
    originY += std::cos(radians) * d.panelSpan;
    originZ += std::sin(radians) * d.panelSpan;
  }
  result.structuredPanels.resize(panels.size());
  result.ribSets.resize(panels.size());
  result.thicknesses.resize(panels.size());
  std::atomic_size_t structuresCompleted{0};
  const std::size_t workerThreadCount = requestedWorkerThreads > 0
      ? std::clamp(requestedWorkerThreads, std::size_t{1},
                   maximumGeometryThreadCount())
      : maximumGeometryThreadCount();
  runCpuParallelTasks(panels.size(), workerThreadCount,
      [&](const std::size_t panelIndex) {
      checkpoint();
      const auto& d = panels[panelIndex];
      const auto& angles = assemblyAngles[panelIndex];
      const auto origin = origins[panelIndex];
      domain::WingParameters p;
      p.halfSpan = d.panelSpan; p.rootChord = d.rootChord; p.tipChord = d.tipChord;
      p.sweep = d.sweep; p.dihedralDegrees = 0.0;
      p.rootTwistDegrees = twistRanges[panelIndex].rootTwistDegrees;
      p.tipTwistDegrees = twistRanges[panelIndex].tipTwistDegrees;
      p.ribThickness = d.ribThickness; p.ribCount = static_cast<std::size_t>(d.ribCount);
      auto ribs = domain::generateRibs(p, d.rootAirfoil, d.tipAirfoil);
      if (d.addRib1a) {
        const double t = 0.5 / static_cast<double>(d.ribCount - 1);
        ribs.insert(ribs.begin() + 1, {p.halfSpan * t,
            p.rootChord + t * (p.tipChord - p.rootChord), p.sweep * t, 0.0,
            p.rootTwistDegrees + t * (p.tipTwistDegrees - p.rootTwistDegrees),
            0.0, -0.5,
            domain::AirfoilProfile::interpolate(d.rootAirfoil, d.tipAirfoil, t)});
      }
      const double radians = angles.panelInclinationDegrees * std::numbers::pi / 180.0;
      for (std::size_t i = 0; i < ribs.size(); ++i) {
        const double localSpan = ribs[i].spanPosition;
        ribs[i].leadingEdgeOffset += origin.x;
        ribs[i].spanPosition = origin.y + std::cos(radians) * localSpan;
        ribs[i].dihedralHeight = origin.z + std::sin(radians) * localSpan;
        ribs[i].ribPlaneAngleDegrees = i == 0 ? angles.rootRibAngleDegrees :
            i + 1 == ribs.size() ? angles.tipRibAngleDegrees :
            angles.intermediateRibAngleDegrees;
        ribs[i].ribThicknessStartFactor = i == 0 ? 0.0 :
            i + 1 == ribs.size() ? -1.0 : -0.5;
      }
      const auto structure = structureParametersFor(d, unit,
          angles.panelInclinationDegrees,
          panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
          panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
          panelIndex != 0);
      try {
        result.structuredPanels[panelIndex] = domain::applyWingStructure(ribs, structure);
      } catch (const domain::EdgeHeightError& exception) {
        throw PanelEdgeHeightError{
            formatEdgeHeightError(exception, d, unit, panelIndex), panelIndex,
            exception.edgeName(), exception.cutHeightMm()};
      } catch (const std::exception& exception) {
        throw std::runtime_error(
            "Panel " + std::to_string(panelIndex + 1) + ": " + exception.what());
      }
      result.ribSets[panelIndex] = std::move(ribs);
      result.thicknesses[panelIndex] = d.ribThickness;
      const auto completed = ++structuresCompleted;
      progress(4 + static_cast<int>(12 * completed / panels.size()),
          QString{"Built %1 of %2 panel structures"}.arg(completed).arg(panels.size()));
      checkpoint();
  });
  result.panelPreparationMs = millisecondsSince(totalStart);
  checkpoint();
  const auto jointStart = std::chrono::steady_clock::now();
  progress(18, "Connecting panel joiners...");
  for (std::size_t i = 1; i < result.structuredPanels.size(); ++i) {
    checkpoint();
    addInnerPanelJoinerCuts(result.structuredPanels[i - 1], result.structuredPanels[i],
        panels[i - 1].ribThickness, panels[i].ribThickness);
  }
  addConfiguredJoiners(panels, result.structuredPanels, result.thicknesses);
  std::size_t ribNumber = 1;
  std::size_t shearWebNumber = 1;
  for (std::size_t panelIndex = 0; panelIndex < result.structuredPanels.size(); ++panelIndex) {
    auto& structured = result.structuredPanels[panelIndex];
    const auto panelPartNumber = std::to_string(panelIndex + 1);
    const bool hasRib1a = panelIndex == 0 && panels[panelIndex].addRib1a;
    for (std::size_t ribIndex = 0; ribIndex < structured.ribs.size(); ++ribIndex) {
      structured.ribs[ribIndex].name = hasRib1a && ribIndex == 1
          ? "R1a" : "R" + std::to_string(ribNumber++);
      if (structured.ribs[ribIndex].uniqueHalfPartVariants ||
          !structured.ribs[ribIndex].positiveHalfBooleanHoles.empty() ||
          !structured.ribs[ribIndex].negativeHalfBooleanHoles.empty()) {
        structured.ribs[ribIndex].positiveHalfName =
            structured.ribs[ribIndex].name + " Right";
        structured.ribs[ribIndex].negativeHalfName =
            structured.ribs[ribIndex].name + " Left";
      }
    }
    const auto& angles = assemblyAngles[panelIndex];
    domain::addRiblets(structured, structureParametersFor(
        panels[panelIndex], unit, angles.panelInclinationDegrees,
        panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
        panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
        panelIndex != 0));
    for (auto& web : structured.shearWebs)
      web.name = "SW" + std::to_string(shearWebNumber++);
    for (auto& member : structured.profiledMembers) {
      if (member.name.find("leading edge") != std::string::npos)
        member.name = "LE" + panelPartNumber;
      else if (member.name.find("trailing edge") != std::string::npos)
        member.name = "TE" + panelPartNumber;
    }
    for (auto& member : structured.members)
      if (member.name.find("leading edge") != std::string::npos)
        member.name = "LE" + panelPartNumber;
    for (std::size_t stockIndex = 0; stockIndex < structured.sheetStockParts.size(); ++stockIndex) {
      auto& stock = structured.sheetStockParts[stockIndex];
      stock.name = "TE" + panelPartNumber;
      if (structured.sheetStockParts.size() > 1)
        stock.name += "-" + std::to_string(stockIndex + 1);
    }
    for (auto& joiner : structured.joiners)
      if (joiner.name == "Center spar wood joiner")
        joiner.name = "J" + panelPartNumber;
  }
  result.jointAndNamingMs = millisecondsSince(jointStart);
  checkpoint();
  const auto geometryStart = std::chrono::steady_clock::now();
  progress(20, "Building panel geometry...");
  std::vector<TopoDS_Shape> panelShapes(result.structuredPanels.size());
  std::vector<geometry::MaterialShapeSet> panelMaterialShapes(
      result.structuredPanels.size());
  result.panelBuildTimings.resize(result.structuredPanels.size());
  const std::size_t totalGeometryWorkers = workerThreadCount;
  const std::size_t panelConcurrency = std::min(
      result.structuredPanels.size(), totalGeometryWorkers);
  std::vector<std::size_t> panelWorkerBudgets(
      result.structuredPanels.size(), 1);
  if (result.structuredPanels.size() <= totalGeometryWorkers) {
    const std::size_t base =
        totalGeometryWorkers / result.structuredPanels.size();
    const std::size_t remainder =
        totalGeometryWorkers % result.structuredPanels.size();
    for (std::size_t panelIndex = 0;
         panelIndex < panelWorkerBudgets.size(); ++panelIndex)
      panelWorkerBudgets[panelIndex] =
          base + (panelIndex < remainder ? 1 : 0);
  }

  constexpr int geometryProgressStart = 20;
  constexpr int geometryProgressSpan = 65;
  std::mutex panelProgressMutex;
  std::vector<int> panelProgressValues(result.structuredPanels.size(), 0);
  const auto reportPanelProgress =
      [&](const std::size_t panelIndex, const int localValue,
          const QString& localMessage) {
        std::scoped_lock lock{panelProgressMutex};
        panelProgressValues[panelIndex] = std::max(
            panelProgressValues[panelIndex], std::clamp(localValue, 0, 100));
        const int combined = std::accumulate(
            panelProgressValues.begin(), panelProgressValues.end(), 0);
        const int globalValue = geometryProgressStart +
            geometryProgressSpan * combined /
                static_cast<int>(100 * panelProgressValues.size());
        progress(globalValue,
            QString{"Panel %1 of %2: %3"}
                .arg(panelIndex + 1)
                .arg(result.structuredPanels.size())
                .arg(localMessage));
      };

  std::atomic_size_t nextPanel{0};
  std::vector<std::future<void>> panelWorkers;
  panelWorkers.reserve(panelConcurrency);
  for (std::size_t worker = 0; worker < panelConcurrency; ++worker)
    panelWorkers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t panelIndex = nextPanel.fetch_add(1);
        if (panelIndex >= result.structuredPanels.size()) return;
        checkpoint();
        const auto& angles = assemblyAngles[panelIndex];
        const auto structure = structureParametersFor(
            panels[panelIndex], unit, angles.panelInclinationDegrees,
            panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
            panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
            panelIndex != 0);
        reportPanelProgress(
            panelIndex, 0, "Laying out rib lightening holes");
        domain::addRibLighteningHoles(
            result.structuredPanels[panelIndex], structure,
            [&](const std::size_t completed, const std::size_t total) {
              const int layoutProgress = total == 0 ? 5 :
                  static_cast<int>(5 * completed / total);
              reportPanelProgress(panelIndex, layoutProgress,
                  QString{"Laying out rib lightening holes (%1/%2)"}
                      .arg(completed).arg(total));
            }, panelWorkerBudgets[panelIndex]);
        reportPanelProgress(panelIndex, 5, "Building panel geometry");
        const auto panelProgress = [&](const int localValue,
                                       const std::string& localMessage) {
          reportPanelProgress(panelIndex,
              5 + 95 * std::clamp(localValue, 0, 100) / 100,
              QString::fromStdString(localMessage));
        };
        try {
          panelShapes[panelIndex] = geometry::buildStructuredWingPreview(
              result.structuredPanels[panelIndex],
              result.thicknesses[panelIndex],
              &result.panelBuildTimings[panelIndex],
              &panelMaterialShapes[panelIndex], panelProgress,
              panelWorkerBudgets[panelIndex]);
        } catch (const std::exception& exception) {
          throw std::runtime_error(
              "Panel " + std::to_string(panelIndex + 1) + ": " +
              exception.what());
        }
        reportPanelProgress(panelIndex, 100, "Panel geometry complete");
        checkpoint();
      }
    }));
  for (auto& worker : panelWorkers) worker.get();
  result.panelGeometryMs = millisecondsSince(geometryStart);
  checkpoint();
  const auto mirrorStart = std::chrono::steady_clock::now();
  progress(88, "Copying mirrored wing presentation...");
  result.materialShapes = geometry::assembleMirroredMaterialPreview(panelMaterialShapes);
  result.mirrorAssemblyMs = millisecondsSince(mirrorStart);
  checkpoint();
  const auto finalizationStart = std::chrono::steady_clock::now();
  progress(94, "Finalizing preview...");
  result.fullSpan = result.structuredPanels.back().ribs.back().rib.spanPosition * 2.0;
  result.fullArea = halfArea * 2.0;
  result.aspectRatio = result.fullSpan * result.fullSpan / result.fullArea;
  result.taperRatio = panels.back().tipChord / panels.front().rootChord;
  result.finalizationMs = millisecondsSince(finalizationStart);
  result.measuredWorkerMs = millisecondsSince(totalStart);
  progress(96, "Geometry ready for display");
  return result;
}

} // namespace

int runJoinerBackendRegression() {
  const auto buildPanels = [](const std::vector<WingPanelData>& panels) {
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    return computePreview(panels, DisplayUnit::Millimeters, cancellation,
        [](const int, const QString&) {});
  };
  const auto build = [&](const WingPanelData& panel) {
    return buildPanels({panel});
  };
  const auto holeCenter = [](const std::vector<domain::Point2>& hole) {
    domain::Point2 center{};
    for (const auto point : hole) {
      center.x += point.x;
      center.y += point.y;
    }
    center.x /= static_cast<double>(hole.size());
    center.y /= static_cast<double>(hole.size());
    return center;
  };
  const auto heightFromCamber =
      [&](const domain::StructuredRib& rib,
          const std::vector<domain::Point2>& hole) {
    const auto center = holeCenter(hole);
    return center.y - joinerCamberY(rib.rib, center.x);
  };
  const auto hasBalancedJoinerHoles =
      [&](const domain::StructuredRib& joint,
          const domain::StructuredRib& adjoining) {
    if (joint.booleanHoles.empty() || adjoining.booleanHoles.empty())
      return false;
    const double jointOffset =
        heightFromCamber(joint, joint.booleanHoles.back());
    const double adjoiningOffset =
        heightFromCamber(adjoining, adjoining.booleanHoles.back());
    return jointOffset > 1.0e-4 && adjoiningOffset < -1.0e-4 &&
        std::abs(jointOffset + adjoiningOffset) < 1.0e-5;
  };
  const auto hasBalancedPositiveHalfJoinerHoles =
      [&](const domain::StructuredRib& joint,
          const domain::StructuredRib& adjoining) {
    if (joint.positiveHalfBooleanHoles.empty() ||
        adjoining.positiveHalfBooleanHoles.empty())
      return false;
    const double jointOffset = heightFromCamber(
        joint, joint.positiveHalfBooleanHoles.back());
    const double adjoiningOffset = heightFromCamber(
        adjoining, adjoining.positiveHalfBooleanHoles.back());
    return jointOffset > 1.0e-4 && adjoiningOffset < -1.0e-4 &&
        std::abs(jointOffset + adjoiningOffset) < 1.0e-5;
  };
  try {
    WingPanelData teSheetingProject;
    teSheetingProject.panelSpan = 698.5;
    teSheetingProject.rootChord = 254.0;
    teSheetingProject.tipChord = 152.4;
    teSheetingProject.sweep = 25.4;
    teSheetingProject.dihedral = 4.0;
    teSheetingProject.ribThickness = 2.38125;
    teSheetingProject.ribCount = 11;
    SparDefaults topTeSpar;
    topTeSpar.chordLocationPercent = 25.0;
    topTeSpar.tipChordLocationPercent = 25.0;
    topTeSpar.verticalLocation = 0;
    topTeSpar.material = 0;
    topTeSpar.type = 2;
    topTeSpar.woodHeight = 3.175;
    topTeSpar.woodWidth = 6.35;
    auto bottomTeSpar = topTeSpar;
    bottomTeSpar.verticalLocation = 1;
    bottomTeSpar.type = 0;
    teSheetingProject.spars = {topTeSpar, bottomTeSpar};
    teSheetingProject.leadingEdgeType = 2;
    teSheetingProject.leadingEdgeWidth = 4.7625;
    teSheetingProject.leadingEdgeHeight = 15.875;
    teSheetingProject.leTopSheet = true;
    teSheetingProject.leBottomSheet = true;
    teSheetingProject.leTopSheetThickness = 1.5875;
    teSheetingProject.leBottomSheetThickness = 1.5875;
    teSheetingProject.leTopSheetStopRib = 2;
    teSheetingProject.leBottomSheetStopRib = 2;
    teSheetingProject.teTopSheet = true;
    teSheetingProject.teBottomSheet = true;
    teSheetingProject.teTopSheetThickness = 1.5875;
    teSheetingProject.teBottomSheetThickness = 1.5875;
    teSheetingProject.teTopSheetStopRib = 2;
    teSheetingProject.teBottomSheetStopRib = 2;
    teSheetingProject.topTeSheeting = true;
    teSheetingProject.topTeSheetingWidth = 25.4;
    teSheetingProject.topTeSheetingThickness = 1.5875;
    teSheetingProject.joinerPanelMode = 1;
    FixedJoinerData firstWoodJoiner;
    firstWoodJoiner.material = 0;
    firstWoodJoiner.chordLocationPercent = 25.0;
    firstWoodJoiner.woodThickness = 3.175;
    auto secondWoodJoiner = firstWoodJoiner;
    secondWoodJoiner.carbonType = 1;
    teSheetingProject.fixedJoiners = {
        firstWoodJoiner, secondWoodJoiner};
    bool namedWoodJoinerCollision = false;
    try {
      static_cast<void>(build(teSheetingProject));
    } catch (const std::exception& error) {
      const std::string message = error.what();
      namedWoodJoinerCollision =
          message.find("Joiner collision") != std::string::npos &&
          message.find("Joiner 1") != std::string::npos &&
          message.find("Joiner 2") != std::string::npos;
    }
    if (!namedWoodJoinerCollision) return 120;
    // Keep the TE-sheeting solid regression below as a valid model. Its
    // purpose does not require the second, now-invalid overlapping joiner.
    teSheetingProject.fixedJoiners.resize(1);
    const auto teSheetingPreview = build(teSheetingProject);
    if (teSheetingPreview.structuredPanels.empty() ||
        teSheetingPreview.materialShapes.parts.empty())
      return 20;

    WingPanelData fixed;
    fixed.panelSpan = 160.0; fixed.rootChord = fixed.tipChord = 180.0;
    fixed.sweep = 0.0; fixed.dihedral = 6.0; fixed.ribCount = 3;
    fixed.spars.clear(); fixed.joinerPanelMode = 1;
    fixed.fixedJoiners = {FixedJoinerData{}};
    const auto fixedPreview = build(fixed);
    if (fixedPreview.structuredPanels.front().joiners.size() != 1 ||
        fixedPreview.structuredPanels.front().joiners.front().mirrorInAssembly ||
        std::abs(fixedPreview.structuredPanels.front().joiners.front().innerEndpoint.x -
                 fixedPreview.structuredPanels.front().joiners.front().outerEndpoint.x) > 1.0e-8 ||
        std::abs(fixedPreview.structuredPanels.front().joiners.front().innerEndpoint.z -
                 fixedPreview.structuredPanels.front().joiners.front().outerEndpoint.z) > 1.0e-8 ||
        fixedPreview.structuredPanels.front().joiners.front().innerEndpoint.y >= 0.0 ||
        fixedPreview.structuredPanels.front().joiners.front().outerEndpoint.y <= 0.0 ||
        fixedPreview.structuredPanels.front().ribs[0].booleanHoles.empty() ||
        fixedPreview.structuredPanels.front().ribs[1].booleanHoles.empty() ||
        !hasBalancedJoinerHoles(
            fixedPreview.structuredPanels.front().ribs[0],
            fixedPreview.structuredPanels.front().ribs[1]))
      return 1;

    WingPanelData fixedInner = fixed;
    fixedInner.joinerPanelMode = -1;
    fixedInner.fixedJoiners.clear();
    WingPanelData fixedOuter = fixed;
    const auto panelFixedPreview = buildPanels({fixedInner, fixedOuter});
    const auto& panelFixedInner =
        panelFixedPreview.structuredPanels.front();
    const auto& panelFixedOuter =
        panelFixedPreview.structuredPanels.back();
    if (!panelFixedInner.joiners.empty() ||
        panelFixedOuter.joiners.size() != 1 ||
        !panelFixedOuter.joiners.front().hasExplicitEndpoints ||
        panelFixedOuter.joiners.front().name.find("This Panel") !=
            std::string::npos ||
        panelFixedOuter.joiners.front().name.find("Adjoining Panel") !=
            std::string::npos ||
        panelFixedOuter.joiners.front().innerEndpoint.y >=
            panelFixedOuter.ribs.front().rib.spanPosition ||
        panelFixedOuter.joiners.front().outerEndpoint.y <=
            panelFixedOuter.ribs.front().rib.spanPosition ||
        !hasBalancedJoinerHoles(
            panelFixedOuter.ribs.front(), panelFixedOuter.ribs[1]) ||
        !hasBalancedJoinerHoles(
            panelFixedInner.ribs.back(),
            panelFixedInner.ribs[panelFixedInner.ribs.size() - 2]))
      return 44;

    WingPanelData fixedWithRib1a = fixed;
    fixedWithRib1a.addRib1a = true;
    const auto fixedWithRib1aPreview = build(fixedWithRib1a);
    const auto& fixedWithRib1aWing =
        fixedWithRib1aPreview.structuredPanels.front();
    if (fixedWithRib1aWing.joiners.size() != 1 ||
        fixedWithRib1aWing.joiners.front().outerEndpoint.y <=
            fixedWithRib1aWing.ribs[2].rib.spanPosition ||
        fixedWithRib1aWing.ribs[0].booleanHoles.empty() ||
        fixedWithRib1aWing.ribs[1].booleanHoles.empty() ||
        fixedWithRib1aWing.ribs[2].booleanHoles.empty())
      return 41;

    WingPanelData removable = fixed;
    removable.joinerPanelMode = 0; removable.fixedJoiners.clear();
    RemovableJoinerData sleeveRod;
    sleeveRod.adjoiningPanelPart = 0;
    sleeveRod.thisRodOd = 5.0;
    sleeveRod.adjoiningSleeveOd = 7.0;
    RemovableJoinerData alignment;
    alignment.kind = 1; alignment.chordLocationPercent = 70;
    removable.removableJoiners = {sleeveRod, alignment};
    const auto removablePreview = build(removable);
    if (removablePreview.structuredPanels.front().joiners.size() != 3 ||
        removablePreview.structuredPanels.front().joiners[0].kind != domain::SpanMemberKind::Rod ||
        removablePreview.structuredPanels.front().joiners[1].kind != domain::SpanMemberKind::Tube ||
        removablePreview.structuredPanels.front().ribs[0]
                .positiveHalfBooleanHoles.size() != 1 ||
        removablePreview.structuredPanels.front().ribs[0]
                .negativeHalfBooleanHoles.size() != 1)
      return 2;
    const auto& centerRemovableWing =
        removablePreview.structuredPanels.front();
    const double centerJoinerX =
        centerRemovableWing.joiners[0].innerEndpoint.x;
    const double centerJoinerZ =
        centerRemovableWing.joiners[0].innerEndpoint.z;
    for (std::size_t joinerIndex = 0; joinerIndex < 2; ++joinerIndex) {
      const auto& joiner = centerRemovableWing.joiners[joinerIndex];
      if (std::abs(joiner.innerEndpoint.x - centerJoinerX) > 1.0e-8 ||
          std::abs(joiner.outerEndpoint.x - centerJoinerX) > 1.0e-8 ||
          std::abs(joiner.innerEndpoint.z - centerJoinerZ) > 1.0e-8 ||
          std::abs(joiner.outerEndpoint.z - centerJoinerZ) > 1.0e-8)
        return 121;
    }
    const auto sameEndpoint = [](const domain::Point3& first,
                                 const domain::Point3& second) {
      return std::abs(first.x - second.x) < 1.0e-8 &&
          std::abs(first.y - second.y) < 1.0e-8 &&
          std::abs(first.z - second.z) < 1.0e-8;
    };
    if (!sameEndpoint(centerRemovableWing.joiners[0].innerEndpoint,
                      centerRemovableWing.joiners[1].outerEndpoint) ||
        centerRemovableWing.joiners[0].innerEndpoint.y >= 0.0 ||
        centerRemovableWing.joiners[0].outerEndpoint.y <= 0.0)
      return 123;
    if (!hasBalancedPositiveHalfJoinerHoles(
            centerRemovableWing.ribs[0], centerRemovableWing.ribs[1]))
      return 122;
    const auto holeWidth = [](const std::vector<domain::Point2>& hole) {
      const auto [minimum, maximum] = std::minmax_element(
          hole.begin(), hole.end(),
          [](const domain::Point2 left, const domain::Point2 right) {
            return left.x < right.x;
          });
      return maximum->x - minimum->x;
    };
    if (holeWidth(removablePreview.structuredPanels.front().ribs[0]
                      .negativeHalfBooleanHoles.front()) <=
        holeWidth(removablePreview.structuredPanels.front().ribs[0]
                      .positiveHalfBooleanHoles.front()) * 1.15)
      return 45;
    if (removablePreview.structuredPanels.front().joiners[0].annotationName.find(
            "Joiner 1\nCF Rod") == std::string::npos ||
        removablePreview.structuredPanels.front().joiners[1].annotationName.find(
            "Joiner 1\nAluminum Sleeve") == std::string::npos ||
        removablePreview.structuredPanels.front().joiners[2].annotationName.find(
            "Alignment Pin 1\nCF Pin") == std::string::npos ||
        removablePreview.structuredPanels.front().joiners[0]
            .annotateOnBothPlanHalves ||
        removablePreview.structuredPanels.front().joiners[0]
            .annotateOnMirroredPlanHalf ||
        !removablePreview.structuredPanels.front().joiners[1]
            .annotateOnMirroredPlanHalf)
      return 24;

    WingPanelData removableInner = fixed;
    removableInner.joinerPanelMode = -1;
    removableInner.fixedJoiners.clear();
    WingPanelData removableOuter = removable;
    removableOuter.removableJoiners = {sleeveRod};
    const auto panelRemovablePreview =
        buildPanels({removableInner, removableOuter});
    const auto& panelRemovableInner =
        panelRemovablePreview.structuredPanels.front();
    const auto& panelRemovableOuter =
        panelRemovablePreview.structuredPanels.back();
    if (panelRemovableInner.joiners.size() != 1 ||
        panelRemovableOuter.joiners.size() != 1 ||
        !sameEndpoint(panelRemovableOuter.joiners.front().innerEndpoint,
                      panelRemovableInner.joiners.front().outerEndpoint) ||
        !hasBalancedJoinerHoles(
            panelRemovableOuter.ribs.front(),
            panelRemovableOuter.ribs[1]) ||
        !hasBalancedJoinerHoles(
            panelRemovableInner.ribs.back(),
            panelRemovableInner.ribs[
                panelRemovableInner.ribs.size() - 2]))
      return 46;

    WingPanelData removableWithRib1a = removable;
    removableWithRib1a.addRib1a = true;
    removableWithRib1a.removableJoiners = {sleeveRod};
    const auto removableWithRib1aPreview = build(removableWithRib1a);
    const auto& removableWithRib1aWing =
        removableWithRib1aPreview.structuredPanels.front();
    if (removableWithRib1aWing.joiners.size() != 2 ||
        removableWithRib1aWing.joiners[0].outerEndpoint.y <=
            removableWithRib1aWing.ribs[2].rib.spanPosition ||
        removableWithRib1aWing.ribs[0].positiveHalfBooleanHoles.empty() ||
        removableWithRib1aWing.ribs[1].positiveHalfBooleanHoles.empty() ||
        removableWithRib1aWing.ribs[2].positiveHalfBooleanHoles.empty() ||
        removableWithRib1aWing.ribs[0].negativeHalfBooleanHoles.empty() ||
        removableWithRib1aWing.ribs[1].negativeHalfBooleanHoles.empty() ||
        removableWithRib1aWing.ribs[2].negativeHalfBooleanHoles.empty() ||
        removableWithRib1aWing.ribs[0].positiveHalfName != "R1 Right" ||
        removableWithRib1aWing.ribs[0].negativeHalfName != "R1 Left" ||
        removableWithRib1aWing.ribs[1].positiveHalfName != "R1a Right" ||
        removableWithRib1aWing.ribs[1].negativeHalfName != "R1a Left" ||
        removableWithRib1aWing.ribs[2].positiveHalfName != "R2 Right" ||
        removableWithRib1aWing.ribs[2].negativeHalfName != "R2 Left")
      return 43;

    removable.removableJoiners = {alignment};
    const auto pinHolePreview = build(removable);
    const auto& pinHoleWing = pinHolePreview.structuredPanels.front();
    if (pinHoleWing.joiners.size() != 1 ||
        pinHoleWing.joiners.front().innerEndpoint.y >= 0.0 ||
        pinHoleWing.joiners.front().outerEndpoint.y <= 0.0 ||
        pinHoleWing.ribs.front().booleanHoles.empty() ||
        !pinHoleWing.ribs[1].booleanHoles.empty())
      return 22;

    WingPanelData pinHoleWithRib1a = removable;
    pinHoleWithRib1a.addRib1a = true;
    const auto pinHoleWithRib1aPreview = build(pinHoleWithRib1a);
    const auto& pinHoleWithRib1aWing =
        pinHoleWithRib1aPreview.structuredPanels.front();
    if (pinHoleWithRib1aWing.joiners.size() != 1 ||
        pinHoleWithRib1aWing.ribs[0].booleanHoles.empty() ||
        !pinHoleWithRib1aWing.ribs[1].booleanHoles.empty() ||
        !pinHoleWithRib1aWing.ribs[2].booleanHoles.empty() ||
        std::abs(pinHoleWithRib1aWing.joiners.front().outerEndpoint.y) >=
            pinHoleWithRib1aWing.ribs[1].rib.spanPosition)
      return 42;

    alignment.alignmentMode = 0;
    alignment.thisPanelPart = 0;
    removable.removableJoiners = {alignment};
    const auto sleevePinPreview = build(removable);
    const auto& sleevePinWing = sleevePinPreview.structuredPanels.front();
    if (sleevePinWing.joiners.size() != 2 ||
        std::abs(sleevePinWing.joiners[0].innerEndpoint.y) > 1.0e-8 ||
        sleevePinWing.joiners[0].outerEndpoint.y <= 5.0 ||
        sleevePinWing.joiners[0].outerEndpoint.y >= sleevePinWing.ribs[1].rib.spanPosition ||
        std::abs(sleevePinWing.joiners[1].innerEndpoint.y) > 1.0e-8 ||
        sleevePinWing.joiners[1].outerEndpoint.y >= -5.0 ||
        sleevePinWing.joiners[1].outerEndpoint.y <= -sleevePinWing.ribs[1].rib.spanPosition ||
        !sleevePinWing.ribs[1].booleanHoles.empty() ||
        sleevePinWing.ribs[0].positiveHalfBooleanHoles.empty() ||
        sleevePinWing.ribs[0].negativeHalfBooleanHoles.empty() ||
        sleevePinWing.ribs[0].positiveHalfName != "R1 Right" ||
        sleevePinWing.ribs[0].negativeHalfName != "R1 Left" ||
        sleevePinWing.ribs[1].positiveHalfName != "R2 Right" ||
        sleevePinWing.ribs[1].negativeHalfName != "R2 Left" ||
        sleevePinWing.joiners[0].annotationName.find("This Panel") !=
            std::string::npos ||
        sleevePinWing.joiners[1].annotationName.find("Adjoining Panel") !=
            std::string::npos ||
        sleevePinWing.joiners[0].annotateOnBothPlanHalves ||
        !sleevePinWing.joiners[1].annotateOnMirroredPlanHalf)
      return 23;

    sleeveRod.thisPanelPart = 0;
    sleeveRod.adjoiningPanelPart = 0;
    removable.removableJoiners = {sleeveRod};
    const auto twoSleevePreview = build(removable);
    if (twoSleevePreview.structuredPanels.front().joiners.size() != 2 ||
        twoSleevePreview.structuredPanels.front().joiners[0].kind != domain::SpanMemberKind::Tube ||
        twoSleevePreview.structuredPanels.front().joiners[1].kind != domain::SpanMemberKind::Tube)
      return 21;

    WingPanelData wood = fixed;
    wood.spars = {
        {35, 0, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
        {35, 1, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
        // Keep an unrelated internal rectangular cutout on every rib. This
        // guards against regressing wood-joiner slots back into closed holes.
        {60, 2, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
    wood.sparShearWebs = true;
    wood.addRib1a = true;
    wood.fixedJoiners.front().material = 0;
    const auto woodPreview = build(wood);
    const auto& woodWing = woodPreview.structuredPanels.front();
    if (woodWing.joiners.size() != 1) return 31;
    if (woodWing.joiners.front().stopRibIndex != 2 ||
        woodWing.joiners.front().rectangularProfiles.size() != 3 ||
        woodWing.ribs[0].ribSplitCutouts.size() != 1 ||
        woodWing.ribs[1].ribSplitCutouts.size() != 1 ||
        !woodWing.ribs[2].ribSplitCutouts.empty() ||
        woodWing.ribs[0].booleanCutouts.size() <=
            woodWing.ribs[0].ribSplitCutouts.size())
      return 32;
    if (woodWing.joiners.front().innerRectangularProfiles.size() != 2) return 33;
    if (woodWing.joiners.front().dxfOutline.size() != 6 ||
        woodWing.joiners.front().name != "Joiner 1") return 34;

    const auto exportsAsTwoPieces =
        [](const domain::StructuredRib& rib,
           const std::size_t expectedInternalCutoutCount) {
      if (rib.ribSplitCutouts.size() != 1) return false;
      const auto& slot = rib.ribSplitCutouts.front();
      const auto [slotMinimum, slotMaximum] = std::minmax_element(
          slot.begin(), slot.end(),
          [](const domain::Point2 left, const domain::Point2 right) {
            return left.x < right.x;
          });
      const auto drawing =
          domain::makeStructuredRibPartDrawing(rib, rib.name);
      bool hasLeadingPiece = false;
      bool hasTrailingPiece = false;
      for (const auto& path : drawing.paths) {
        if (path.layer != "RIB_OUTLINE" || path.points.empty()) continue;
        const auto [minimum, maximum] = std::minmax_element(
            path.points.begin(), path.points.end(),
            [](const domain::Point2 left, const domain::Point2 right) {
              return left.x < right.x;
            });
        if (maximum->x <= slotMinimum->x + 1.0e-8)
          hasLeadingPiece = true;
        else if (minimum->x >= slotMaximum->x - 1.0e-8)
          hasTrailingPiece = true;
        else
          return false;
      }
      const auto internalCutoutCount = std::count_if(
          drawing.paths.begin(), drawing.paths.end(),
          [](const domain::PartDrawingPath& path) {
            return path.layer == "RIB_HOLES";
          });
      return hasLeadingPiece && hasTrailingPiece &&
          static_cast<std::size_t>(internalCutoutCount) ==
              expectedInternalCutoutCount;
    };
    if (!exportsAsTwoPieces(woodWing.ribs[0], 1) ||
        !exportsAsTwoPieces(woodWing.ribs[1], 1))
      return 35;

    WingPanelData oneBayWood = wood;
    oneBayWood.addRib1a = false;
    const auto oneBayPreview = build(oneBayWood);
    const auto& oneBayWing = oneBayPreview.structuredPanels.front();
    if (oneBayWing.joiners.size() != 1 ||
        oneBayWing.joiners.front().stopRibIndex != 1 ||
        oneBayWing.joiners.front().rectangularProfiles.size() != 2 ||
        oneBayWing.ribs[0].ribSplitCutouts.size() != 1 ||
        !oneBayWing.ribs[1].ribSplitCutouts.empty() ||
        !exportsAsTwoPieces(oneBayWing.ribs[0], 1))
      return 36;

    WingPanelData panelJointInner = oneBayWood;
    panelJointInner.joinerPanelMode = -1;
    panelJointInner.fixedJoiners.clear();
    WingPanelData panelJointOuter = oneBayWood;
    const auto panelWoodPreview =
        buildPanels({panelJointInner, panelJointOuter});
    const auto& panelWoodInner =
        panelWoodPreview.structuredPanels.front();
    const auto& panelWoodOuter =
        panelWoodPreview.structuredPanels.back();
    if (!panelWoodInner.joiners.empty() ||
        panelWoodOuter.joiners.size() != 1 ||
        panelWoodOuter.joiners.front().innerRectangularProfiles.size() != 2 ||
        !panelWoodOuter.joiners.front().mirrorInAssembly ||
        panelWoodInner.ribs.back().ribSplitCutouts.size() != 1 ||
        !panelWoodInner.ribs[panelWoodInner.ribs.size() - 2]
             .ribSplitCutouts.empty() ||
        panelWoodOuter.ribs.front().ribSplitCutouts.size() != 1 ||
        !panelWoodOuter.ribs[1].ribSplitCutouts.empty() ||
        !exportsAsTwoPieces(panelWoodInner.ribs.back(), 1) ||
        !exportsAsTwoPieces(panelWoodOuter.ribs.front(), 1))
      return 37;
    const auto panelJoinerShapeCount = std::count_if(
        panelWoodPreview.materialShapes.parts.begin(),
        panelWoodPreview.materialShapes.parts.end(),
        [](const geometry::NamedPartShape& part) {
          return part.name.ends_with("Joiner 1");
        });
    if (panelJoinerShapeCount != 2) return 38;
  } catch (const std::exception& exception) {
    qCritical() << "Joiner backend regression exception:" << exception.what();
    std::fprintf(stderr, "Joiner backend regression exception: %s\n",
                 exception.what());
    return 4;
  } catch (...) {
    qCritical() << "Joiner backend regression exception: unknown";
    std::fprintf(stderr, "Joiner backend regression exception: unknown\n");
    return 4;
  }
  return 0;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow{parent} {
  updateWindowTitle();
  resize(1400, 860);
  buildMenus();

  auto* actionToolBar = new QToolBar{"Wing Actions", this};
  actionToolBar->setObjectName("wingActionToolBar");
  actionToolBar->setMovable(false);
  actionToolBar->setFloatable(false);
  actionToolBar->setAllowedAreas(Qt::TopToolBarArea);
  actionToolBar->setIconSize(QSize{24, 24});
  actionToolBar->setMinimumHeight(42);
  addToolBar(Qt::TopToolBarArea, actionToolBar);
  const auto addToolBarButton = [actionToolBar](
                                    const QString& text,
                                    const QString& objectName) {
    auto* button = new QPushButton{text};
    button->setObjectName(objectName);
    button->setMinimumSize(112, 32);
    actionToolBar->addWidget(button);
    return button;
  };
  updateButton_ = addToolBarButton("Generate Wing", "generateWingButton");
  generatePlanButton_ =
      addToolBarButton("Generate Plan", "generatePlanButton");
  generatePlanButton_->setEnabled(false);
  exportPlanButton_ =
      addToolBarButton("Export Plan PDF", "exportPlanPdfButton");
  exportPlanButton_->setEnabled(false);
  exportPartsButton_ =
      addToolBarButton("Export Parts", "exportPartsButton");
  exportPartsButton_->setEnabled(false);
  exportStepButton_ =
      addToolBarButton("Export STEP", "exportStepButton");
  exportStepButton_->setEnabled(false);

  QSettings settings;
  globalUnit_ = static_cast<DisplayUnit>(settings.value("defaults/globalUnit",
      static_cast<int>(installedDefaultDisplayUnit())).toInt());
  workerThreadCount_ = std::clamp(
      settings.value("defaults/numberOfThreads",
          static_cast<qulonglong>(maximumGeometryThreadCount()))
          .toULongLong(),
      qulonglong{1},
      static_cast<qulonglong>(maximumGeometryThreadCount()));

  auto* splitter = new QSplitter;
  auto* left = new QWidget;
  auto* leftLayout = new QVBoxLayout{left};
  leftLayout->setContentsMargins(8, 8, 8, 8);
  leftLayout->setSpacing(6);
  auto* panelCountRow = new QHBoxLayout;
  panelCountRow->addWidget(new QLabel{"Number of Wing Panels"});
  panelCount_ = new QSpinBox;
  panelCount_->setRange(1, 12);
  panelCount_->setValue(1);
  panelCountRow->addWidget(panelCount_);
  leftLayout->addLayout(panelCountRow);

  panelTabs_ = new QTabWidget;
  leftLayout->addWidget(panelTabs_, 1);
  metrics_ = new QLabel;
  metrics_->setObjectName("wingMetrics");
  metrics_->setWordWrap(true);
  leftLayout->addWidget(metrics_);

  auto* threeDimensionalPage = new QWidget;
  viewport_ = new OcctViewport{threeDimensionalPage};
  planViewport_ = new PlanViewport;
  auto* threeDimensionalLayout = new QVBoxLayout{threeDimensionalPage};
  threeDimensionalLayout->setContentsMargins(0, 0, 0, 4);
  threeDimensionalLayout->setSpacing(4);
  threeDimensionalLayout->addWidget(viewport_, 1);
  auto* viewButtons = new QHBoxLayout;
  viewButtons->addStretch();
  auto* fit = new QPushButton{"Fit View"};
  fit->setObjectName("fitViewButton");
  viewButtons->addWidget(fit);
  const auto addViewButton = [this, viewButtons](const QString& text,
                                                 const QString& objectName,
                                                 const CameraView cameraView) {
    auto* button = new QPushButton{text};
    button->setObjectName(objectName);
    viewButtons->addWidget(button);
    connect(button, &QPushButton::clicked, this,
        [this, cameraView] { setCameraView(cameraView); });
  };
  addViewButton("Reset", "viewResetButton", CameraView::Reset);
  addViewButton("Top", "viewTopButton", CameraView::Top);
  addViewButton("Bottom", "viewBottomButton", CameraView::Bottom);
  addViewButton("Front", "viewFrontButton", CameraView::Front);
  addViewButton("Back", "viewBackButton", CameraView::Back);
  addViewButton("Left", "viewLeftButton", CameraView::Left);
  addViewButton("Right", "viewRightButton", CameraView::Right);
  viewButtons->addStretch();
  threeDimensionalLayout->addLayout(viewButtons);
  graphicsTabs_ = new QTabWidget;
  graphicsTabs_->addTab(threeDimensionalPage, "3D View");
  graphicsTabs_->addTab(planViewport_, "Plan View");
  graphicsTabs_->setTabEnabled(1, false);
  splitter->addWidget(left);
  splitter->addWidget(graphicsTabs_);
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(1);
  splitter->setSizes({470, 930});
  splitter->setStretchFactor(1, 1);
  setCentralWidget(splitter);

  connect(panelCount_, &QSpinBox::valueChanged, this, [this](int count) { changePanelCount(count); });
  connect(updateButton_, &QPushButton::clicked, this, [this] { regeneratePreview(); });
  connect(fit, &QPushButton::clicked, this, [this] {
    const BusyCursor busy;
    if (graphicsTabs_->currentIndex() == 1) planViewport_->fitAll();
    else viewport_->fitAll();
  });
  connect(generatePlanButton_, &QPushButton::clicked, this, [this] { generatePlan(); });
  connect(exportPlanButton_, &QPushButton::clicked, this, [this] { exportPlanPdf(); });
  connect(exportPartsButton_, &QPushButton::clicked, this, [this] { exportRibs(); });
  connect(exportStepButton_, &QPushButton::clicked, this, [this] { exportStep(); });
  connect(graphicsTabs_, &QTabWidget::currentChanged, this, [this](const int index) {
    statusBar()->showMessage(index == 1
        ? "Plan: left drag to pan  |  Wheel: zoom"
        : "Left drag: orbit  |  Right drag: pan  |  Wheel: zoom");
  });

  rebuildPanelTabs(defaultPanelData(globalUnit_));
  updateProgress_ = new QProgressBar;
  updateProgress_->setRange(0, 100);
  updateProgress_->setMinimumWidth(210);
  updateProgress_->setMaximumWidth(300);
  updateProgress_->setTextVisible(true);
  updateProgress_->hide();
  cancelUpdateButton_ = new QPushButton{"Cancel"};
  cancelUpdateButton_->hide();
  statusBar()->addPermanentWidget(updateProgress_);
  statusBar()->addPermanentWidget(cancelUpdateButton_);
  connect(cancelUpdateButton_, &QPushButton::clicked, this, [this] {
    if (!updateCancellation_) return;
    updateCancellation_->store(true);
    cancelUpdateButton_->setEnabled(false);
    statusBar()->showMessage("Cancelling Update View after the current geometry operation...");
  });
  statusBar()->showMessage("Left drag: orbit  |  Right drag: pan  |  Wheel: zoom");
}

void MainWindow::buildMenus() {
  auto* file = menuBar()->addMenu("&File");
  auto* edit = menuBar()->addMenu("&Edit");
  auto* view = menuBar()->addMenu("&View");
  auto* help = menuBar()->addMenu("&Help");
  auto* newAction = file->addAction("&New");
  auto* openAction = file->addAction("&Open...");
  auto* saveAction = file->addAction("&Save");
  auto* saveAsAction = file->addAction("Save &As...");
  file->addSeparator();
  auto* exitAction = file->addAction("E&xit");
  auto* copyAction = edit->addAction("&Copy");
  auto* pasteAction = edit->addAction("&Paste");
  auto* defaultsAction = edit->addAction("&Defaults...");
  const auto addViewAction = [this, view](const QString& text,
                                         const QString& objectName,
                                         const CameraView cameraView) {
    auto* action = view->addAction(text);
    action->setObjectName(objectName);
    connect(action, &QAction::triggered, this,
        [this, cameraView] { setCameraView(cameraView); });
  };
  addViewAction("Reset", "viewResetAction", CameraView::Reset);
  view->addSeparator();
  addViewAction("Top", "viewTopAction", CameraView::Top);
  addViewAction("Bottom", "viewBottomAction", CameraView::Bottom);
  addViewAction("Front", "viewFrontAction", CameraView::Front);
  addViewAction("Back", "viewBackAction", CameraView::Back);
  addViewAction("Left", "viewLeftAction", CameraView::Left);
  addViewAction("Right", "viewRightAction", CameraView::Right);
  auto* helpAction = help->addAction("&Help");
  auto* aboutAction = help->addAction("&About");
  newAction->setShortcut(QKeySequence::New); openAction->setShortcut(QKeySequence::Open);
  saveAction->setShortcut(QKeySequence::Save); saveAsAction->setShortcut(QKeySequence::SaveAs);
  copyAction->setShortcut(QKeySequence::Copy); pasteAction->setShortcut(QKeySequence::Paste);
  exitAction->setShortcut(QKeySequence{Qt::ALT | Qt::Key_F4});
  defaultsAction->setShortcut(QKeySequence{Qt::CTRL | Qt::Key_Comma});
  helpAction->setShortcut(QKeySequence::HelpContents);
  connect(newAction, &QAction::triggered, this, [this] { newProject(); });
  connect(openAction, &QAction::triggered, this, [this] { openProject(); });
  connect(saveAction, &QAction::triggered, this, [this] { saveProject(); });
  connect(saveAsAction, &QAction::triggered, this, [this] { saveProjectAs(); });
  connect(exitAction, &QAction::triggered, this, &QWidget::close);
  connect(copyAction, &QAction::triggered, this, [this] { copyFocusedText(); });
  connect(pasteAction, &QAction::triggered, this, [this] { pasteFocusedText(); });
  connect(defaultsAction, &QAction::triggered, this, [this] { openDefaults(); });
  connect(helpAction, &QAction::triggered, this, [this] { openHelp(); });
  connect(aboutAction, &QAction::triggered, this, [this] { showAbout(); });
}

void MainWindow::setCameraView(const CameraView cameraView) {
  if (!viewport_) return;
  if (graphicsTabs_) graphicsTabs_->setCurrentIndex(0);
  viewport_->setCameraView(cameraView);
}

void MainWindow::exportStep() {
  if (currentMaterialShapes_.parts.empty() ||
      !generatePlanButton_ || !generatePlanButton_->isEnabled()) {
    QMessageBox::information(this, "Export STEP",
        "Generate the current 3D geometry with Update View before exporting STEP.");
    return;
  }
  QSettings settings;
  const QString directory = settings.value("lastDirectory").toString();
  const QString projectName = currentFile_.isEmpty()
      ? QString{"Untitled"} : QFileInfo{currentFile_}.completeBaseName();
  QString path = QFileDialog::getSaveFileName(this, "Export STEP Assembly",
      QDir{directory}.filePath(projectName + ".step"),
      "STEP files (*.step *.stp)");
  if (path.isEmpty()) return;
  if (QFileInfo{path}.suffix().isEmpty()) path += ".step";
  try {
    const BusyCursor busy;
    statusBar()->showMessage(
        QString{"Exporting STEP assembly to %1..."}.arg(path));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    geometry::exportStepAssembly(currentMaterialShapes_.parts,
        std::filesystem::path{path.toStdWString()}, projectName.toStdString());
    settings.setValue("lastDirectory", QFileInfo{path}.absolutePath());
    statusBar()->showMessage(QString{"STEP assembly exported to %1"}.arg(path), 5000);
  } catch (const Standard_Failure& exception) {
    statusBar()->showMessage("STEP export failed", 5000);
    QMessageBox::critical(this, "STEP export failed",
        QString{"OpenCascade: %1"}.arg(occtExceptionMessage(exception)));
  } catch (const std::exception& exception) {
    statusBar()->showMessage("STEP export failed", 5000);
    QMessageBox::critical(this, "STEP export failed", exception.what());
  }
}

void MainWindow::openHelp() {
  const QString path = QDir{QApplication::applicationDirPath()}
      .filePath("help/index.html");
  if (!QFileInfo::exists(path)) {
    QMessageBox::critical(this, "Help unavailable",
        QString{"The DesignRC help document was not found at:\n%1"}.arg(path));
    return;
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
    QMessageBox::critical(this, "Help unavailable",
        "The system could not open the DesignRC help document.");
  }
}

void MainWindow::showAbout() {
  const QString licensesPath = QDir::toNativeSeparators(
      QDir{QApplication::applicationDirPath()}.filePath("licenses"));
  QMessageBox::about(this, "About DesignRC",
      QString{"<h2>DesignRC</h2>"
              "<p><b>Parametric built-up RC aircraft wing design and manufacturing.</b></p>"
              "<p>Version %1 &middot; Release date: %2</p>"
              "<p>DesignRC turns a multi-panel half-wing definition into a complete "
              "mirrored solid assembly. It imports root and tip airfoils; generates "
              "solid ribs, tapered root-to-tip spars, shear webs, front, rear, and "
              "trailing-edge sheeting, leading and trailing edges, collision-checked "
              "joiners, wiring holes, spline-lofted ailerons and flaps, hinge posts, "
              "and spar-aware spoilers; and displays the result in an interactive "
              "3D viewport.</p>"
              "<p>It also creates annotated full-scale wing plans and exports vector "
              "plan PDFs, individual or combined DXF/SVG/PDF cutting parts, and a "
              "material-colored STEP assembly. Version 1.1.0 adds independent top and "
              "bottom trailing-edge sheeting, configurable front-sheeting extents, "
              "improved joiner geometry and collision checks, and cleaner rib and "
              "part exports.</p>"
              "<p>Copyright &copy; 2026 Barry Foust</p>"
              "<p>DesignRC is free software licensed under the GNU General Public License "
              "version 3 only. It comes with absolutely no warranty.</p>"
              "<p>DesignRC uses Qt 6 under LGPL 3.0, Open CASCADE Technology under LGPL 2.1 "
              "with its additional exception, and FreeType under the FreeType License.</p>"
              "<p>License texts and third-party notices are installed in:<br><code>%3</code></p>"}
          .arg(QApplication::applicationVersion(), DESIGNRC_RELEASE_DATE,
               licensesPath.toHtmlEscaped()));
}

std::vector<WingPanelData> MainWindow::defaultPanelData(const DisplayUnit unit) const {
  QSettings settings;
  const QString key = unit == DisplayUnit::Inches ? "defaults/panels/in" : "defaults/panels/mm";
  auto json = settings.value("defaults/panels/current").toByteArray();
  if (json.isEmpty()) json = settings.value(key).toByteArray();
  if (json.isEmpty() && unit == DisplayUnit::Millimeters)
    json = settings.value("defaults/panels").toByteArray();
  if (!json.isEmpty()) {
    const auto document = QJsonDocument::fromJson(json);
    std::vector<WingPanelData> result;
    for (const auto& value : document.array()) {
      const auto object = value.toObject();
      auto panel = panelDataFromJson(object);
      // Earlier installed defaults incorrectly forced this wood-stock width
      // to inches. Migrate only the two legacy installed values so an
      // intentional user-selected inch override remains intact.
      migrateLegacyLeadingEdgeWidthUnit(panel);
      // Remove the duplicate wood joiner shipped in the previous defaults.
      migrateLegacyDuplicateDefaultWoodJoiner(panel);
      const std::size_t panelIndex = result.size();
      if (!object.contains("spoilerMinimumWoodMargin"))
        panel.spoilerMinimumWoodMargin =
            unit == DisplayUnit::Inches ? 7.9375 : 6.0;
      if (!object.contains("spoilerMinimumCircleDistance"))
        panel.spoilerMinimumCircleDistance =
            unit == DisplayUnit::Inches ? 12.7 : 12.0;
      if (!object.contains("ribLighteningMinimumWoodMargin"))
        panel.ribLighteningMinimumWoodMargin =
            unit == DisplayUnit::Inches ? 7.9375 : 6.0;
      if (!object.contains("ribLighteningMinimumHoleDistance"))
        panel.ribLighteningMinimumHoleDistance =
            unit == DisplayUnit::Inches ? 12.7 : 12.0;
      if (!object.contains("ribLighteningStopRib"))
        panel.ribLighteningStopRib = 0;
      if (!object.contains("ribletStartRib"))
        panel.ribletStartRib = panelIndex == 0 ? 2 : 1;
      if (!object.contains("ribletEndRib"))
        panel.ribletEndRib = panel.ribCount;
      result.push_back(std::move(panel));
    }
    if (!result.empty()) {
      for (std::size_t index = 0; index < result.size(); ++index)
        if (result[index].ribLighteningStopRib <= 0)
          result[index].ribLighteningStopRib = std::max(
              1, result[index].ribCount -
                     (index + 1 == result.size() ? 1 : 2));
      return result;
    }
  }
  auto installed = installedDefaultPanelData(unit);
  installed.ribLighteningStopRib = std::max(1, installed.ribCount - 1);
  installed.ribletStartRib = 2;
  installed.ribletEndRib = installed.ribCount;
  return {installed};
}

std::vector<WingPanelData> MainWindow::panelData() const {
  std::vector<WingPanelData> result;
  result.reserve(panelEditors_.size());
  for (const auto* editor : panelEditors_) {
    auto panel = editor->data();
    if (!result.empty()) {
      panel.rootChord = result.back().tipChord;
      panel.rootAirfoil = result.back().tipAirfoil;
      panel.rootAirfoilPath = result.back().tipAirfoilPath;
    }
    result.push_back(std::move(panel));
  }
  return result;
}

void MainWindow::rebuildPanelTabs(const std::vector<WingPanelData>& panels) {
  changingPanelCount_ = true;
  const auto joinerDefaults = defaultPanelData(globalUnit_);
  while (panelTabs_->count() > 0) {
    auto* page = panelTabs_->widget(0);
    panelTabs_->removeTab(0);
    page->deleteLater();
  }
  panelEditors_.clear();
  for (std::size_t i = 0; i < panels.size(); ++i) {
    auto panel = panels[i];
    if (panel.ribLighteningStopRib <= 0)
      panel.ribLighteningStopRib = std::max(
          1, panel.ribCount - (i + 1 == panels.size() ? 1 : 2));
    if (panel.ribletEndRib <= 0)
      panel.ribletEndRib = panel.ribCount;
    if (i > 0) {
      panel.rootChord = panels[i - 1].tipChord;
      panel.rootAirfoil = panels[i - 1].tipAirfoil;
      panel.rootAirfoilPath = panels[i - 1].tipAirfoilPath;
    }
    auto* editor = new WingPanelEditor{panel, globalUnit_, false, true, i == 0};
    if (!joinerDefaults.empty())
      editor->setJoinerAddDefaults(
          joinerDefaults[std::min(i, joinerDefaults.size() - 1)]);
    connect(editor, &WingPanelEditor::changed, this, [this] { markPreviewPending(); });
    panelTabs_->addTab(editor, QString{"Panel %1"}.arg(i + 1));
    panelEditors_.push_back(editor);
  }
  panelCount_->setValue(static_cast<int>(panels.size()));
  changingPanelCount_ = false;
  updateMetrics();
}

void MainWindow::updateMetrics() {
  if (!metrics_) return;
  const auto panels = panelData();
  if (panels.empty()) {
    metrics_->setText(
        "Wingspan: --  |  Wing area: --\nAspect ratio: --  |  Taper ratio: --");
    return;
  }

  std::vector<double> dihedrals;
  dihedrals.reserve(panels.size());
  for (const auto& panel : panels) dihedrals.push_back(panel.dihedral);
  const auto assemblyAngles = domain::calculatePanelAssemblyAngles(dihedrals);

  double halfProjectedSpan = 0.0;
  double halfArea = 0.0;
  for (std::size_t index = 0; index < panels.size(); ++index) {
    const double angleRadians =
        assemblyAngles[index].panelInclinationDegrees * std::numbers::pi / 180.0;
    halfProjectedSpan += panels[index].panelSpan * std::cos(angleRadians);
    halfArea += panels[index].panelSpan *
        (panels[index].rootChord + panels[index].tipChord) * 0.5;
  }

  const double fullSpan = halfProjectedSpan * 2.0;
  const double fullArea = halfArea * 2.0;
  const double aspectRatio =
      fullArea > 0.0 ? fullSpan * fullSpan / fullArea : 0.0;
  const double taperRatio = panels.front().rootChord > 0.0
      ? panels.back().tipChord / panels.front().rootChord : 0.0;
  const double lengthFactor =
      globalUnit_ == DisplayUnit::Inches ? 1.0 / 25.4 : 1.0;
  const QString lengthUnit =
      globalUnit_ == DisplayUnit::Inches ? "in" : "mm";
  const double areaFactor = globalUnit_ == DisplayUnit::Inches
      ? 1.0 / (25.4 * 25.4) : 1.0 / 10000.0;
  const QString areaUnit =
      globalUnit_ == DisplayUnit::Inches ? QStringLiteral("in\u00B2")
                                         : QStringLiteral("dm\u00B2");
  metrics_->setText(
      QString{"Wingspan: %1 %2  |  Wing area: %3 %4\n"
              "Aspect ratio: %5  |  Taper ratio: %6"}
          .arg(fullSpan * lengthFactor, 0, 'f', 2)
          .arg(lengthUnit)
          .arg(fullArea * areaFactor, 0, 'f', 2)
          .arg(areaUnit)
          .arg(aspectRatio, 0, 'f', 2)
          .arg(taperRatio, 0, 'f', 2));
}

void MainWindow::changePanelCount(const int count) {
  if (changingPanelCount_ || count == static_cast<int>(panelEditors_.size())) return;
  const auto answer = QMessageBox::question(this, "Change number of wing panels",
      "Changing the number of wing panels will add or remove panel parameter sets. Continue?");
  if (answer != QMessageBox::Yes) {
    changingPanelCount_ = true;
    panelCount_->setValue(static_cast<int>(panelEditors_.size()));
    changingPanelCount_ = false;
    return;
  }
  auto panels = panelData();
  const auto defaults = defaultPanelData(globalUnit_);
  while (static_cast<int>(panels.size()) < count) {
    const auto index = panels.size();
    panels.push_back(index < defaults.size() ? defaults[index] : defaults.back());
    if (index >= defaults.size()) {
      panels.back().ribletStartRib = 1;
      panels.back().ribletEndRib = panels.back().ribCount;
    }
  }
  panels.resize(static_cast<std::size_t>(count));
  rebuildPanelTabs(panels);
  markPreviewPending();
}

void MainWindow::markPreviewPending() {
  ++designRevision_;
  projectModified_ = true;
  setWindowModified(true);
  updateMetrics();
  invalidatePlan();
  statusBar()->showMessage("Design changed - press Generate Wing to recompute");
}

void MainWindow::invalidatePlan() {
  if (generatePlanButton_) generatePlanButton_->setEnabled(false);
  if (exportPlanButton_) exportPlanButton_->setEnabled(false);
  if (exportPartsButton_) exportPartsButton_->setEnabled(false);
  if (exportStepButton_) exportStepButton_->setEnabled(false);
  if (graphicsTabs_) {
    if (graphicsTabs_->currentIndex() == 1) graphicsTabs_->setCurrentIndex(0);
    graphicsTabs_->setTabEnabled(1, false);
  }
  if (planViewport_) planViewport_->clearPlan();
}

void MainWindow::generatePlan() {
  if (!generatePlanButton_->isEnabled() || currentStructuredPanels_.empty()) return;
  const BusyCursor busy;
  const QString projectFileName = currentFile_.isEmpty()
      ? QString{"Untitled.designrc"} : QFileInfo{currentFile_}.fileName();
  const auto document = buildFlattenedWingPlan(
      currentStructuredPanels_, currentRibThicknesses_, currentPlanParameters_,
      globalUnit_ == DisplayUnit::Inches, projectFileName);
  if (document.empty()) {
    QMessageBox::warning(this, "Generate Plan", "No valid wing geometry is available for the plan.");
    return;
  }
  planViewport_->setDocument(document);
  graphicsTabs_->setTabEnabled(1, true);
  graphicsTabs_->setCurrentIndex(1);
  exportPlanButton_->setEnabled(true);
  statusBar()->showMessage("Flattened full-scale plan generated", 3000);
}

void MainWindow::exportPlanPdf() {
  if (!exportPlanButton_->isEnabled()) return;
  QSettings settings;
  const QString directory = settings.value("lastDirectory").toString();
  const QString projectName = currentFile_.isEmpty()
      ? QString{"Untitled"} : QFileInfo{currentFile_}.completeBaseName();
  QString path = QFileDialog::getSaveFileName(
      this, "Export Full-Scale Plan PDF",
      QDir{directory}.filePath(projectName + ".pdf"), "PDF files (*.pdf)");
  if (path.isEmpty()) return;
  if (QFileInfo{path}.suffix().isEmpty()) path += ".pdf";

  const BusyCursor busy;
  QString error;
  if (!planViewport_->exportPdf(path, error)) {
    QMessageBox::critical(this, "Plan PDF export failed", error);
    return;
  }
  settings.setValue("lastDirectory", QFileInfo{path}.absolutePath());
  statusBar()->showMessage("Full-scale plan PDF exported", 3000);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (updateThread_) {
    if (updateCancellation_) updateCancellation_->store(true);
    cancelUpdateButton_->setEnabled(false);
    statusBar()->showMessage("Cancelling Update View; close again when cancellation completes...");
    event->ignore();
    return;
  }
  if (!projectModified_) {
    event->accept();
    return;
  }
  const auto answer = QMessageBox::warning(this, "Save changes?",
      "The project has unsaved changes. Do you want to save them?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);
  if (answer == QMessageBox::Cancel) {
    event->ignore();
  } else if (answer == QMessageBox::Save) {
    saveProject() ? event->accept() : event->ignore();
  } else {
    event->accept();
  }
}

void MainWindow::regeneratePreview() {
  if (panelEditors_.empty() || updateThread_) return;
  for (std::size_t i = 0; i < panelEditors_.size(); ++i) {
    QString error;
    if (!panelEditors_[i]->validate(error)) {
      QMessageBox::warning(this, "Invalid panel settings",
          QString{"Panel %1: %2"}.arg(i + 1).arg(error));
      return;
    }
  }
  const auto panels = panelData();
  const auto alignmentError = woodJoinerSparAlignmentError(panels);
  if (!alignmentError.isEmpty()) {
    QMessageBox::warning(this, "Invalid wood joiner", alignmentError);
    return;
  }
  const auto unit = globalUnit_;
  const auto selected = static_cast<std::size_t>(std::max(0, panelTabs_->currentIndex()));
  const auto revision = designRevision_;
  const auto workerThreadCount = workerThreadCount_;
  updateCancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = updateCancellation_;

  invalidatePlan();
  updateButton_->setEnabled(false);
  panelCount_->setEnabled(false);
  panelTabs_->setEnabled(false);
  menuBar()->setEnabled(false);
  updateProgress_->setValue(0);
  updateProgress_->setFormat("%p%");
  updateProgress_->show();
  cancelUpdateButton_->setEnabled(true);
  cancelUpdateButton_->show();
  QApplication::setOverrideCursor(Qt::WaitCursor);
  statusBar()->showMessage("Starting Update View...");

  const QPointer<MainWindow> window{this};
  updateThread_ = QThread::create(
      [window, panels, unit, selected, revision, cancellation,
       workerThreadCount] {
    std::shared_ptr<PreviewComputation> result;
    QString error;
    std::optional<EdgeHeightCorrection> edgeHeightCorrection;
    bool cancelled = false;
    const auto reportProgress = [window](const int value, const QString& message) {
      if (!window) return;
      QMetaObject::invokeMethod(window, [window, value, message] {
        if (!window || !window->updateProgress_) return;
        window->updateProgress_->setValue(value);
        window->updateProgress_->setFormat("%p%");
        window->statusBar()->showMessage(message);
      }, Qt::QueuedConnection);
    };
    try {
      const auto workerStart = std::chrono::steady_clock::now();
      auto computed = computePreview(
          panels, unit, cancellation, reportProgress, workerThreadCount);
      computed.workerTotalMs = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - workerStart).count();
      computed.workerReturnOverheadMs = std::max(
          0.0, computed.workerTotalMs - computed.measuredWorkerMs);
      result = std::make_shared<PreviewComputation>(std::move(computed));
      result->queuedForGuiAt = std::chrono::steady_clock::now();
    } catch (const UpdateCancelled&) {
      cancelled = true;
    } catch (const Standard_Failure& exception) {
      error = QString{"OpenCascade: %1"}.arg(
          occtExceptionMessage(exception));
    } catch (const PanelEdgeHeightError& exception) {
      error = exception.what();
      const bool isLeadingEdge = exception.edgeName() == "LE";
      const bool isTrailingEdge = exception.edgeName() == "TE";
      const bool adjustableBlockStock =
          exception.panelIndex() < panels.size() &&
          ((isLeadingEdge &&
            panels[exception.panelIndex()].leadingEdgeType == 2) ||
           (isTrailingEdge &&
            panels[exception.panelIndex()].trailingEdgeType == 2));
      if (adjustableBlockStock) {
        edgeHeightCorrection = EdgeHeightCorrection{
            exception.panelIndex(), isLeadingEdge,
            exception.cutHeightMm()};
      }
    } catch (const std::exception& exception) {
      error = exception.what();
    } catch (...) {
      error = "An unknown geometry error occurred while rebuilding the wing.";
    }
    if (!window) return;
    QMetaObject::invokeMethod(window, [window, result, error, edgeHeightCorrection,
                                      cancelled, selected, revision, panels] {
      if (!window) return;
      window->updateThread_ = nullptr;
      window->updateCancellation_.reset();
      const auto finishUi = [window] {
        window->updateButton_->setEnabled(true);
        window->panelCount_->setEnabled(true);
        window->panelTabs_->setEnabled(true);
        window->menuBar()->setEnabled(true);
        window->updateProgress_->hide();
        window->cancelUpdateButton_->hide();
        QApplication::restoreOverrideCursor();
      };
      if (cancelled) {
        finishUi();
        window->statusBar()->showMessage("Update View cancelled", 3000);
        return;
      }
      if (!error.isEmpty()) {
        finishUi();
        window->statusBar()->showMessage("Update View failed", 3000);
        QMessageBox::critical(window, "Preview update failed", error);
        if (edgeHeightCorrection &&
            edgeHeightCorrection->panelIndex < window->panelEditors_.size()) {
          auto* editor =
              window->panelEditors_[edgeHeightCorrection->panelIndex];
          if (edgeHeightCorrection->leadingEdge)
            editor->setLeadingEdgeHeightMm(edgeHeightCorrection->heightMm);
          else
            editor->setTrailingEdgeHeightMm(edgeHeightCorrection->heightMm);
        }
        return;
      }
      if (!result || revision != window->designRevision_) {
        finishUi();
        window->statusBar()->showMessage(
            "Update result discarded because the design changed", 4000);
        return;
      }
      const double handoffMs = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - result->queuedForGuiAt).count();
      const std::size_t selectedPanel = std::min(selected, result->structuredPanels.size() - 1);
      window->currentRibs_ = result->ribSets[selectedPanel];
      window->currentStructuredWing_ = result->structuredPanels[selectedPanel];
      window->currentStructuredPanels_ = result->structuredPanels;
      window->currentRibThicknesses_ = result->thicknesses;
      window->currentPlanParameters_ = panels;
      window->currentDihedralAngles_ = result->dihedrals;
      window->currentMaterialShapes_ = std::move(result->materialShapes);
      const double lengthFactor = window->globalUnit_ == DisplayUnit::Inches ? 1.0 / 25.4 : 1.0;
      const QString lengthUnit = window->globalUnit_ == DisplayUnit::Inches ? "in" : "mm";
      const double areaFactor = window->globalUnit_ == DisplayUnit::Inches
          ? 1.0 / (25.4 * 25.4) : 1.0 / 10000.0;
      const QString areaUnit = window->globalUnit_ == DisplayUnit::Inches ? "in²" : "dm²";
      const QString designMetrics = QString{"Wingspan: %1 %2  |  Wing area: %3 %4\nAspect ratio: %5  |  Taper ratio: %6"}
          .arg(result->fullSpan * lengthFactor, 0, 'f', 2).arg(lengthUnit)
          .arg(result->fullArea * areaFactor, 0, 'f', 2).arg(areaUnit)
          .arg(result->aspectRatio, 0, 'f', 2).arg(result->taperRatio, 0, 'f', 2);
      window->updateProgress_->setValue(97);
      window->updateProgress_->setFormat("%p%");
      window->statusBar()->showMessage("Displaying preview...");
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      QElapsedTimer displayTimer;
      displayTimer.start();
      try {
        window->viewport_->displayMaterialShapes(
            window->currentMaterialShapes_.wood,
            window->currentMaterialShapes_.carbonFiber,
            window->currentMaterialShapes_.aluminum,
            window->currentMaterialShapes_.steel,
            window->currentMaterialShapes_.fiberglass);
      } catch (const Standard_Failure& exception) {
        finishUi();
        QMessageBox::critical(window, "Preview update failed",
            QString{"OpenCascade display: %1"}.arg(
                occtExceptionMessage(exception)));
        return;
      } catch (const std::exception& exception) {
        finishUi();
        QMessageBox::critical(window, "Preview update failed", exception.what());
        return;
      }
      const double displayMs = static_cast<double>(displayTimer.nsecsElapsed()) / 1.0e6;
      window->updateProgress_->setValue(100);
      window->updateProgress_->setFormat("%p%");
      const double totalMs = result->workerTotalMs + handoffMs + displayMs;
      const QString timing = QString{
          "Timing (s): prep %1 | joints/names %2 | panel geometry %3 | mirror %4 | "
          "finalize %5 | worker return %6 | GUI handoff %7 | display %8 | total %9"}
          .arg(result->panelPreparationMs / 1000.0, 0, 'f', 2)
          .arg(result->jointAndNamingMs / 1000.0, 0, 'f', 2)
          .arg(result->panelGeometryMs / 1000.0, 0, 'f', 2)
          .arg(result->mirrorAssemblyMs / 1000.0, 0, 'f', 2)
          .arg(result->finalizationMs / 1000.0, 0, 'f', 3)
          .arg(result->workerReturnOverheadMs / 1000.0, 0, 'f', 2)
          .arg(handoffMs / 1000.0, 0, 'f', 2)
          .arg(displayMs / 1000.0, 0, 'f', 2)
          .arg(totalMs / 1000.0, 0, 'f', 2);
      window->metrics_->setText(designMetrics);
      window->generatePlanButton_->setEnabled(true);
      window->exportPartsButton_->setEnabled(true);
      window->exportStepButton_->setEnabled(true);
      finishUi();
      window->statusBar()->showMessage(timing, 15000);
    }, Qt::QueuedConnection);
  });
  connect(updateThread_, &QThread::finished, updateThread_, &QObject::deleteLater);
  updateThread_->start();
}

void MainWindow::regeneratePreviewSynchronous() {
  if (panelEditors_.empty()) return;
  const BusyCursor busy;
  for (std::size_t i = 0; i < panelEditors_.size(); ++i) {
    QString error;
    if (!panelEditors_[i]->validate(error)) {
      QMessageBox::warning(this, "Invalid panel settings",
          QString{"Panel %1: %2"}.arg(i + 1).arg(error));
      return;
    }
  }
  try {
    const auto panels = panelData();
    const auto alignmentError = woodJoinerSparAlignmentError(panels);
    if (!alignmentError.isEmpty()) {
      QMessageBox::warning(this, "Invalid wood joiner", alignmentError);
      return;
    }
    std::vector<double> dihedrals;
    dihedrals.reserve(panels.size());
    for (const auto& panel : panels) dihedrals.push_back(panel.dihedral);
    const auto assemblyAngles = domain::calculatePanelAssemblyAngles(dihedrals);
    std::vector<double> panelTwists;
    panelTwists.reserve(panels.size());
    for (const auto& panel : panels) panelTwists.push_back(panel.twist);
    const auto twistRanges = domain::calculatePanelTwistRanges(panelTwists);

    std::vector<domain::StructuredWing> structuredPanels;
    std::vector<std::vector<domain::RibDefinition>> ribSets;
    std::vector<double> thicknesses;
    double originX = 0.0, originY = 0.0, originZ = 0.0;
    double halfArea = 0.0;
    for (std::size_t panelIndex = 0; panelIndex < panels.size(); ++panelIndex) {
      const auto& d = panels[panelIndex];
      domain::WingParameters p;
      p.halfSpan = d.panelSpan; p.rootChord = d.rootChord; p.tipChord = d.tipChord;
      p.sweep = d.sweep; p.dihedralDegrees = 0.0;
      p.rootTwistDegrees = twistRanges[panelIndex].rootTwistDegrees;
      p.tipTwistDegrees = twistRanges[panelIndex].tipTwistDegrees;
      p.ribThickness = d.ribThickness; p.ribCount = static_cast<std::size_t>(d.ribCount);
      auto ribs = domain::generateRibs(p, d.rootAirfoil, d.tipAirfoil);
      if (d.addRib1a) {
        const double t = 0.5 / static_cast<double>(d.ribCount - 1);
        ribs.insert(ribs.begin() + 1, {p.halfSpan * t,
            p.rootChord + t * (p.tipChord - p.rootChord), p.sweep * t, 0.0,
            p.rootTwistDegrees + t * (p.tipTwistDegrees - p.rootTwistDegrees),
            0.0, -0.5,
            domain::AirfoilProfile::interpolate(d.rootAirfoil, d.tipAirfoil, t)});
      }
      const auto& angles = assemblyAngles[panelIndex];
      const double orientation = angles.panelInclinationDegrees;
      const double radians = orientation * std::numbers::pi / 180.0;
      for (std::size_t i = 0; i < ribs.size(); ++i) {
        const double localSpan = ribs[i].spanPosition;
        ribs[i].leadingEdgeOffset += originX;
        ribs[i].spanPosition = originY + std::cos(radians) * localSpan;
        ribs[i].dihedralHeight = originZ + std::sin(radians) * localSpan;
        ribs[i].ribPlaneAngleDegrees = i == 0 ? angles.rootRibAngleDegrees :
            i + 1 == ribs.size() ? angles.tipRibAngleDegrees :
            angles.intermediateRibAngleDegrees;
        ribs[i].ribThicknessStartFactor = i == 0 ? 0.0 :
            i + 1 == ribs.size() ? -1.0 : -0.5;
      }
      // The outer half follows this panel. At an outer-panel root it is
      // mirrored about the joint-bisecting rib; at the center it is mirrored
      // about the center plane. The two halves therefore follow their panels.
      const double joinerAxisAngle = angles.panelInclinationDegrees;
      const double joinerMirrorAngle = panelIndex == 0 ? 0.0
          : angles.rootRibAngleDegrees;
      const auto structure = structureParametersFor(
          d, globalUnit_, joinerAxisAngle, joinerMirrorAngle,
          panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
          panelIndex != 0);
      try {
        structuredPanels.push_back(domain::applyWingStructure(ribs, structure));
      } catch (const domain::EdgeHeightError& exception) {
        QMessageBox::critical(
            this, "Preview update failed",
            formatEdgeHeightError(exception, d, globalUnit_, panelIndex));
        if (exception.edgeName() == "LE" && d.leadingEdgeType == 2)
          panelEditors_[panelIndex]->setLeadingEdgeHeightMm(
              exception.cutHeightMm());
        else if (exception.edgeName() == "TE" && d.trailingEdgeType == 2)
          panelEditors_[panelIndex]->setTrailingEdgeHeightMm(
              exception.cutHeightMm());
        return;
      }
      ribSets.push_back(ribs);
      thicknesses.push_back(d.ribThickness);
      halfArea += d.panelSpan * (d.rootChord + d.tipChord) * 0.5;
      originX = ribs.back().leadingEdgeOffset;
      originY = ribs.back().spanPosition;
      originZ = ribs.back().dihedralHeight;
    }
    for (std::size_t i = 1; i < structuredPanels.size(); ++i)
      addInnerPanelJoinerCuts(structuredPanels[i - 1], structuredPanels[i],
          panels[i - 1].ribThickness, panels[i].ribThickness);
    addConfiguredJoiners(panels, structuredPanels, thicknesses);
    std::size_t ribNumber = 1;
    std::size_t shearWebNumber = 1;
    for (std::size_t panelIndex = 0; panelIndex < structuredPanels.size(); ++panelIndex) {
      auto& structured = structuredPanels[panelIndex];
      const auto panelPartNumber = std::to_string(panelIndex + 1);
      const bool hasRib1a = panelIndex == 0 && panels[panelIndex].addRib1a;
      for (std::size_t ribIndex = 0; ribIndex < structured.ribs.size(); ++ribIndex) {
        if (hasRib1a && ribIndex == 1)
          structured.ribs[ribIndex].name = "R1a";
        else
          structured.ribs[ribIndex].name = "R" + std::to_string(ribNumber++);
      }
      const auto& angles = assemblyAngles[panelIndex];
      const auto structure = structureParametersFor(
          panels[panelIndex], globalUnit_,
          angles.panelInclinationDegrees,
          panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
          panelIndex == 0 ? 0.0 : angles.rootRibAngleDegrees,
          panelIndex != 0);
      domain::addRiblets(structured, structure);
      domain::addRibLighteningHoles(structured, structure);
      for (auto& web : structured.shearWebs)
        web.name = "SW" + std::to_string(shearWebNumber++);
      for (auto& member : structured.profiledMembers) {
        if (member.name.find("leading edge") != std::string::npos)
          member.name = "LE" + panelPartNumber;
        else if (member.name.find("trailing edge") != std::string::npos)
          member.name = "TE" + panelPartNumber;
      }
      for (auto& member : structured.members)
        if (member.name.find("leading edge") != std::string::npos)
          member.name = "LE" + panelPartNumber;
      for (std::size_t stockIndex = 0; stockIndex < structured.sheetStockParts.size(); ++stockIndex) {
        auto& stock = structured.sheetStockParts[stockIndex];
        stock.name = "TE" + panelPartNumber;
        if (structured.sheetStockParts.size() > 1)
          stock.name += "-" + std::to_string(stockIndex + 1);
      }
      for (auto& joiner : structured.joiners)
        if (joiner.name == "Center spar wood joiner")
          joiner.name = "J" + panelPartNumber;
    }
    const std::size_t selected = static_cast<std::size_t>(std::max(0, panelTabs_->currentIndex()));
    currentRibs_ = ribSets[selected];
    currentStructuredWing_ = structuredPanels[selected];
    currentStructuredPanels_ = structuredPanels;
    currentRibThicknesses_ = thicknesses;
    currentPlanParameters_ = panels;
    currentDihedralAngles_ = dihedrals;
    const double fullSpan = structuredPanels.back().ribs.back().rib.spanPosition * 2.0;
    const double fullArea = halfArea * 2.0;
    const double lengthFactor = globalUnit_ == DisplayUnit::Inches ? 1.0 / 25.4 : 1.0;
    const QString unit = globalUnit_ == DisplayUnit::Inches ? "in" : "mm";
    const double areaFactor = globalUnit_ == DisplayUnit::Inches ? 1.0 / (25.4 * 25.4) : 1.0 / 10000.0;
    const QString areaUnit = globalUnit_ == DisplayUnit::Inches ? "in²" : "dm²";
    metrics_->setText(QString{"Wingspan: %1 %2  |  Wing area: %3 %4\nAspect ratio: %5  |  Taper ratio: %6"}
      .arg(fullSpan * lengthFactor, 0, 'f', 2).arg(unit)
      .arg(fullArea * areaFactor, 0, 'f', 2).arg(areaUnit)
      .arg(fullSpan * fullSpan / fullArea, 0, 'f', 2)
      .arg(panels.back().tipChord / panels.front().rootChord, 0, 'f', 2));
    std::vector<geometry::MaterialShapeSet> panelMaterials(structuredPanels.size());
    for (std::size_t i = 0; i < structuredPanels.size(); ++i)
      static_cast<void>(geometry::buildStructuredWingPreview(
          structuredPanels[i], thicknesses[i], nullptr, &panelMaterials[i]));
    currentMaterialShapes_ = geometry::assembleMirroredMaterialPreview(panelMaterials);
    viewport_->displayMaterialShapes(currentMaterialShapes_.wood,
        currentMaterialShapes_.carbonFiber, currentMaterialShapes_.aluminum,
        currentMaterialShapes_.steel, currentMaterialShapes_.fiberglass);
    statusBar()->showMessage("Complete mirrored wing preview updated", 3000);
  } catch (const Standard_Failure& exception) {
    QMessageBox::critical(this, "Preview update failed",
        QString{"OpenCascade: %1"}.arg(occtExceptionMessage(exception)));
  } catch (const std::exception& exception) {
    QMessageBox::critical(this, "Preview update failed", exception.what());
  } catch (...) {
    QMessageBox::critical(this, "Preview update failed",
        "An unknown geometry error occurred while rebuilding the wing.");
  }
}

void MainWindow::regeneratePreviewLegacy() {
  if (panelEditors_.empty()) return;
  const BusyCursor busy;
  auto* editor = panelEditors_[static_cast<std::size_t>(std::max(0, panelTabs_->currentIndex()))];
  QString error;
  if (!editor->validate(error)) { QMessageBox::warning(this, "Invalid panel settings", error); return; }
  try {
    const auto d = editor->data();
    domain::WingParameters p;
    p.halfSpan = d.panelSpan; p.rootChord = d.rootChord; p.tipChord = d.tipChord;
    p.sweep = d.sweep; p.dihedralDegrees = d.dihedral;
    const int panelIndex = std::max(0, panelTabs_->currentIndex());
    std::vector<double> panelTwists;
    panelTwists.reserve(static_cast<std::size_t>(panelIndex + 1));
    for (int i = 0; i <= panelIndex; ++i)
      panelTwists.push_back(panelEditors_[static_cast<std::size_t>(i)]->data().twist);
    const auto twistRange = domain::calculatePanelTwistRanges(panelTwists).back();
    p.rootTwistDegrees = twistRange.rootTwistDegrees;
    p.tipTwistDegrees = twistRange.tipTwistDegrees;
    p.ribThickness = d.ribThickness; p.ribCount = static_cast<std::size_t>(d.ribCount);
    currentRibs_ = domain::generateRibs(p, d.rootAirfoil, d.tipAirfoil);
    const bool panelOne = panelTabs_->currentIndex() == 0;
    if (panelOne && d.addRib1a) {
      const double t = 0.5 / static_cast<double>(d.ribCount - 1);
      domain::RibDefinition rib1a{
          p.halfSpan * t,
          p.rootChord + t * (p.tipChord - p.rootChord),
          p.sweep * t,
          std::tan(p.dihedralDegrees * std::numbers::pi / 180.0) * p.halfSpan * t,
          p.rootTwistDegrees + t * (p.tipTwistDegrees - p.rootTwistDegrees),
          p.dihedralDegrees,
          -0.5,
          domain::AirfoilProfile::interpolate(d.rootAirfoil, d.tipAirfoil, t)};
      currentRibs_.insert(currentRibs_.begin() + 1, std::move(rib1a));
    }
    domain::StructureParameters structure;
    structure.ribThickness = d.ribThickness;
    structure.spars.reserve(d.spars.size());
    for (const auto& spar : d.spars)
      structure.spars.push_back({spar.chordLocationPercent, spar.verticalLocation,
          spar.material, spar.type, spar.woodHeight, spar.woodWidth,
          spar.tubeOd, spar.tubeId, spar.rodOd, spar.stripWidth,
          spar.stripThickness, spar.tipChordLocationPercent >= 0.0
              ? spar.tipChordLocationPercent : spar.chordLocationPercent});
    structure.sparShearWebs = d.sparShearWebs;
    structure.sparShearWebThickness = d.sparDefaults.shearWebThickness;
    const bool useLegacySpars = d.spars.empty();
    structure.topSpar = useLegacySpars && d.topSpar; structure.topSparHeight = d.topSparHeight; structure.topSparWidth = d.topSparWidth;
    structure.bottomSpar = useLegacySpars && d.bottomSpar; structure.bottomSparHeight = d.bottomSparHeight; structure.bottomSparWidth = d.bottomSparWidth;
    structure.shearWebs = useLegacySpars && d.shearWebs; structure.shearWebThickness = d.shearWebWidth;
    structure.carbonSpar = useLegacySpars ? d.carbonSpar : 0; structure.cfTubeOd = d.cfTubeOd; structure.cfTubeId = d.cfTubeId; structure.cfRodOd = d.cfRodOd;
    structure.leTopSheet = d.leTopSheet; structure.leTopSheetThickness = d.leTopSheetThickness;
    structure.leTopSheetStopChordPercent = d.leTopSheetStopChordPercent;
    structure.leTopSheetUpToSpar = d.leTopSheetUpToSpar;
    structure.leBottomSheet = d.leBottomSheet; structure.leBottomSheetThickness = d.leBottomSheetThickness;
    structure.leBottomSheetStopChordPercent = d.leBottomSheetStopChordPercent;
    structure.leBottomSheetUpToSpar = d.leBottomSheetUpToSpar;
    structure.teTopSheet = d.teTopSheet; structure.teTopSheetThickness = d.teTopSheetThickness;
    structure.teBottomSheet = d.teBottomSheet; structure.teBottomSheetThickness = d.teBottomSheetThickness;
    structure.turbulators = d.turbulators; structure.turbulatorCount = d.turbulatorCount;
    structure.turbulatorHeight = d.turbulatorHeight; structure.turbulatorWidth = d.turbulatorWidth;
    structure.topRearSpar = useLegacySpars && d.topRearSpar; structure.topRearSparHeight = d.topRearSparHeight; structure.topRearSparWidth = d.topRearSparWidth;
    structure.bottomRearSpar = useLegacySpars && d.bottomRearSpar; structure.bottomRearSparHeight = d.bottomRearSparHeight; structure.bottomRearSparWidth = d.bottomRearSparWidth;
    structure.leadingEdgeType = d.leadingEdgeType; structure.leadingEdgeWidth = d.leadingEdgeWidth; structure.leadingEdgeHeight = d.leadingEdgeHeight;
    structure.leadingEdgeTubeOd = d.leadingEdgeTubeOd; structure.leadingEdgeTubeId = d.leadingEdgeTubeId; structure.leadingEdgeRodOd = d.leadingEdgeRodOd;
    structure.trailingEdgeType = d.trailingEdgeType; structure.trailingEdgeWidth = d.trailingEdgeWidth; structure.trailingEdgeHeight = d.trailingEdgeHeight;
    structure.trailingEdgeSlotted = d.slottedForRibs;
    structure.trailingEdgeSlotDepth = globalUnit_ == DisplayUnit::Inches ? 25.4 / 4.0 : 6.0;
    structure.topTeSheeting = d.topTeSheeting;
    structure.topTeSheetingWidth = d.topTeSheetingWidth;
    structure.topTeSheetingThickness = d.topTeSheetingThickness;
    structure.topTeSheetingTaper = d.topTeSheetingTaper;
    structure.topTeSheetingTaperStartLocationPercent =
        d.topTeSheetingTaperStartLocationPercent;
    structure.bottomTeSheeting = d.bottomTeSheeting;
    structure.bottomTeSheetingWidth = d.bottomTeSheetingWidth;
    structure.bottomTeSheetingThickness = d.bottomTeSheetingThickness;
    structure.bottomTeSheetingTaper = d.bottomTeSheetingTaper;
    structure.bottomTeSheetingTaperStartLocationPercent =
        d.bottomTeSheetingTaperStartLocationPercent;
    structure.ailerons = d.ailerons; structure.aileronWidth = d.aileronWidth; structure.aileronHeight = d.aileronHeight;
    structure.aileronHingePostWidth = d.aileronHingePostWidth; structure.aileronHingePostHeight = d.aileronHingePostHeight;
    const auto stationNumber = [panelOne, &d](const int ribNumber) {
      return panelOne && d.addRib1a && ribNumber >= 2 ? ribNumber + 1 : ribNumber;
    };
    structure.leTopSheetStopRib = stationNumber(d.leTopSheetStopRib);
    structure.leBottomSheetStopRib = stationNumber(d.leBottomSheetStopRib);
    structure.teTopSheetStopRib = stationNumber(d.teTopSheetStopRib);
    structure.teBottomSheetStopRib = stationNumber(d.teBottomSheetStopRib);
    structure.aileronStartRib = stationNumber(d.aileronStartRib);
    structure.aileronStopRib = stationNumber(d.aileronStopRib);
    structure.flaps = d.flaps; structure.flapWidth = d.flapWidth; structure.flapHeight = d.flapHeight;
    structure.flapHingePostWidth = d.flapHingePostWidth; structure.flapHingePostHeight = d.flapHingePostHeight;
    structure.flapStartRib = stationNumber(d.flapStartRib);
    structure.flapStopRib = stationNumber(d.flapStopRib);
    structure.controlSurfaceGap = globalUnit_ == DisplayUnit::Inches ? 25.4 / 16.0 : 1.5;
    structure.spoilers = panelOne && d.spoilers;
    structure.spoilerStartRib = stationNumber(d.spoilerStartRib);
    structure.spoilerEndRib = stationNumber(d.spoilerEndRib);
    structure.spoilerChordLocationPercent = d.spoilerChordLocationPercent;
    structure.spoilerImmediatelyBehindSpar =
        d.spoilerImmediatelyBehindSpar;
    structure.spoilerWidth = d.spoilerWidth;
    structure.spoilerThickness = d.spoilerThickness;
    structure.spoilerFrameRailWidth = d.spoilerFrameRailWidth;
    structure.spoilerSupportRailHeight = d.spoilerSupportRailHeight;
    structure.spoilerLighteningHoles = d.spoilerLighteningHoles;
    structure.spoilerMinimumWoodMargin = d.spoilerMinimumWoodMargin;
    structure.spoilerMinimumCircleDistance =
        d.spoilerMinimumCircleDistance;
    structure.ribLighteningHoles = d.ribLighteningHoles;
    structure.ribLighteningStartRib =
        stationNumber(d.ribLighteningStartRib);
    structure.ribLighteningStopRib = stationNumber(
        d.ribLighteningStopRib > 0
            ? d.ribLighteningStopRib : std::max(1, d.ribCount - 2));
    structure.ribLighteningMinimumWoodMargin =
        d.ribLighteningMinimumWoodMargin;
    structure.ribLighteningMinimumHoleDistance =
        d.ribLighteningMinimumHoleDistance;
    structure.riblets = d.riblets;
    structure.ribletStartRib = stationNumber(d.ribletStartRib);
    structure.ribletEndRib = stationNumber(d.ribletEndRib > 0
        ? d.ribletEndRib : d.ribCount);
    structure.ribletsPerBay = d.ribletsPerBay;
    structure.wiringHoles = d.wiringHoles;
    structure.wiringHoleStartRib = d.wiringHoleStartRib;
    structure.wiringHoleEndRib = d.wiringHoleEndRib > 0
        ? d.wiringHoleEndRib
        : d.ribCount + (panelOne && d.addRib1a ? 1 : 0);
    structure.wiringHoleChordLocationPercent = d.wiringHoleChordLocationPercent;
    structure.wiringHoleWidth = d.wiringHoleWidth;
    structure.wiringHoleHeight = d.wiringHoleHeight;
    structure.rib1aPresent = panelOne && d.addRib1a;
    structure.centerSparWoodJoiner = panelOne && d.centerSparWoodJoiner;
    structure.behindSparJoiner = panelOne && d.behindSparJoiner;
    structure.behindSparJoinerType = d.behindSparJoinerType;
    structure.behindSparJoinerOd = d.behindSparJoinerOd;
    structure.behindSparJoinerId = d.behindSparJoinerId;
    structure.fiftyPercentJoiner = panelOne && d.fiftyPercentJoiner;
    structure.fiftyPercentJoinerType = d.fiftyPercentJoinerType;
    structure.fiftyPercentJoinerOd = d.fiftyPercentJoinerOd;
    structure.fiftyPercentJoinerId = d.fiftyPercentJoinerId;
    currentStructuredWing_ = domain::applyWingStructure(currentRibs_, structure);
    for (std::size_t ribIndex = 0;
         ribIndex < currentStructuredWing_.ribs.size(); ++ribIndex)
      currentStructuredWing_.ribs[ribIndex].name =
          "R" + std::to_string(ribIndex + 1);
    domain::addRiblets(currentStructuredWing_, structure);
    domain::addRibLighteningHoles(currentStructuredWing_, structure);
    const auto values = domain::calculateWingMetrics(p);
    const double lengthFactor = globalUnit_ == DisplayUnit::Inches ? 1.0 / 25.4 : 1.0;
    const QString unit = globalUnit_ == DisplayUnit::Inches ? "in" : "mm";
    const double areaFactor = globalUnit_ == DisplayUnit::Inches ? 1.0 / (25.4 * 25.4) : 1.0 / 10000.0;
    const QString areaUnit = globalUnit_ == DisplayUnit::Inches ? "in²" : "dm²";
    metrics_->setText(QString{"Wingspan: %1 %2  |  Wing area: %3 %4\nAspect ratio: %5  |  Taper ratio: %6"}
      .arg(values.fullSpan * lengthFactor, 0, 'f', 2).arg(unit)
      .arg(values.planformArea * areaFactor, 0, 'f', 2).arg(areaUnit)
      .arg(values.aspectRatio, 0, 'f', 2).arg(values.taperRatio, 0, 'f', 2));
    geometry::MaterialShapeSet panelMaterials;
    static_cast<void>(geometry::buildStructuredWingPreview(
        currentStructuredWing_, p.ribThickness, nullptr, &panelMaterials));
    currentMaterialShapes_ = geometry::assembleMirroredMaterialPreview({panelMaterials});
    viewport_->displayMaterialShapes(currentMaterialShapes_.wood,
        currentMaterialShapes_.carbonFiber, currentMaterialShapes_.aluminum,
        currentMaterialShapes_.steel, currentMaterialShapes_.fiberglass);
    statusBar()->showMessage(QString{"Panel %1 preview updated"}.arg(panelTabs_->currentIndex() + 1), 3000);
  } catch (const std::exception& exception) {
    QMessageBox::critical(this, "Preview update failed", exception.what());
  }
}

void MainWindow::exportRibs() {
  if (currentStructuredPanels_.empty()) return;
  QDialog dialog{this}; dialog.setWindowTitle("Choose parts to export");
  auto* layout = new QVBoxLayout{&dialog};
  auto* formatLayout = new QHBoxLayout;
  auto* exportDxf = new QCheckBox{"DXF"};
  auto* exportSvg = new QCheckBox{"SVG"};
  auto* exportPdf = new QCheckBox{"PDF"};
  exportDxf->setChecked(true);
  formatLayout->addWidget(new QLabel{"Export formats:"});
  formatLayout->addWidget(exportDxf);
  formatLayout->addWidget(exportSvg);
  formatLayout->addWidget(exportPdf);
  formatLayout->addStretch();
  layout->addLayout(formatLayout);

  auto* selectionLayout = new QHBoxLayout;
  auto* selectAll = new QPushButton{"Select All"};
  auto* selectNone = new QPushButton{"Select None"};
  selectionLayout->addWidget(selectAll);
  selectionLayout->addWidget(selectNone);
  selectionLayout->addStretch();
  layout->addLayout(selectionLayout);

  auto* list = new QListWidget;
  const auto addItem = [list](const QString& label, const char* type,
                              const std::size_t panel, const std::size_t index) {
    auto* item = new QListWidgetItem{label, list};
    item->setData(Qt::UserRole, type);
    item->setData(Qt::UserRole + 1, static_cast<int>(index));
    item->setData(Qt::UserRole + 2, static_cast<int>(panel));
    item->setCheckState(Qt::Unchecked);
  };
  auto* allPartsItem = new QListWidgetItem{"All Parts", list};
  allPartsItem->setData(Qt::UserRole, "all");
  allPartsItem->setCheckState(Qt::Unchecked);
  for (std::size_t panel = 0; panel < currentStructuredPanels_.size(); ++panel) {
    const auto& wing = currentStructuredPanels_[panel];
    for (std::size_t i = 0; i < wing.ribs.size(); ++i) {
      const auto& rib = wing.ribs[i];
      if (!rib.positiveHalfName.empty() && !rib.negativeHalfName.empty()) {
        addItem(QString::fromStdString(rib.positiveHalfName),
            "rib_positive", panel, i);
        addItem(QString::fromStdString(rib.negativeHalfName),
            "rib_negative", panel, i);
      } else {
        addItem(QString::fromStdString(rib.name), "rib", panel, i);
      }
    }
    for (std::size_t i = 0; i < wing.riblets.size(); ++i)
      addItem(QString::fromStdString(wing.riblets[i].name),
          "riblet", panel, i);
    for (std::size_t i = 0; i < wing.shearWebs.size(); ++i)
      addItem(QString::fromStdString(wing.shearWebs[i].name), "web", panel, i);
    for (std::size_t i = 0; i < wing.sheetStockParts.size(); ++i)
      addItem(QString::fromStdString(wing.sheetStockParts[i].name), "sheet_te", panel, i);
    for (std::size_t i = 0; i < wing.joiners.size(); ++i)
      if (wing.joiners[i].kind == domain::SpanMemberKind::Rectangular)
        addItem(QString::fromStdString(wing.joiners[i].name), "wood_joiner", panel, i);
    for (std::size_t i = 0; i < wing.spoilers.size(); ++i)
      addItem(QString::fromStdString(wing.spoilers[i].name), "spoiler", panel, i);
  }
  for (std::size_t panel = 0; panel < currentDihedralAngles_.size(); ++panel)
    addItem(QString{"Dihedral Angle %1"}.arg(panel + 1), "dihedral", panel, 0);
  layout->addWidget(list);
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel}; layout->addWidget(buttons);
  connect(selectAll, &QPushButton::clicked, &dialog, [list] {
    for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Checked);
  });
  connect(selectNone, &QPushButton::clicked, &dialog, [list] {
    for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Unchecked);
  });
  const auto updateOkEnabled = [=] {
    bool hasSelection = false;
    for (int row = 0; row < list->count(); ++row)
      hasSelection = hasSelection || list->item(row)->checkState() == Qt::Checked;
    buttons->button(QDialogButtonBox::Ok)->setEnabled(
        hasSelection && (exportDxf->isChecked() || exportSvg->isChecked() ||
                         exportPdf->isChecked()));
  };
  connect(exportDxf, &QCheckBox::toggled, &dialog, updateOkEnabled);
  connect(exportSvg, &QCheckBox::toggled, &dialog, updateOkEnabled);
  connect(exportPdf, &QCheckBox::toggled, &dialog, updateOkEnabled);
  connect(list, &QListWidget::itemChanged, &dialog, updateOkEnabled);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  updateOkEnabled();
  dialog.adjustSize();
  dialog.resize(dialog.width(), dialog.height() * 2);
  if (dialog.exec() != QDialog::Accepted) return;
  QSettings settings;
  QStringList selectedFormats;
  if (exportDxf->isChecked()) selectedFormats.push_back("DXF");
  if (exportSvg->isChecked()) selectedFormats.push_back("SVG");
  if (exportPdf->isChecked()) selectedFormats.push_back("PDF");
  const QString formats = selectedFormats.join(" / ");
  const auto directory = QFileDialog::getExistingDirectory(
      this, QString{"Export %1 parts"}.arg(formats),
      settings.value("lastDirectory").toString());
  if (directory.isEmpty()) return;
  settings.setValue("lastDirectory", directory);
  const BusyCursor busy;
  try {
    const auto outputPath = [&](const QString& name, const QString& extension) {
      return std::filesystem::path{directory.toStdWString()} /
          (name + extension).toStdWString();
    };
    const auto ribVariant = [&](const std::size_t panel,
                                const std::size_t index,
                                const bool negative) {
      auto rib = currentStructuredPanels_[panel].ribs[index];
      if (negative) {
        rib.positiveHalfBooleanHoles = rib.negativeHalfBooleanHoles;
        rib.name = rib.negativeHalfName;
      } else if (!rib.positiveHalfName.empty()) {
        rib.name = rib.positiveHalfName;
      }
      rib.negativeHalfBooleanHoles.clear();
      rib.positiveHalfName.clear();
      rib.negativeHalfName.clear();
      rib.uniqueHalfPartVariants = false;
      return rib;
    };
    const auto exportPart = [&](const QString& type, const std::size_t panel,
                                const std::size_t index, const bool svg) {
      const auto& wing = currentStructuredPanels_[panel];
      if (type == "rib" || type == "rib_positive" ||
          type == "rib_negative" || type == "riblet") {
        if (type == "riblet") {
          const auto& riblet = wing.riblets[index];
          const auto name = QString::fromStdString(riblet.name);
          if (svg) domain::exportStructuredRibSvg(
              riblet, outputPath(name, ".svg"), name.toStdString());
          else domain::exportStructuredRibDxf(
              riblet, outputPath(name, ".dxf"), name.toStdString());
          return;
        }
        const auto rib = ribVariant(panel, index, type == "rib_negative");
        const auto name = QString::fromStdString(rib.name);
        if (svg) domain::exportStructuredRibSvg(
            rib, outputPath(name, ".svg"), name.toStdString());
        else domain::exportStructuredRibDxf(
            rib, outputPath(name, ".dxf"), name.toStdString());
      } else if (type == "web") {
        const auto name = QString::fromStdString(wing.shearWebs[index].name);
        if (svg) domain::exportShearWebSvg(
            wing.shearWebs[index], outputPath(name, ".svg"), name.toStdString());
        else domain::exportShearWebDxf(
            wing.shearWebs[index], outputPath(name, ".dxf"), name.toStdString());
      } else if (type == "sheet_te") {
        const auto name = QString::fromStdString(wing.sheetStockParts[index].name);
        if (svg) domain::exportSheetStockSvg(
            wing.sheetStockParts[index], outputPath(name, ".svg"), name.toStdString());
        else domain::exportSheetStockDxf(
            wing.sheetStockParts[index], outputPath(name, ".dxf"), name.toStdString());
      } else if (type == "wood_joiner") {
        const auto name = QString::fromStdString(wing.joiners[index].name);
        if (svg) domain::exportWoodJoinerSvg(
            wing.joiners[index], outputPath(name, ".svg"), name.toStdString());
        else domain::exportWoodJoinerDxf(
            wing.joiners[index], outputPath(name, ".dxf"), name.toStdString());
      } else if (type == "spoiler") {
        const auto name = QString::fromStdString(wing.spoilers[index].name);
        if (svg) domain::exportSpoilerSvg(
            wing.spoilers[index], outputPath(name, ".svg"), name.toStdString());
        else domain::exportSpoilerDxf(
            wing.spoilers[index], outputPath(name, ".dxf"), name.toStdString());
      } else if (type == "dihedral") {
        const auto name = QString{"Dihedral Angle %1"}.arg(panel + 1);
        if (svg) domain::exportDihedralAngleSvg(
            currentDihedralAngles_[panel], outputPath(name, ".svg"), name.toStdString());
        else domain::exportDihedralAngleDxf(
            currentDihedralAngles_[panel], outputPath(name, ".dxf"), name.toStdString());
      }
    };
    const auto partDrawing = [&](const QString& type, const std::size_t panel,
                                 const std::size_t index) {
      const auto& wing = currentStructuredPanels_[panel];
      if (type == "rib" || type == "rib_positive" ||
          type == "rib_negative" || type == "riblet") {
        if (type == "riblet") {
          const auto& riblet = wing.riblets[index];
          return domain::makeStructuredRibPartDrawing(
              riblet, riblet.name);
        }
        const auto rib = ribVariant(panel, index, type == "rib_negative");
        return domain::makeStructuredRibPartDrawing(rib, rib.name);
      }
      if (type == "web") {
        const auto name = wing.shearWebs[index].name;
        return domain::makeShearWebPartDrawing(wing.shearWebs[index], name);
      }
      if (type == "sheet_te") {
        const auto name = wing.sheetStockParts[index].name;
        return domain::makeSheetStockPartDrawing(wing.sheetStockParts[index], name);
      }
      if (type == "wood_joiner") {
        const auto name = wing.joiners[index].name;
        return domain::makeWoodJoinerPartDrawing(wing.joiners[index], name);
      }
      if (type == "spoiler") {
        const auto name = wing.spoilers[index].name;
        return domain::makeSpoilerPartDrawing(wing.spoilers[index], name);
      }
      if (type == "dihedral") {
        const auto name = QString{"Dihedral Angle %1"}.arg(panel + 1).toStdString();
        return domain::makeDihedralAnglePartDrawing(currentDihedralAngles_[panel], name);
      }
      throw std::invalid_argument("Unknown part type selected for export");
    };
    std::vector<domain::PartDrawing> allParts;
    allParts.reserve(static_cast<std::size_t>(std::max(0, list->count() - 1)));
    for (int row = 0; row < list->count(); ++row) {
      const auto* item = list->item(row);
      const auto type = item->data(Qt::UserRole).toString();
      if (type == "all") continue;
      const auto index = static_cast<std::size_t>(
          item->data(Qt::UserRole + 1).toInt());
      const auto panel = static_cast<std::size_t>(
          item->data(Qt::UserRole + 2).toInt());
      allParts.push_back(partDrawing(type, panel, index));
    }
    for (int row = 0; row < list->count(); ++row) if (list->item(row)->checkState() == Qt::Checked) {
      const auto type = list->item(row)->data(Qt::UserRole).toString();
      if (type == "all") {
        if (exportDxf->isChecked())
          domain::exportPartsDxf(allParts, outputPath("All Parts", ".dxf"));
        if (exportSvg->isChecked())
          domain::exportPartsSvg(allParts, outputPath("All Parts", ".svg"));
        if (exportPdf->isChecked())
          exportPartsPdf(allParts, outputPath("All Parts", ".pdf"));
        continue;
      }
      const auto index = static_cast<std::size_t>(list->item(row)->data(Qt::UserRole + 1).toInt());
      const auto panel = static_cast<std::size_t>(list->item(row)->data(Qt::UserRole + 2).toInt());
      if (exportDxf->isChecked()) exportPart(type, panel, index, false);
      if (exportSvg->isChecked()) exportPart(type, panel, index, true);
      if (exportPdf->isChecked()) {
        const auto drawing = partDrawing(type, panel, index);
        exportPartPdf(drawing, outputPath(
            QString::fromStdString(drawing.label), ".pdf"));
      }
    }
    statusBar()->showMessage(QString{"%1 export complete"}.arg(formats), 3000);
  } catch (const std::exception& exception) {
    QMessageBox::critical(this, "Part export failed", exception.what());
  }
}

QJsonObject MainWindow::projectJson(const std::vector<WingPanelData>& panels, const DisplayUnit unit) const {
  QJsonArray array;
  for (const auto& panel : panels) array.append(panelDataToJson(panel));
  return {{"format", "DesignRC"}, {"version", 1}, {"globalUnit", static_cast<int>(unit)}, {"panels", array}};
}

bool MainWindow::loadProjectJson(const QJsonObject& object) {
  if (object.value("format").toString() != "DesignRC") return false;
  globalUnit_ = static_cast<DisplayUnit>(object.value("globalUnit").toInt(0));
  std::vector<WingPanelData> panels;
  for (const auto& value : object.value("panels").toArray()) {
    const auto panelObject = value.toObject();
    auto panel = panelDataFromJson(panelObject);
    const std::size_t panelIndex = panels.size();
    if (!panelObject.contains("spoilerMinimumWoodMargin"))
      panel.spoilerMinimumWoodMargin =
          globalUnit_ == DisplayUnit::Inches ? 7.9375 : 6.0;
    if (!panelObject.contains("spoilerMinimumCircleDistance"))
      panel.spoilerMinimumCircleDistance =
          globalUnit_ == DisplayUnit::Inches ? 12.7 : 12.0;
    if (!panelObject.contains("ribletStartRib"))
      panel.ribletStartRib = panelIndex == 0 ? 2 : 1;
    if (!panelObject.contains("ribletEndRib"))
      panel.ribletEndRib = panel.ribCount;
    panels.push_back(std::move(panel));
  }
  if (panels.empty()) return false;
  rebuildPanelTabs(panels);
  markPreviewPending();
  return true;
}

void MainWindow::newProject() {
  QSettings settings;
  globalUnit_ = static_cast<DisplayUnit>(settings.value("defaults/globalUnit",
      static_cast<int>(installedDefaultDisplayUnit())).toInt());
  workerThreadCount_ = std::clamp(
      settings.value("defaults/numberOfThreads",
          static_cast<qulonglong>(maximumGeometryThreadCount()))
          .toULongLong(),
      qulonglong{1},
      static_cast<qulonglong>(maximumGeometryThreadCount()));
  rebuildPanelTabs(defaultPanelData(globalUnit_));
  currentFile_.clear();
  updateWindowTitle();
  currentRibs_.clear();
  currentStructuredWing_ = {};
  currentStructuredPanels_.clear();
  currentRibThicknesses_.clear();
  currentPlanParameters_.clear();
  currentDihedralAngles_.clear();
  viewport_->clearShape();
  invalidatePlan();
  projectModified_ = false;
  setWindowModified(false);
  statusBar()->showMessage("New project - press Generate Wing to generate the preview");
}

void MainWindow::openProject() {
  QSettings settings;
  const auto path = QFileDialog::getOpenFileName(this, "Open DesignRC project", settings.value("lastDirectory").toString(), projectFilter());
  if (path.isEmpty()) return;
  QFile file{path};
  if (!file.open(QIODevice::ReadOnly) || !loadProjectJson(QJsonDocument::fromJson(file.readAll()).object())) {
    QMessageBox::critical(this, "Open failed", "The selected file is not a valid DesignRC project."); return;
  }
  currentFile_ = path;
  updateWindowTitle();
  settings.setValue("lastDirectory", QFileInfo{path}.absolutePath()); regeneratePreview();
  projectModified_ = false;
  setWindowModified(false);
}

bool MainWindow::saveProject() { return currentFile_.isEmpty() ? saveProjectAs() : writeProject(currentFile_); }
bool MainWindow::saveProjectAs() {
  QSettings settings;
  auto path = QFileDialog::getSaveFileName(this, "Save DesignRC project", settings.value("lastDirectory").toString(), projectFilter());
  if (path.isEmpty()) return false;
  if (QFileInfo{path}.suffix().isEmpty()) path += ".designrc";
  if (!writeProject(path)) return false;
  currentFile_ = path;
  updateWindowTitle();
  settings.setValue("lastDirectory", QFileInfo{path}.absolutePath()); return true;
}
bool MainWindow::writeProject(const QString& path) {
  QSaveFile file{path};
  if (!file.open(QIODevice::WriteOnly)) { QMessageBox::critical(this, "Save failed", file.errorString()); return false; }
  file.write(QJsonDocument{projectJson(panelData(), globalUnit_)}.toJson(QJsonDocument::Indented));
  if (!file.commit()) { QMessageBox::critical(this, "Save failed", file.errorString()); return false; }
  projectModified_ = false;
  setWindowModified(false);
  statusBar()->showMessage("Project saved", 3000); return true;
}

void MainWindow::updateWindowTitle() {
  QString title{"DesignRC - Wing Design"};
  if (!currentFile_.isEmpty()) title += " - " + QFileInfo{currentFile_}.fileName();
  setWindowTitle(title);
}

void MainWindow::openDefaults() {
  QDialog dialog{this}; dialog.setWindowTitle("DesignRC Defaults"); dialog.resize(1200, 760);
  auto* outer = new QVBoxLayout{&dialog};
  auto* unitRow = new QHBoxLayout; unitRow->addWidget(new QLabel{"Global length unit"});
  auto* unit = new QComboBox; unit->addItems({"Millimetres (mm)", "Inches (in)"}); unit->setCurrentIndex(static_cast<int>(globalUnit_));
  unitRow->addWidget(unit);
  unitRow->addSpacing(24);
  unitRow->addWidget(new QLabel{"Number of Wing Panels"});
  auto* defaultCount = new QSpinBox; defaultCount->setRange(1, 12);
  unitRow->addWidget(defaultCount);
  unitRow->addSpacing(24);
  unitRow->addWidget(new QLabel{"Number of Threads"});
  auto* defaultThreadCount = new QSpinBox;
  defaultThreadCount->setObjectName("defaultThreadCount");
  defaultThreadCount->setRange(
      1, static_cast<int>(maximumGeometryThreadCount()));
  defaultThreadCount->setValue(static_cast<int>(workerThreadCount_));
  unitRow->addWidget(defaultThreadCount);
  unitRow->addStretch(); outer->addLayout(unitRow);
  auto* container = new QWidget; auto* columns = new QHBoxLayout{container};
  std::vector<WingPanelEditor*> editors;
  auto defaults = defaultPanelData(globalUnit_);
  const auto addEditor = [&](const WingPanelData& panelDefaults) {
    auto* group = new QGroupBox{QString{"Panel %1 Defaults"}.arg(editors.size() + 1)};
    auto* layout = new QVBoxLayout{group};
    auto panel = panelDefaults;
    if (!editors.empty()) {
      const auto previous = editors.back()->data();
      panel.rootChord = previous.tipChord;
      panel.rootAirfoil = previous.tipAirfoil;
      panel.rootAirfoilPath = previous.tipAirfoilPath;
    }
    auto* editor = new WingPanelEditor{panel,
        static_cast<DisplayUnit>(unit->currentIndex()), true, true, editors.empty()};
    editor->setMinimumWidth(430);
    layout->addWidget(editor); columns->addWidget(group); editors.push_back(editor);
  };
  const auto removeLastEditor = [&] {
    auto* editor = editors.back();
    auto* group = editor->parentWidget();
    columns->removeWidget(group);
    group->deleteLater();
    editors.pop_back();
  };
  const auto snapshotActiveDefaults = [&] {
    defaults.clear();
    for (const auto* editor : editors) {
      auto panel = editor->data();
      if (!defaults.empty()) {
        panel.rootChord = defaults.back().tipChord;
        panel.rootAirfoil = defaults.back().tipAirfoil;
        panel.rootAirfoilPath = defaults.back().tipAirfoilPath;
      }
      defaults.push_back(std::move(panel));
    }
  };
  const auto applyUnitDefaults = [&](const int index) {
    if (defaults.empty())
      defaults.push_back(installedDefaultPanelData(
          static_cast<DisplayUnit>(index)));
    while (editors.size() < defaults.size())
      addEditor(defaults[editors.size()]);
    while (editors.size() > defaults.size()) removeLastEditor();
    for (std::size_t i = 0; i < editors.size(); ++i) {
      if (i > 0) {
        defaults[i].rootChord = defaults[i - 1].tipChord;
        defaults[i].rootAirfoil = defaults[i - 1].tipAirfoil;
        defaults[i].rootAirfoilPath = defaults[i - 1].tipAirfoilPath;
      }
      editors[i]->setGlobalUnit(static_cast<DisplayUnit>(index));
      editors[i]->setData(defaults[i]);
    }
    const QSignalBlocker blocker{defaultCount};
    defaultCount->setValue(static_cast<int>(defaults.size()));
  };
  for (const auto& panelDefaults : defaults)
    addEditor(panelDefaults);
  defaultCount->setValue(static_cast<int>(editors.size()));
  auto* scroll = new QScrollArea; scroll->setWidget(container); scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Save | QDialogButtonBox::Cancel}; outer->addWidget(buttons);
  connect(unit, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    snapshotActiveDefaults();
    applyUnitDefaults(index);
  });
  connect(defaultCount, &QSpinBox::valueChanged, &dialog,
      [&](int count) {
        while (static_cast<int>(editors.size()) < count) {
          auto seed = editors.empty()
              ? installedDefaultPanelData(
                    static_cast<DisplayUnit>(unit->currentIndex()))
              : editors.back()->data();
          if (!editors.empty()) {
            seed.ribletStartRib = 1;
            seed.ribletEndRib = seed.ribCount;
          }
          addEditor(seed);
        }
        while (static_cast<int>(editors.size()) > count) removeLastEditor();
      });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  snapshotActiveDefaults();
  QSettings settings; settings.setValue("defaults/globalUnit", unit->currentIndex());
  settings.setValue("defaults/numberOfThreads", defaultThreadCount->value());
  QJsonArray array;
  for (const auto& panel : defaults) array.append(panelDataToJson(panel));
  const auto serialized = QJsonDocument{array}.toJson(QJsonDocument::Compact);
  settings.setValue("defaults/panels/current", serialized);
  // Keep the historical unit-specific keys synchronized for older builds.
  settings.setValue("defaults/panels/mm", serialized);
  settings.setValue("defaults/panels/in", serialized);
  settings.sync();
  globalUnit_ = static_cast<DisplayUnit>(unit->currentIndex());
  workerThreadCount_ =
      static_cast<std::size_t>(defaultThreadCount->value());
  for (std::size_t i = 0; i < panelEditors_.size(); ++i) {
    panelEditors_[i]->setGlobalUnit(globalUnit_);
    if (!defaults.empty())
      panelEditors_[i]->setJoinerAddDefaults(
          defaults[std::min(i, defaults.size() - 1)]);
  }
  updateMetrics();
  statusBar()->showMessage("Defaults saved", 3000);
}

void MainWindow::copyFocusedText() {
  if (auto* line = qobject_cast<QLineEdit*>(QApplication::focusWidget())) line->copy();
  else if (auto* text = qobject_cast<QTextEdit*>(QApplication::focusWidget())) text->copy();
}
void MainWindow::pasteFocusedText() {
  if (auto* line = qobject_cast<QLineEdit*>(QApplication::focusWidget())) line->paste();
  else if (auto* text = qobject_cast<QTextEdit*>(QApplication::focusWidget())) text->paste();
}

} // namespace designrc::gui
