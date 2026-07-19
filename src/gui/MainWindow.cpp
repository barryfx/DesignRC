#include "gui/MainWindow.h"

#include "domain/DxfExporter.h"
#include "geometry/OcctRibBuilder.h"
#include "gui/OcctViewport.h"
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
#include <QUrl>
#include <QVBoxLayout>

#include <Standard_Failure.hxx>

#include <filesystem>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

namespace designrc::gui {
namespace {

class BusyCursor final {
public:
  BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
  ~BusyCursor() { QApplication::restoreOverrideCursor(); }
};

QString projectFilter() { return "DesignRC project (*.designrc)"; }

DisplayUnit effectiveParameterUnit(const WingPanelData& panel,
                                   const QString& key,
                                   const DisplayUnit globalUnit) {
  const auto override = panel.unitOverrides.value(key, UnitOverride::Global);
  if (override == UnitOverride::Inches) return DisplayUnit::Inches;
  if (override == UnitOverride::Millimeters) return DisplayUnit::Millimeters;
  return globalUnit;
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

domain::StructureParameters structureParametersFor(const WingPanelData& d,
                                                    const DisplayUnit unit,
                                                    const double joinerAxisAngle,
                                                    const double joinerMirrorAngle,
                                                    const double circularJoinerAxisAngle,
                                                    const bool circularJoinerSpansJoint) {
  domain::StructureParameters s;
  s.ribThickness = d.ribThickness;
  s.topSpar = d.topSpar; s.topSparHeight = d.topSparHeight; s.topSparWidth = d.topSparWidth;
  s.bottomSpar = d.bottomSpar; s.bottomSparHeight = d.bottomSparHeight; s.bottomSparWidth = d.bottomSparWidth;
  s.shearWebs = d.shearWebs; s.shearWebThickness = d.shearWebWidth;
  s.carbonSpar = d.carbonSpar; s.cfTubeOd = d.cfTubeOd; s.cfTubeId = d.cfTubeId; s.cfRodOd = d.cfRodOd;
  s.leTopSheet = d.leTopSheet; s.leTopSheetThickness = d.leTopSheetThickness;
  s.leBottomSheet = d.leBottomSheet; s.leBottomSheetThickness = d.leBottomSheetThickness;
  s.teTopSheet = d.teTopSheet; s.teTopSheetThickness = d.teTopSheetThickness;
  s.teBottomSheet = d.teBottomSheet; s.teBottomSheetThickness = d.teBottomSheetThickness;
  s.turbulators = d.turbulators; s.turbulatorCount = d.turbulatorCount;
  s.turbulatorHeight = d.turbulatorHeight; s.turbulatorWidth = d.turbulatorWidth;
  s.topRearSpar = d.topRearSpar; s.topRearSparHeight = d.topRearSparHeight; s.topRearSparWidth = d.topRearSparWidth;
  s.bottomRearSpar = d.bottomRearSpar; s.bottomRearSparHeight = d.bottomRearSparHeight; s.bottomRearSparWidth = d.bottomRearSparWidth;
  s.leadingEdgeType = d.leadingEdgeType; s.leadingEdgeWidth = d.leadingEdgeWidth; s.leadingEdgeHeight = d.leadingEdgeHeight;
  s.leadingEdgeTubeOd = d.leadingEdgeTubeOd; s.leadingEdgeTubeId = d.leadingEdgeTubeId; s.leadingEdgeRodOd = d.leadingEdgeRodOd;
  s.trailingEdgeType = d.trailingEdgeType; s.trailingEdgeWidth = d.trailingEdgeWidth; s.trailingEdgeHeight = d.trailingEdgeHeight;
  s.trailingEdgeSlotted = d.slottedForRibs;
  s.trailingEdgeSlotDepth = unit == DisplayUnit::Inches ? 25.4 / 4.0 : 6.0;
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
  s.rib1aPresent = d.addRib1a;
  s.centerSparWoodJoiner = d.centerSparWoodJoiner;
  s.behindSparJoiner = d.behindSparJoiner; s.behindSparJoinerType = d.behindSparJoinerType;
  s.behindSparJoinerOd = d.behindSparJoinerOd; s.behindSparJoinerId = d.behindSparJoinerId;
  s.fiftyPercentJoiner = d.fiftyPercentJoiner; s.fiftyPercentJoinerType = d.fiftyPercentJoinerType;
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
                                  const std::function<void(int, const QString&)>& progress) {
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
  std::vector<std::future<void>> structureTasks;
  structureTasks.reserve(panels.size());
  for (std::size_t panelIndex = 0; panelIndex < panels.size(); ++panelIndex) {
    structureTasks.push_back(std::async(std::launch::async, [&, panelIndex] {
      checkpoint();
      const auto& d = panels[panelIndex];
      const auto& angles = assemblyAngles[panelIndex];
      const auto origin = origins[panelIndex];
      domain::WingParameters p;
      p.halfSpan = d.panelSpan; p.rootChord = d.rootChord; p.tipChord = d.tipChord;
      p.sweep = d.sweep; p.dihedralDegrees = 0.0; p.tipTwistDegrees = d.twist;
      p.ribThickness = d.ribThickness; p.ribCount = static_cast<std::size_t>(d.ribCount);
      auto ribs = domain::generateRibs(p, d.rootAirfoil, d.tipAirfoil);
      if (d.addRib1a) {
        const double t = 0.5 / static_cast<double>(d.ribCount - 1);
        ribs.insert(ribs.begin() + 1, {p.halfSpan * t,
            p.rootChord + t * (p.tipChord - p.rootChord), p.sweep * t, 0.0,
            p.tipTwistDegrees * t, 0.0, -0.5,
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
        throw std::runtime_error(formatEdgeHeightError(
            exception, d, unit, panelIndex).toStdString());
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
    }));
  }
  for (auto& task : structureTasks) task.get();
  result.panelPreparationMs = millisecondsSince(totalStart);
  checkpoint();
  const auto jointStart = std::chrono::steady_clock::now();
  progress(18, "Connecting panel joiners...");
  for (std::size_t i = 1; i < result.structuredPanels.size(); ++i) {
    checkpoint();
    addInnerPanelJoinerCuts(result.structuredPanels[i - 1], result.structuredPanels[i],
        panels[i - 1].ribThickness, panels[i].ribThickness);
  }
  std::size_t ribNumber = 1;
  std::size_t shearWebNumber = 1;
  for (std::size_t panelIndex = 0; panelIndex < result.structuredPanels.size(); ++panelIndex) {
    auto& structured = result.structuredPanels[panelIndex];
    const auto panelPartNumber = std::to_string(panelIndex + 1);
    const bool hasRib1a = panelIndex == 0 && panels[panelIndex].addRib1a;
    for (std::size_t ribIndex = 0; ribIndex < structured.ribs.size(); ++ribIndex)
      structured.ribs[ribIndex].name = hasRib1a && ribIndex == 1
          ? "R1a" : "R" + std::to_string(ribNumber++);
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
      if (joiner.kind == domain::SpanMemberKind::Rectangular)
        joiner.name = "J" + panelPartNumber;
  }
  result.jointAndNamingMs = millisecondsSince(jointStart);
  checkpoint();
  const auto geometryStart = std::chrono::steady_clock::now();
  progress(20, "Building panel geometry in parallel...");
  std::vector<TopoDS_Shape> panelShapes(result.structuredPanels.size());
  std::vector<geometry::MaterialShapeSet> panelMaterialShapes(
      result.structuredPanels.size());
  result.panelBuildTimings.resize(result.structuredPanels.size());
  std::atomic_size_t geometryCompleted{0};
  std::vector<std::future<void>> geometryTasks;
  geometryTasks.reserve(result.structuredPanels.size());
  for (std::size_t panelIndex = 0; panelIndex < result.structuredPanels.size(); ++panelIndex) {
    geometryTasks.push_back(std::async(std::launch::async, [&, panelIndex] {
      checkpoint();
      panelShapes[panelIndex] = geometry::buildStructuredWingPreview(
          result.structuredPanels[panelIndex], result.thicknesses[panelIndex],
          &result.panelBuildTimings[panelIndex], &panelMaterialShapes[panelIndex]);
      const auto completed = ++geometryCompleted;
      progress(20 + static_cast<int>(65 * completed / result.structuredPanels.size()),
          QString{"Meshed %1 of %2 panels"}.arg(completed).arg(result.structuredPanels.size()));
      checkpoint();
    }));
  }
  for (auto& task : geometryTasks) task.get();
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

MainWindow::MainWindow(QWidget* parent) : QMainWindow{parent} {
  updateWindowTitle();
  resize(1400, 860);
  buildMenus();

  QSettings settings;
  globalUnit_ = static_cast<DisplayUnit>(settings.value("defaults/globalUnit",
      static_cast<int>(installedDefaultDisplayUnit())).toInt());

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
  metrics_->setWordWrap(true);
  leftLayout->addWidget(metrics_);
  updateButton_ = new QPushButton{"Update View"};
  auto* fit = new QPushButton{"Fit View"};
  generatePlanButton_ = new QPushButton{"Generate Plan"};
  generatePlanButton_->setEnabled(false);
  exportPlanButton_ = new QPushButton{"Export Plan PDF"};
  exportPlanButton_->setEnabled(false);
  auto* exportDxf = new QPushButton{"Export DXF/SVG"};
  auto* actions = new QHBoxLayout;
  actions->addWidget(updateButton_); actions->addWidget(fit);
  actions->addWidget(generatePlanButton_); actions->addWidget(exportPlanButton_);
  actions->addWidget(exportDxf);
  leftLayout->addLayout(actions);

  viewport_ = new OcctViewport;
  planViewport_ = new PlanViewport;
  graphicsTabs_ = new QTabWidget;
  graphicsTabs_->addTab(viewport_, "3D View");
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
  connect(exportDxf, &QPushButton::clicked, this, [this] { exportRibs(); });
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
      QString{"<h2>DesignRC</h2><p>Version %1</p><p>Release date: %2</p>"
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
  auto json = settings.value(key).toByteArray();
  if (json.isEmpty() && unit == DisplayUnit::Millimeters)
    json = settings.value("defaults/panels").toByteArray();
  if (!json.isEmpty()) {
    const auto document = QJsonDocument::fromJson(json);
    std::vector<WingPanelData> result;
    for (const auto& value : document.array()) result.push_back(panelDataFromJson(value.toObject()));
    if (!result.empty()) return result;
  }
  return {installedDefaultPanelData(unit)};
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
  while (panelTabs_->count() > 0) {
    auto* page = panelTabs_->widget(0);
    panelTabs_->removeTab(0);
    page->deleteLater();
  }
  panelEditors_.clear();
  for (std::size_t i = 0; i < panels.size(); ++i) {
    auto panel = panels[i];
    if (i > 0) {
      panel.rootChord = panels[i - 1].tipChord;
      panel.rootAirfoil = panels[i - 1].tipAirfoil;
      panel.rootAirfoilPath = panels[i - 1].tipAirfoilPath;
    }
    auto* editor = new WingPanelEditor{panel, globalUnit_, false, true, i == 0};
    connect(editor, &WingPanelEditor::changed, this, [this] { markPreviewPending(); });
    panelTabs_->addTab(editor, QString{"Panel %1"}.arg(i + 1));
    panelEditors_.push_back(editor);
  }
  panelCount_->setValue(static_cast<int>(panels.size()));
  changingPanelCount_ = false;
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
  }
  panels.resize(static_cast<std::size_t>(count));
  rebuildPanelTabs(panels);
  markPreviewPending();
}

void MainWindow::markPreviewPending() {
  ++designRevision_;
  projectModified_ = true;
  setWindowModified(true);
  invalidatePlan();
  statusBar()->showMessage("Design changed - press Update View to recompute");
}

void MainWindow::invalidatePlan() {
  if (generatePlanButton_) generatePlanButton_->setEnabled(false);
  if (exportPlanButton_) exportPlanButton_->setEnabled(false);
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
  updateThread_ = QThread::create([window, panels, unit, selected, revision, cancellation] {
    std::shared_ptr<PreviewComputation> result;
    QString error;
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
      auto computed = computePreview(panels, unit, cancellation, reportProgress);
      computed.workerTotalMs = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - workerStart).count();
      computed.workerReturnOverheadMs = std::max(
          0.0, computed.workerTotalMs - computed.measuredWorkerMs);
      result = std::make_shared<PreviewComputation>(std::move(computed));
      result->queuedForGuiAt = std::chrono::steady_clock::now();
    } catch (const UpdateCancelled&) {
      cancelled = true;
    } catch (const Standard_Failure& exception) {
      error = QString{"OpenCascade: %1"}.arg(exception.what());
    } catch (const std::exception& exception) {
      error = exception.what();
    } catch (...) {
      error = "An unknown geometry error occurred while rebuilding the wing.";
    }
    if (!window) return;
    QMetaObject::invokeMethod(window, [window, result, error, cancelled, selected, revision, panels] {
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
            result->materialShapes.wood,
            result->materialShapes.carbonFiber,
            result->materialShapes.aluminum);
      } catch (const Standard_Failure& exception) {
        finishUi();
        QMessageBox::critical(window, "Preview update failed",
            QString{"OpenCascade display: %1"}.arg(exception.what()));
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

    std::vector<domain::StructuredWing> structuredPanels;
    std::vector<std::vector<domain::RibDefinition>> ribSets;
    std::vector<double> thicknesses;
    double originX = 0.0, originY = 0.0, originZ = 0.0;
    double halfArea = 0.0;
    for (std::size_t panelIndex = 0; panelIndex < panels.size(); ++panelIndex) {
      const auto& d = panels[panelIndex];
      domain::WingParameters p;
      p.halfSpan = d.panelSpan; p.rootChord = d.rootChord; p.tipChord = d.tipChord;
      p.sweep = d.sweep; p.dihedralDegrees = 0.0; p.tipTwistDegrees = d.twist;
      p.ribThickness = d.ribThickness; p.ribCount = static_cast<std::size_t>(d.ribCount);
      auto ribs = domain::generateRibs(p, d.rootAirfoil, d.tipAirfoil);
      if (d.addRib1a) {
        const double t = 0.5 / static_cast<double>(d.ribCount - 1);
        ribs.insert(ribs.begin() + 1, {p.halfSpan * t,
            p.rootChord + t * (p.tipChord - p.rootChord), p.sweep * t, 0.0,
            p.tipTwistDegrees * t, 0.0, -0.5,
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
      structuredPanels.push_back(domain::applyWingStructure(ribs, structure));
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
        if (joiner.kind == domain::SpanMemberKind::Rectangular)
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
    viewport_->displayShape(
        geometry::buildMirroredWingAssemblyPreview(structuredPanels, thicknesses));
    statusBar()->showMessage("Complete mirrored wing preview updated", 3000);
  } catch (const Standard_Failure& exception) {
    QMessageBox::critical(this, "Preview update failed",
        QString{"OpenCascade: %1"}.arg(exception.what()));
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
    p.sweep = d.sweep; p.dihedralDegrees = d.dihedral; p.tipTwistDegrees = d.twist;
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
          p.tipTwistDegrees * t,
          p.dihedralDegrees,
          -0.5,
          domain::AirfoilProfile::interpolate(d.rootAirfoil, d.tipAirfoil, t)};
      currentRibs_.insert(currentRibs_.begin() + 1, std::move(rib1a));
    }
    domain::StructureParameters structure;
    structure.ribThickness = d.ribThickness;
    structure.topSpar = d.topSpar; structure.topSparHeight = d.topSparHeight; structure.topSparWidth = d.topSparWidth;
    structure.bottomSpar = d.bottomSpar; structure.bottomSparHeight = d.bottomSparHeight; structure.bottomSparWidth = d.bottomSparWidth;
    structure.shearWebs = d.shearWebs; structure.shearWebThickness = d.shearWebWidth;
    structure.carbonSpar = d.carbonSpar; structure.cfTubeOd = d.cfTubeOd; structure.cfTubeId = d.cfTubeId; structure.cfRodOd = d.cfRodOd;
    structure.leTopSheet = d.leTopSheet; structure.leTopSheetThickness = d.leTopSheetThickness;
    structure.leBottomSheet = d.leBottomSheet; structure.leBottomSheetThickness = d.leBottomSheetThickness;
    structure.teTopSheet = d.teTopSheet; structure.teTopSheetThickness = d.teTopSheetThickness;
    structure.teBottomSheet = d.teBottomSheet; structure.teBottomSheetThickness = d.teBottomSheetThickness;
    structure.turbulators = d.turbulators; structure.turbulatorCount = d.turbulatorCount;
    structure.turbulatorHeight = d.turbulatorHeight; structure.turbulatorWidth = d.turbulatorWidth;
    structure.topRearSpar = d.topRearSpar; structure.topRearSparHeight = d.topRearSparHeight; structure.topRearSparWidth = d.topRearSparWidth;
    structure.bottomRearSpar = d.bottomRearSpar; structure.bottomRearSparHeight = d.bottomRearSparHeight; structure.bottomRearSparWidth = d.bottomRearSparWidth;
    structure.leadingEdgeType = d.leadingEdgeType; structure.leadingEdgeWidth = d.leadingEdgeWidth; structure.leadingEdgeHeight = d.leadingEdgeHeight;
    structure.leadingEdgeTubeOd = d.leadingEdgeTubeOd; structure.leadingEdgeTubeId = d.leadingEdgeTubeId; structure.leadingEdgeRodOd = d.leadingEdgeRodOd;
    structure.trailingEdgeType = d.trailingEdgeType; structure.trailingEdgeWidth = d.trailingEdgeWidth; structure.trailingEdgeHeight = d.trailingEdgeHeight;
    structure.trailingEdgeSlotted = d.slottedForRibs;
    structure.trailingEdgeSlotDepth = globalUnit_ == DisplayUnit::Inches ? 25.4 / 4.0 : 6.0;
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
    const auto values = domain::calculateWingMetrics(p);
    const double lengthFactor = globalUnit_ == DisplayUnit::Inches ? 1.0 / 25.4 : 1.0;
    const QString unit = globalUnit_ == DisplayUnit::Inches ? "in" : "mm";
    const double areaFactor = globalUnit_ == DisplayUnit::Inches ? 1.0 / (25.4 * 25.4) : 1.0 / 10000.0;
    const QString areaUnit = globalUnit_ == DisplayUnit::Inches ? "in²" : "dm²";
    metrics_->setText(QString{"Wingspan: %1 %2  |  Wing area: %3 %4\nAspect ratio: %5  |  Taper ratio: %6"}
      .arg(values.fullSpan * lengthFactor, 0, 'f', 2).arg(unit)
      .arg(values.planformArea * areaFactor, 0, 'f', 2).arg(areaUnit)
      .arg(values.aspectRatio, 0, 'f', 2).arg(values.taperRatio, 0, 'f', 2));
    viewport_->displayShape(geometry::buildStructuredWingPreview(currentStructuredWing_, p.ribThickness));
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
  exportDxf->setChecked(true);
  formatLayout->addWidget(new QLabel{"Export formats:"});
  formatLayout->addWidget(exportDxf);
  formatLayout->addWidget(exportSvg);
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
    item->setCheckState(Qt::Checked);
  };
  for (std::size_t panel = 0; panel < currentStructuredPanels_.size(); ++panel) {
    const auto& wing = currentStructuredPanels_[panel];
    for (std::size_t i = 0; i < wing.ribs.size(); ++i)
      addItem(QString::fromStdString(wing.ribs[i].name), "rib", panel, i);
    for (std::size_t i = 0; i < wing.shearWebs.size(); ++i)
      addItem(QString::fromStdString(wing.shearWebs[i].name), "web", panel, i);
    for (std::size_t i = 0; i < wing.sheetStockParts.size(); ++i)
      addItem(QString::fromStdString(wing.sheetStockParts[i].name), "sheet_te", panel, i);
    for (std::size_t i = 0; i < wing.joiners.size(); ++i)
      if (wing.joiners[i].kind == domain::SpanMemberKind::Rectangular)
        addItem(QString::fromStdString(wing.joiners[i].name), "wood_joiner", panel, i);
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
    buttons->button(QDialogButtonBox::Ok)->setEnabled(
        exportDxf->isChecked() || exportSvg->isChecked());
  };
  connect(exportDxf, &QCheckBox::toggled, &dialog, updateOkEnabled);
  connect(exportSvg, &QCheckBox::toggled, &dialog, updateOkEnabled);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.adjustSize();
  dialog.resize(dialog.width(), dialog.height() * 2);
  if (dialog.exec() != QDialog::Accepted) return;
  QSettings settings;
  const QString formats = exportDxf->isChecked() && exportSvg->isChecked()
      ? "DXF and SVG" : exportDxf->isChecked() ? "DXF" : "SVG";
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
    const auto exportPart = [&](const QString& type, const std::size_t panel,
                                const std::size_t index, const bool svg) {
      const auto& wing = currentStructuredPanels_[panel];
      if (type == "rib") {
        const auto name = QString::fromStdString(wing.ribs[index].name);
        if (svg) domain::exportStructuredRibSvg(
            wing.ribs[index], outputPath(name, ".svg"), name.toStdString());
        else domain::exportStructuredRibDxf(
            wing.ribs[index], outputPath(name, ".dxf"), name.toStdString());
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
      } else if (type == "dihedral") {
        const auto name = QString{"Dihedral Angle %1"}.arg(panel + 1);
        if (svg) domain::exportDihedralAngleSvg(
            currentDihedralAngles_[panel], outputPath(name, ".svg"), name.toStdString());
        else domain::exportDihedralAngleDxf(
            currentDihedralAngles_[panel], outputPath(name, ".dxf"), name.toStdString());
      }
    };
    for (int row = 0; row < list->count(); ++row) if (list->item(row)->checkState() == Qt::Checked) {
      const auto type = list->item(row)->data(Qt::UserRole).toString();
      const auto index = static_cast<std::size_t>(list->item(row)->data(Qt::UserRole + 1).toInt());
      const auto panel = static_cast<std::size_t>(list->item(row)->data(Qt::UserRole + 2).toInt());
      if (exportDxf->isChecked()) exportPart(type, panel, index, false);
      if (exportSvg->isChecked()) exportPart(type, panel, index, true);
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
  for (const auto& value : object.value("panels").toArray()) panels.push_back(panelDataFromJson(value.toObject()));
  if (panels.empty()) return false;
  rebuildPanelTabs(panels);
  markPreviewPending();
  return true;
}

void MainWindow::newProject() {
  globalUnit_ = static_cast<DisplayUnit>(QSettings{}.value("defaults/globalUnit",
      static_cast<int>(installedDefaultDisplayUnit())).toInt());
  rebuildPanelTabs(defaultPanelData(globalUnit_));
  currentFile_.clear();
  updateWindowTitle();
  currentRibs_.clear();
  currentStructuredWing_ = {};
  currentStructuredPanels_.clear();
  currentRibThicknesses_.clear();
  currentPlanParameters_.clear();
  currentDihedralAngles_.clear();
  metrics_->clear();
  viewport_->clearShape();
  invalidatePlan();
  projectModified_ = false;
  setWindowModified(false);
  statusBar()->showMessage("New project - press Update View to generate the preview");
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
  unitRow->addWidget(defaultCount); unitRow->addStretch(); outer->addLayout(unitRow);
  auto* container = new QWidget; auto* columns = new QHBoxLayout{container};
  std::vector<WingPanelEditor*> editors;
  std::array<std::vector<WingPanelData>, 2> defaultsByUnit{
      defaultPanelData(DisplayUnit::Millimeters), defaultPanelData(DisplayUnit::Inches)};
  int activeUnit = unit->currentIndex();
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
    auto& values = defaultsByUnit[static_cast<std::size_t>(activeUnit)];
    values.clear();
    for (const auto* editor : editors) {
      auto panel = editor->data();
      if (!values.empty()) {
        panel.rootChord = values.back().tipChord;
        panel.rootAirfoil = values.back().tipAirfoil;
        panel.rootAirfoilPath = values.back().tipAirfoilPath;
      }
      values.push_back(std::move(panel));
    }
  };
  const auto applyUnitDefaults = [&](const int index) {
    auto& values = defaultsByUnit[static_cast<std::size_t>(index)];
    if (values.empty())
      values.push_back(installedDefaultPanelData(static_cast<DisplayUnit>(index)));
    while (editors.size() < values.size()) addEditor(values[editors.size()]);
    while (editors.size() > values.size()) removeLastEditor();
    for (std::size_t i = 0; i < editors.size(); ++i) {
      if (i > 0) {
        values[i].rootChord = values[i - 1].tipChord;
        values[i].rootAirfoil = values[i - 1].tipAirfoil;
        values[i].rootAirfoilPath = values[i - 1].tipAirfoilPath;
      }
      editors[i]->setGlobalUnit(static_cast<DisplayUnit>(index));
      editors[i]->setData(values[i]);
    }
    const QSignalBlocker blocker{defaultCount};
    defaultCount->setValue(static_cast<int>(values.size()));
  };
  for (const auto& panelDefaults : defaultsByUnit[static_cast<std::size_t>(activeUnit)])
    addEditor(panelDefaults);
  defaultCount->setValue(static_cast<int>(editors.size()));
  auto* scroll = new QScrollArea; scroll->setWidget(container); scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Save | QDialogButtonBox::Cancel}; outer->addWidget(buttons);
  connect(unit, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    snapshotActiveDefaults();
    activeUnit = index;
    applyUnitDefaults(index);
  });
  connect(defaultCount, &QSpinBox::valueChanged, &dialog,
      [&](int count) {
        while (static_cast<int>(editors.size()) < count) {
          const auto seed = editors.empty()
              ? installedDefaultPanelData(static_cast<DisplayUnit>(activeUnit))
              : editors.back()->data();
          addEditor(seed);
        }
        while (static_cast<int>(editors.size()) > count) removeLastEditor();
      });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  snapshotActiveDefaults();
  QSettings settings; settings.setValue("defaults/globalUnit", unit->currentIndex());
  for (int index = 0; index < 2; ++index) {
    QJsonArray array;
    for (const auto& panel : defaultsByUnit[static_cast<std::size_t>(index)])
      array.append(panelDataToJson(panel));
    settings.setValue(index == static_cast<int>(DisplayUnit::Inches)
            ? "defaults/panels/in" : "defaults/panels/mm",
        QJsonDocument{array}.toJson(QJsonDocument::Compact));
  }
  globalUnit_ = static_cast<DisplayUnit>(unit->currentIndex());
  for (auto* editor : panelEditors_) editor->setGlobalUnit(globalUnit_);
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
