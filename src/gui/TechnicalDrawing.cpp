#include "gui/TechnicalDrawing.h"

#include "gui/WingPanelEditor.h"

#include <QDate>
#include <QFont>
#include <QFontMetricsF>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <tuple>
#include <utility>

namespace designrc::gui {
namespace {

constexpr QColor kOutlineColor{62, 52, 45};
constexpr QColor kRibColor{112, 84, 62};
constexpr QColor kWoodFill{212, 189, 165, 105};
constexpr QColor kCarbonFill{25, 25, 25, 175};
constexpr QColor kAluminumFill{155, 160, 168, 175};

struct PanelStations {
  const domain::StructuredWing* wing{};
  std::vector<double> span;
};

struct PlanLayout {
  std::vector<PanelStations> panels;
  double halfSpan{};
  double chordMinimum{};
  double chordMaximum{};
  double rowDepth{};
  double upperRow{};
  double lowerRow{};
};

QPointF drawingPoint(const PlanLayout& layout, const bool mirrored,
                     const double row, const double span, const double chord) {
  return {mirrored ? layout.halfSpan - span : span,
          row + chord - layout.chordMinimum};
}

void addPolyline(TechnicalDrawingDocument& document, const std::vector<QPointF>& points,
                 const QColor& color, const double widthMm, const bool close = false,
                 const QColor& fill = Qt::transparent) {
  if (points.size() < 2) return;
  QPainterPath path{points.front()};
  for (std::size_t i = 1; i < points.size(); ++i) path.lineTo(points[i]);
  if (close) path.closeSubpath();
  document.paths.push_back({std::move(path), color, fill, widthMm});
}

QString formatLength(const double millimeters, const bool useInches) {
  if (!useInches) {
    const double rounded = std::round(millimeters * 10.0) / 10.0;
    const int decimals = std::abs(rounded - std::round(rounded)) < 1.0e-8 ? 0 : 1;
    return QString{"%1 mm"}.arg(rounded, 0, 'f', decimals);
  }
  const int thirtySeconds = static_cast<int>(std::round(millimeters / 25.4 * 32.0));
  const int whole = thirtySeconds / 32;
  const int remainder = std::abs(thirtySeconds % 32);
  if (remainder == 0) return QString{"%1 in"}.arg(whole);
  const int divisor = std::gcd(remainder, 32);
  const QString fraction = QString{"%1/%2"}.arg(remainder / divisor).arg(32 / divisor);
  return whole == 0 ? fraction + " in"
                    : QString{"%1 %2 in"}.arg(whole).arg(fraction);
}

QString formatSize(const double width, const double height, const bool useInches) {
  return formatLength(width, useInches) + " x " + formatLength(height, useInches);
}

bool parameterUsesInches(const WingPanelData* parameters, const QString& key,
                         const bool globalUseInches) {
  if (parameters == nullptr) return globalUseInches;
  switch (parameters->unitOverrides.value(key, UnitOverride::Global)) {
  case UnitOverride::Inches:
    return true;
  case UnitOverride::Millimeters:
    return false;
  case UnitOverride::Global:
    return globalUseInches;
  }
  return globalUseInches;
}

QString formatParameterLength(const double millimeters,
                              const WingPanelData* parameters,
                              const QString& key,
                              const bool globalUseInches) {
  return formatLength(millimeters,
      parameterUsesInches(parameters, key, globalUseInches));
}

QString formatParameterSize(const double width, const QString& widthKey,
                            const double height, const QString& heightKey,
                            const WingPanelData* parameters,
                            const bool globalUseInches) {
  return formatParameterLength(width, parameters, widthKey, globalUseInches) +
      " x " +
      formatParameterLength(height, parameters, heightKey, globalUseInches);
}

double drawingTextWidth(const QString& text, const double heightMm) {
  QFont font{"Arial"};
  font.setPointSizeF(heightMm * 72.0 / 25.4);
  const QFontMetricsF metrics{font};
  double width = 0.0;
  for (const auto& line : text.split('\n'))
    width = std::max(width, metrics.horizontalAdvance(line));
  return width;
}

QRectF drawingTextBounds(const TechnicalDrawingText& text) {
  QFont font{"Arial"};
  font.setPointSizeF(text.heightMm * 72.0 / 25.4);
  const QFontMetricsF metrics{font};
  const auto lines = text.text.split('\n');
  double width = 0.0;
  for (const auto& line : lines)
    width = std::max(width, metrics.horizontalAdvance(line));
  const double height = metrics.height() * static_cast<double>(lines.size());
  const double angle = text.rotationDegrees * std::numbers::pi / 180.0;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  const auto rotated = [&](const QPointF local) {
    return text.position + QPointF{
        local.x() * cosine - local.y() * sine,
        local.x() * sine + local.y() * cosine};
  };
  const std::array<QPointF, 4> corners{
      rotated({0.0, 0.0}), rotated({width, 0.0}),
      rotated({width, height}), rotated({0.0, height})};
  double left = corners.front().x(), right = left;
  double top = corners.front().y(), bottom = top;
  for (const auto corner : corners) {
    left = std::min(left, corner.x());
    right = std::max(right, corner.x());
    top = std::min(top, corner.y());
    bottom = std::max(bottom, corner.y());
  }
  return {left, top, right - left, bottom - top};
}

void addArrowHead(TechnicalDrawingDocument& document, const QPointF& tip,
                  const QPointF& toward) {
  const double dx = toward.x() - tip.x();
  const double dy = toward.y() - tip.y();
  const double magnitude = std::hypot(dx, dy);
  if (magnitude < 1.0e-8) return;
  constexpr double length = 4.0;
  constexpr double halfWidth = 1.5;
  const double ux = dx / magnitude;
  const double uy = dy / magnitude;
  const QPointF base{tip.x() + ux * length, tip.y() + uy * length};
  const QPointF perpendicular{-uy * halfWidth, ux * halfWidth};
  addPolyline(document, {tip, base + perpendicular, base - perpendicular},
      kOutlineColor, 0.25, true, kOutlineColor);
}

void addLeader(TechnicalDrawingDocument& document, const QPointF& textPosition,
               const QPointF& target, const QString& text) {
  const TechnicalDrawingText label{textPosition, text, 3.5, 0.0};
  const QRectF textBounds = drawingTextBounds(label);
  const bool targetBelowText = target.y() >= textBounds.center().y();
  const QPointF start{textBounds.center().x(),
      targetBelowText ? textBounds.bottom() + 2.0 : textBounds.top() - 2.0};
  const QPointF elbow{start.x(), target.y() + (start.y() - target.y()) * 0.35};
  addPolyline(document, {start, elbow, target}, kOutlineColor, 0.25);
  addArrowHead(document, target, elbow);
  document.texts.push_back(label);
}

void addDimension(TechnicalDrawingDocument& document, const QPointF& first,
                  const QPointF& second, const QPointF& firstExtension,
                  const QPointF& secondExtension, const QPointF& textPosition,
                  const QString& text, const double textRotation = 0.0) {
  addPolyline(document, {firstExtension, first}, kOutlineColor, 0.20);
  addPolyline(document, {secondExtension, second}, kOutlineColor, 0.20);
  addPolyline(document, {first, second}, kOutlineColor, 0.25);
  addArrowHead(document, first, second);
  addArrowHead(document, second, first);
  document.texts.push_back({textPosition, text, 3.5, textRotation});
}

std::pair<double, double> profileDimensions(const std::vector<domain::Point2>& profile) {
  double minimumX = std::numeric_limits<double>::max();
  double maximumX = std::numeric_limits<double>::lowest();
  double minimumY = std::numeric_limits<double>::max();
  double maximumY = std::numeric_limits<double>::lowest();
  for (const auto& point : profile) {
    minimumX = std::min(minimumX, point.x);
    maximumX = std::max(maximumX, point.x);
    minimumY = std::min(minimumY, point.y);
    maximumY = std::max(maximumY, point.y);
  }
  return {maximumX - minimumX, maximumY - minimumY};
}

template <typename BoundsAtStation>
void addBand(TechnicalDrawingDocument& document, const PlanLayout& layout,
             const PanelStations& panel, const bool mirrored, const double row,
             const std::size_t first, const std::size_t last,
             BoundsAtStation&& boundsAtStation, const QColor& fill) {
  if (panel.wing == nullptr || first >= panel.span.size() || last >= panel.span.size() ||
      last <= first)
    return;
  std::vector<QPointF> outline;
  outline.reserve((last - first + 1) * 2);
  for (std::size_t i = first; i <= last; ++i) {
    const auto [low, high] = boundsAtStation(i);
    outline.push_back(drawingPoint(layout, mirrored, row, panel.span[i], low));
  }
  for (std::size_t i = last + 1; i-- > first;) {
    const auto [low, high] = boundsAtStation(i);
    outline.push_back(drawingPoint(layout, mirrored, row, panel.span[i], high));
  }
  addPolyline(document, outline, kOutlineColor, 0.22, true, fill);
}

std::pair<double, double> profileBounds(const std::vector<domain::Point2>& profile,
                                        const double leadingEdge) {
  double low = std::numeric_limits<double>::max();
  double high = std::numeric_limits<double>::lowest();
  for (const auto& point : profile) {
    low = std::min(low, leadingEdge + point.x);
    high = std::max(high, leadingEdge + point.x);
  }
  return {low, high};
}

double panelSpanAtRibFace(const PanelStations& panel, const std::size_t ribIndex,
                          const double materialOffset) {
  if (panel.wing == nullptr || panel.wing->ribs.empty() ||
      ribIndex >= panel.span.size())
    return 0.0;
  const auto& panelRoot = panel.wing->ribs.front().rib;
  const auto& panelTip = panel.wing->ribs.back().rib;
  const double dy = panelTip.spanPosition - panelRoot.spanPosition;
  const double dz = panelTip.dihedralHeight - panelRoot.dihedralHeight;
  const double length = std::hypot(dy, dz);
  if (length < 1.0e-9) return panel.span[ribIndex];
  const double plane = panel.wing->ribs[ribIndex].rib.ribPlaneAngleDegrees *
      std::numbers::pi / 180.0;
  const double projectedOffset = materialOffset *
      (std::cos(plane) * dy + std::sin(plane) * dz) / length;
  return panel.span[ribIndex] + projectedOffset;
}

PlanLayout calculateLayout(const std::vector<domain::StructuredWing>& wings) {
  PlanLayout layout;
  layout.chordMinimum = std::numeric_limits<double>::max();
  layout.chordMaximum = std::numeric_limits<double>::lowest();
  double flatRoot = 0.0;
  for (const auto& wing : wings) {
    if (wing.ribs.size() < 2) continue;
    PanelStations panel;
    panel.wing = &wing;
    panel.span.reserve(wing.ribs.size());
    const auto& root = wing.ribs.front().rib;
    for (const auto& structured : wing.ribs) {
      const auto& rib = structured.rib;
      const double localSpan = std::hypot(rib.spanPosition - root.spanPosition,
                                          rib.dihedralHeight - root.dihedralHeight);
      panel.span.push_back(flatRoot + localSpan);
      layout.chordMinimum = std::min(layout.chordMinimum, rib.leadingEdgeOffset);
      layout.chordMaximum = std::max(layout.chordMaximum,
          rib.leadingEdgeOffset + rib.chord);
    }
    flatRoot = panel.span.back();
    layout.panels.push_back(std::move(panel));
  }
  layout.halfSpan = flatRoot;
  if (layout.panels.empty()) return layout;
  layout.rowDepth = std::max(1.0, layout.chordMaximum - layout.chordMinimum);
  const double labelSpace = 12.0;
  const double rowGap = std::max(35.0, layout.rowDepth * 0.20);
  layout.upperRow = labelSpace;
  layout.lowerRow = layout.upperRow + layout.rowDepth + rowGap;
  return layout;
}

double flattenedSpanForModelPoint(const PanelStations& panel,
                                  domain::Point3 point);

void addPanelComponents(TechnicalDrawingDocument& document, const PlanLayout& layout,
                        const PanelStations& panel, const bool mirrored, const double row,
                        const double ribThickness, const std::size_t panelIndex) {
  const auto& wing = *panel.wing;
  const auto ribCount = wing.ribs.size();

  for (const auto& member : wing.members) {
    const std::size_t count = std::min(member.centers.size(), ribCount);
    if (count < 2) continue;
    const QColor fill = member.kind == domain::SpanMemberKind::Tube ||
            member.kind == domain::SpanMemberKind::Rod ? kCarbonFill : kWoodFill;
    addBand(document, layout, panel, mirrored, row, 0, count - 1,
        [&](const std::size_t i) {
          const double center = wing.ribs[i].rib.leadingEdgeOffset + member.centers[i].x;
          return std::pair{center - member.width * 0.5, center + member.width * 0.5};
        }, fill);
  }

  for (const auto& member : wing.profiledMembers) {
    if (member.profiles.size() != ribCount) continue;
    auto ranges = member.activeRanges;
    if (ranges.empty()) ranges.emplace_back(0, ribCount - 1);
    for (const auto [first, last] : ranges) {
      if (first >= ribCount || last >= ribCount || last < first) continue;
      const auto stationSpan = [&](const std::size_t i, const bool startFace) {
        const auto& rib = wing.ribs[i].rib;
        return panelSpanAtRibFace(panel, i,
            (rib.ribThicknessStartFactor + (startFace ? 0.0 : 1.0)) *
                ribThickness);
      };
      std::vector<QPointF> outline;
      if (first == last) {
        const auto [low, high] = profileBounds(
            member.profiles[first], wing.ribs[first].rib.leadingEdgeOffset);
        const double startSpan = stationSpan(first, true);
        const double endSpan = stationSpan(first, false);
        outline = {
            drawingPoint(layout, mirrored, row, startSpan, low),
            drawingPoint(layout, mirrored, row, endSpan, low),
            drawingPoint(layout, mirrored, row, endSpan, high),
            drawingPoint(layout, mirrored, row, startSpan, high)};
      } else {
        outline.reserve((last - first + 1) * 2);
        for (std::size_t i = first; i <= last; ++i) {
          const auto [low, high] = profileBounds(
              member.profiles[i], wing.ribs[i].rib.leadingEdgeOffset);
          const double span = i == first ? stationSpan(i, true)
              : i == last ? stationSpan(i, false) : panel.span[i];
          outline.push_back(drawingPoint(layout, mirrored, row, span, low));
        }
        for (std::size_t i = last + 1; i-- > first;) {
          const auto [low, high] = profileBounds(
              member.profiles[i], wing.ribs[i].rib.leadingEdgeOffset);
          const double span = i == first ? stationSpan(i, true)
              : i == last ? stationSpan(i, false) : panel.span[i];
          outline.push_back(drawingPoint(layout, mirrored, row, span, high));
        }
      }
      addPolyline(document, outline, kOutlineColor, 0.22, true, kWoodFill);
    }
  }

  struct BandStation {
    double span{};
    double low{};
    double high{};
  };
  const auto addStationBand = [&](const std::vector<BandStation>& stations,
                                  const QColor fill) {
    if (stations.size() < 2) return;
    std::vector<QPointF> outline;
    outline.reserve(stations.size() * 2);
    for (const auto& station : stations)
      outline.push_back(drawingPoint(
          layout, mirrored, row, station.span, station.low));
    for (auto i = stations.size(); i-- > 0;)
      outline.push_back(drawingPoint(
          layout, mirrored, row, stations[i].span, stations[i].high));
    addPolyline(document, outline, kOutlineColor, 0.22, true, fill);
  };
  const domain::ControlSurfacePart* sharedFlap = nullptr;
  const domain::ControlSurfacePart* sharedAileron = nullptr;
  for (const auto& flap : wing.controlSurfaces) {
    if (flap.name != "Flap") continue;
    for (const auto& aileron : wing.controlSurfaces) {
      if (aileron.name == "Aileron" &&
          flap.stopRibIndex == aileron.startRibIndex) {
        sharedFlap = &flap;
        sharedAileron = &aileron;
      }
    }
  }
  for (const auto& control : wing.controlSurfaces) {
    if (control.profiles.size() < 2) continue;
    const std::size_t first = control.startRibIndex;
    const std::size_t last = std::min(control.stopRibIndex, ribCount - 1);
    if (last - first + 1 != control.profiles.size()) continue;
    std::vector<BandStation> controlStations;
    controlStations.reserve(control.profiles.size());
    for (std::size_t i = first; i <= last; ++i) {
      const auto [low, high] = profileBounds(
          control.profiles[i - first], wing.ribs[i].rib.leadingEdgeOffset);
      double span = panel.span[i];
      if (i == first)
        span = panelSpanAtRibFace(panel, i,
            (wing.ribs[i].rib.ribThicknessStartFactor + 1.0) * ribThickness +
                control.gap);
      else if (i == last)
        span = panelSpanAtRibFace(panel, i,
            (wing.ribs[i].rib.ribThicknessStartFactor +
                (control.extendThroughStopRib ? 1.0 : 0.0)) * ribThickness -
                (control.extendThroughStopRib ? 0.0 : control.gap));
      controlStations.push_back({span, low, high});
    }
    addStationBand(controlStations, kWoodFill);
    if (&control == sharedFlap || &control == sharedAileron ||
        control.hingePostWidth <= 0.0 ||
        control.hingePostCenters.size() != control.profiles.size())
      continue;
    std::vector<BandStation> hingeStations;
    hingeStations.reserve(control.hingePostCenters.size());
    for (std::size_t i = first; i <= last; ++i) {
      const double center = wing.ribs[i].rib.leadingEdgeOffset +
          control.hingePostCenters[i - first].x;
      double span = panel.span[i];
      if (i == first)
        span = panelSpanAtRibFace(panel, i,
            (wing.ribs[i].rib.ribThicknessStartFactor + 1.0) * ribThickness);
      else if (i == last)
        span = panelSpanAtRibFace(panel, i,
            (wing.ribs[i].rib.ribThicknessStartFactor +
                (control.extendThroughStopRib ? 1.0 : 0.0)) * ribThickness);
      hingeStations.push_back({span,
          center - control.hingePostWidth * 0.5,
          center + control.hingePostWidth * 0.5});
    }
    addStationBand(hingeStations, kWoodFill);
  }
  if (sharedFlap != nullptr && sharedAileron != nullptr) {
    const std::size_t first = sharedFlap->startRibIndex;
    const std::size_t last = sharedAileron->stopRibIndex;
    const double firstCenter = wing.ribs[first].rib.leadingEdgeOffset +
        sharedFlap->hingePostCenters.front().x;
    const double lastCenter = wing.ribs[last].rib.leadingEdgeOffset +
        sharedAileron->hingePostCenters.back().x;
    const double halfWidth = sharedFlap->hingePostWidth * 0.5;
    const std::vector<BandStation> hingeStations{
        {panelSpanAtRibFace(panel, first,
             (wing.ribs[first].rib.ribThicknessStartFactor + 1.0) * ribThickness),
         firstCenter - halfWidth, firstCenter + halfWidth},
        {panelSpanAtRibFace(panel, last,
             (wing.ribs[last].rib.ribThicknessStartFactor +
                 (sharedAileron->extendThroughStopRib ? 1.0 : 0.0)) *
                 ribThickness),
         lastCenter - halfWidth, lastCenter + halfWidth}};
    addStationBand(hingeStations, kWoodFill);
  }

  for (const auto& sheeting : wing.sheeting) {
    const auto& profiles = sheeting.fullProfiles.empty()
        ? sheeting.profiles : sheeting.fullProfiles;
    const std::size_t count = std::min(profiles.size(), ribCount);
    if (count < 2) continue;
    const auto sheetingSpan = [&](const std::size_t i) {
      if (i == 0) {
        return panelSpanAtRibFace(panel, i,
            wing.ribs[i].rib.ribThicknessStartFactor * ribThickness);
      }
      if (i + 1 == count) {
        return panelSpanAtRibFace(panel, i,
            (wing.ribs[i].rib.ribThicknessStartFactor + 1.0) * ribThickness);
      }
      return panel.span[i];
    };
    std::vector<QPointF> outline;
    outline.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
      const auto [low, high] = profileBounds(
          profiles[i], wing.ribs[i].rib.leadingEdgeOffset);
      outline.push_back(drawingPoint(
          layout, mirrored, row, sheetingSpan(i), low));
    }
    for (std::size_t i = count; i-- > 0;) {
      const auto [low, high] = profileBounds(
          profiles[i], wing.ribs[i].rib.leadingEdgeOffset);
      outline.push_back(drawingPoint(
          layout, mirrored, row, sheetingSpan(i), high));
    }
    addPolyline(document, outline, kOutlineColor, 0.22, true,
                QColor{212, 189, 165, 65});
  }

  for (const auto& web : wing.shearWebs) {
    if (web.bayIndex == 0 || web.bayIndex >= ribCount ||
        web.stationCorners.size() != 4)
      continue;
    const std::size_t root = web.bayIndex - 1;
    const std::size_t tip = web.bayIndex;
    const double rootFaceSpan = panelSpanAtRibFace(panel, root,
        (wing.ribs[root].rib.ribThicknessStartFactor + 1.0) * ribThickness);
    const double tipFaceSpan = panelSpanAtRibFace(panel, tip,
        wing.ribs[tip].rib.ribThicknessStartFactor * ribThickness);
    const double rootCenter = wing.ribs[root].rib.leadingEdgeOffset +
        (web.stationCorners[0].x + web.stationCorners[3].x) * 0.5;
    const double tipCenter = wing.ribs[tip].rib.leadingEdgeOffset +
        (web.stationCorners[1].x + web.stationCorners[2].x) * 0.5;
    const double halfThickness = web.thickness * 0.5;
    std::vector<QPointF> points{
        drawingPoint(layout, mirrored, row, rootFaceSpan,
                     rootCenter - halfThickness),
        drawingPoint(layout, mirrored, row, tipFaceSpan,
                     tipCenter - halfThickness),
        drawingPoint(layout, mirrored, row, tipFaceSpan,
                     tipCenter + halfThickness),
        drawingPoint(layout, mirrored, row, rootFaceSpan,
                     rootCenter + halfThickness)};
    addPolyline(document, points, kOutlineColor, 0.20, true, kWoodFill);
  }

  for (const auto& joiner : wing.joiners) {
    if (joiner.kind == domain::SpanMemberKind::Rectangular &&
        joiner.rectangularProfiles.size() >= 2) {
      const std::size_t count = std::min(joiner.rectangularProfiles.size(), ribCount);
      const auto bounds = [&](const std::size_t i) {
        double low = std::numeric_limits<double>::max();
        double high = std::numeric_limits<double>::lowest();
        for (const auto& point : joiner.rectangularProfiles[i]) {
          low = std::min(low, wing.ribs[i].rib.leadingEdgeOffset + point.x);
          high = std::max(high, wing.ribs[i].rib.leadingEdgeOffset + point.x);
        }
        return std::pair{low, high};
      };
      std::vector<QPointF> outline;
      outline.reserve(count * 2);
      for (std::size_t i = 0; i < count; ++i) {
        const double offset = i == 0
            ? wing.ribs[i].rib.ribThicknessStartFactor * ribThickness
            : i + 1 == count
                ? wing.ribs[i].rib.ribThicknessStartFactor * ribThickness
                : 0.0;
        const auto [low, high] = bounds(i);
        outline.push_back(drawingPoint(layout, mirrored, row,
            (i == 0 || i + 1 == count)
                ? panelSpanAtRibFace(panel, i, offset) : panel.span[i], low));
      }
      for (std::size_t i = count; i-- > 0;) {
        const double offset = i == 0
            ? wing.ribs[i].rib.ribThicknessStartFactor * ribThickness
            : i + 1 == count
                ? wing.ribs[i].rib.ribThicknessStartFactor * ribThickness
                : 0.0;
        const auto [low, high] = bounds(i);
        outline.push_back(drawingPoint(layout, mirrored, row,
            (i == 0 || i + 1 == count)
                ? panelSpanAtRibFace(panel, i, offset) : panel.span[i], high));
      }
      addPolyline(document, outline, kOutlineColor, 0.22, true, kWoodFill);
      if (panelIndex > 0 && joiner.innerRectangularProfiles.size() >= 2) {
        const auto profileBounds3 = [](const std::array<domain::Point3, 4>& profile) {
          double low = std::numeric_limits<double>::max();
          double high = std::numeric_limits<double>::lowest();
          domain::Point3 center;
          for (const auto point : profile) {
            low = std::min(low, point.x);
            high = std::max(high, point.x);
            center.x += point.x * 0.25;
            center.y += point.y * 0.25;
            center.z += point.z * 0.25;
          }
          return std::tuple{low, high, center};
        };
        const auto [innerLow, innerHigh, innerCenter] =
            profileBounds3(joiner.innerRectangularProfiles.front());
        const auto [jointLow, jointHigh, jointCenter] =
            profileBounds3(joiner.innerRectangularProfiles.back());
        static_cast<void>(jointCenter);
        const auto& innerPanel = layout.panels[panelIndex - 1];
        const double innerSpan = flattenedSpanForModelPoint(
            innerPanel, innerCenter);
        const double jointSpan = panel.span.front();
        addPolyline(document, {
            drawingPoint(layout, mirrored, row, innerSpan, innerLow),
            drawingPoint(layout, mirrored, row, jointSpan, jointLow),
            drawingPoint(layout, mirrored, row, jointSpan, jointHigh),
            drawingPoint(layout, mirrored, row, innerSpan, innerHigh)},
            kOutlineColor, 0.22, true, kWoodFill);
      }
    } else if (!joiner.hasExplicitEndpoints && joiner.centers.size() >= 2) {
      const std::size_t count = std::min(joiner.centers.size(), ribCount);
      const QColor fill = joiner.innerDiameter > 0.0 &&
              joiner.name.find("Aluminum") != std::string::npos
          ? kAluminumFill : kCarbonFill;
      addBand(document, layout, panel, mirrored, row, 0, count - 1,
          [&](const std::size_t i) {
            const double center = wing.ribs[i].rib.leadingEdgeOffset + joiner.centers[i].x;
            return std::pair{center - joiner.outerDiameter * 0.5,
                             center + joiner.outerDiameter * 0.5};
          }, fill);
    }
  }
}

double flattenedSpanForModelPoint(const PanelStations& panel,
                                  const domain::Point3 point) {
  if (panel.wing == nullptr || panel.wing->ribs.size() < 2 ||
      panel.span.size() < 2)
    return 0.0;
  const auto& root = panel.wing->ribs.front().rib;
  const auto& tip = panel.wing->ribs.back().rib;
  const double dy = tip.spanPosition - root.spanPosition;
  const double dz = tip.dihedralHeight - root.dihedralHeight;
  const double lengthSquared = dy * dy + dz * dz;
  if (lengthSquared < 1.0e-12) return panel.span.front();
  const double projection = ((point.y - root.spanPosition) * dy +
      (point.z - root.dihedralHeight) * dz) / lengthSquared;
  const double t = std::clamp(projection, 0.0, 1.0);
  return panel.span.front() + t * (panel.span.back() - panel.span.front());
}

void addCrossPanelJoiners(TechnicalDrawingDocument& document,
                          const PlanLayout& layout, const bool mirrored,
                          const double row) {
  for (std::size_t panelIndex = 0; panelIndex < layout.panels.size(); ++panelIndex) {
    const auto& panel = layout.panels[panelIndex];
    for (const auto& joiner : panel.wing->joiners) {
      if (!joiner.hasExplicitEndpoints ||
          joiner.kind == domain::SpanMemberKind::Rectangular)
        continue;
      const auto& innerPanel = panelIndex == 0
          ? panel : layout.panels[panelIndex - 1];
      const double innerSpan = flattenedSpanForModelPoint(
          innerPanel, joiner.innerEndpoint);
      const double outerSpan = flattenedSpanForModelPoint(
          panel, joiner.outerEndpoint);
      const double halfDiameter = joiner.outerDiameter * 0.5;
      const std::vector<QPointF> outline{
          drawingPoint(layout, mirrored, row, innerSpan,
                       joiner.innerEndpoint.x - halfDiameter),
          drawingPoint(layout, mirrored, row, outerSpan,
                       joiner.outerEndpoint.x - halfDiameter),
          drawingPoint(layout, mirrored, row, outerSpan,
                       joiner.outerEndpoint.x + halfDiameter),
          drawingPoint(layout, mirrored, row, innerSpan,
                       joiner.innerEndpoint.x + halfDiameter)};
      const QColor fill = joiner.name.find("Aluminum") != std::string::npos
          ? kAluminumFill : kCarbonFill;
      addPolyline(document, outline, kOutlineColor, 0.22, true, fill);
    }
  }
}

void addPanelReferenceGeometry(TechnicalDrawingDocument& document, const PlanLayout& layout,
                               const PanelStations& panel, const bool mirrored,
                               const double row, const double ribThickness) {
  const auto& wing = *panel.wing;
  for (std::size_t i = 0; i < wing.ribs.size(); ++i) {
    const auto& structured = wing.ribs[i];
    const auto& rib = structured.rib;
    double minimumX = 0.0;
    double maximumX = rib.chord;
    if (!structured.outerOutline.empty()) {
      minimumX = std::numeric_limits<double>::max();
      maximumX = std::numeric_limits<double>::lowest();
      for (const auto& point : structured.outerOutline) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
      }
    }
    const double startSpan = panel.span[i] +
        rib.ribThicknessStartFactor * ribThickness;
    const double endSpan = panel.span[i] +
        (rib.ribThicknessStartFactor + 1.0) * ribThickness;
    addPolyline(document, {
        drawingPoint(layout, mirrored, row, startSpan,
                     rib.leadingEdgeOffset + minimumX),
        drawingPoint(layout, mirrored, row, endSpan,
                     rib.leadingEdgeOffset + minimumX),
        drawingPoint(layout, mirrored, row, endSpan,
                     rib.leadingEdgeOffset + maximumX),
        drawingPoint(layout, mirrored, row, startSpan,
                     rib.leadingEdgeOffset + maximumX)},
        kRibColor, 0.20, true, kWoodFill);
  }
  const auto& root = wing.ribs.front().rib;
  const auto& tip = wing.ribs.back().rib;
  addPolyline(document, {
      drawingPoint(layout, mirrored, row, panel.span.front(), root.leadingEdgeOffset),
      drawingPoint(layout, mirrored, row, panel.span.back(), tip.leadingEdgeOffset),
      drawingPoint(layout, mirrored, row, panel.span.back(), tip.leadingEdgeOffset + tip.chord),
      drawingPoint(layout, mirrored, row, panel.span.front(), root.leadingEdgeOffset + root.chord)},
      kOutlineColor, 0.45, true);
}

double addRootRibSection(TechnicalDrawingDocument& document,
                         const PlanLayout& layout) {
  if (layout.panels.empty() || layout.panels.front().wing == nullptr ||
      layout.panels.front().wing->ribs.empty())
    return layout.halfSpan;
  const auto& root = layout.panels.front().wing->ribs.front();
  std::vector<domain::Point2> outline = root.outerOutline;
  if (outline.size() < 3) {
    outline.reserve(root.rib.profile.outline().size());
    for (const auto& point : root.rib.profile.outline())
      outline.push_back({point.x * root.rib.chord, point.y * root.rib.chord});
  }
  if (outline.size() < 3) return layout.halfSpan;

  double minimumY = std::numeric_limits<double>::max();
  double maximumY = std::numeric_limits<double>::lowest();
  for (const auto& point : outline) {
    minimumY = std::min(minimumY, point.y);
    maximumY = std::max(maximumY, point.y);
  }
  constexpr double sectionGap = 35.0;
  const double sectionLeft = layout.halfSpan + sectionGap;
  double mappedRight = sectionLeft + maximumY - minimumY;
  const auto mapPoint = [&](const domain::Point2 point) {
    return QPointF{
        sectionLeft + point.y - minimumY,
        layout.upperRow + root.rib.leadingEdgeOffset - layout.chordMinimum + point.x};
  };
  const auto addMappedProfile = [&](const std::vector<domain::Point2>& profile,
                                    const QColor& fill) {
    if (profile.size() < 3) return;
    std::vector<QPointF> mapped;
    mapped.reserve(profile.size());
    for (const auto point : profile) {
      mapped.push_back(mapPoint(point));
      mappedRight = std::max(mappedRight, mapped.back().x());
    }
    addPolyline(document, mapped, kOutlineColor, 0.25, true, fill);
  };
  addMappedProfile(outline, kWoodFill);
  const QColor cutawayFill{255, 255, 255, 255};
  for (const auto& hole : root.holes) addMappedProfile(hole, cutawayFill);
  for (const auto& cutout : root.booleanCutouts)
    addMappedProfile(cutout, cutawayFill);
  for (const auto& hole : root.booleanHoles) addMappedProfile(hole, cutawayFill);

  const auto rectangleProfile = [](const domain::Point2 center,
                                   const double width, const double height) {
    const double halfWidth = width * 0.5;
    const double halfHeight = height * 0.5;
    return std::vector<domain::Point2>{
        {center.x - halfWidth, center.y - halfHeight},
        {center.x + halfWidth, center.y - halfHeight},
        {center.x + halfWidth, center.y + halfHeight},
        {center.x - halfWidth, center.y + halfHeight}};
  };
  const auto circularProfile = [](const domain::Point2 center,
                                  const double diameter) {
    std::vector<domain::Point2> profile;
    if (diameter <= 0.0) return profile;
    constexpr std::size_t segments = 48;
    profile.reserve(segments);
    const double radius = diameter * 0.5;
    for (std::size_t index = 0; index < segments; ++index) {
      const double angle = static_cast<double>(index) * 2.0 * std::numbers::pi /
          static_cast<double>(segments);
      profile.push_back({center.x + std::cos(angle) * radius,
                         center.y + std::sin(angle) * radius});
    }
    return profile;
  };
  const auto normalizedName = [](const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const char value) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    return lower;
  };
  const auto isSparOrEdge = [&](const std::string& name) {
    const auto lower = normalizedName(name);
    return lower.find("spar") != std::string::npos ||
        lower.rfind("le", 0) == 0 || lower.rfind("te", 0) == 0;
  };
  const auto& wing = *layout.panels.front().wing;
  for (const auto& member : wing.members) {
    if ((!isSparOrEdge(member.name) &&
         member.kind != domain::SpanMemberKind::Turbulator) ||
        member.centers.empty())
      continue;
    if (member.kind == domain::SpanMemberKind::Rectangular ||
        member.kind == domain::SpanMemberKind::Turbulator) {
      addMappedProfile(rectangleProfile(
          member.centers.front(), member.width, member.height), Qt::transparent);
    } else {
      addMappedProfile(circularProfile(member.centers.front(), member.width),
                       Qt::transparent);
      if (member.kind == domain::SpanMemberKind::Tube && member.innerDiameter > 0.0)
        addMappedProfile(circularProfile(
            member.centers.front(), member.innerDiameter), Qt::transparent);
    }
  }
  for (const auto& member : wing.profiledMembers) {
    const auto lower = normalizedName(member.name);
    if ((lower.rfind("le", 0) != 0 && lower.rfind("te", 0) != 0) ||
        member.profiles.empty())
      continue;
    addMappedProfile(member.profiles.front(), Qt::transparent);
  }
  for (const auto& sheeting : wing.sheeting) {
    if (!sheeting.profiles.empty())
      addMappedProfile(sheeting.profiles.front(), Qt::transparent);
  }
  for (const auto& joiner : wing.joiners) {
    if (!joiner.rectangularProfiles.empty()) {
      const auto& source = joiner.rectangularProfiles.front();
      addMappedProfile(std::vector<domain::Point2>{
          source.begin(), source.end()}, Qt::transparent);
    } else if (!joiner.centers.empty()) {
      addMappedProfile(circularProfile(
          joiner.centers.front(), joiner.outerDiameter), Qt::transparent);
      if (joiner.innerDiameter > 0.0)
        addMappedProfile(circularProfile(
            joiner.centers.front(), joiner.innerDiameter), Qt::transparent);
    }
  }

  const QString ribName = root.name.empty()
      ? QString{"R1"} : QString::fromStdString(root.name);
  const QString label = ribName + " CUTAWAY";
  document.texts.push_back({
      {sectionLeft, layout.upperRow + root.rib.leadingEdgeOffset -
                        layout.chordMinimum - 18.0},
      label, 3.5, 0.0});
  return std::max(mappedRight, sectionLeft + drawingTextWidth(label, 3.5));
}

struct CalloutPlacer {
  struct PendingCallout {
    QPointF target;
    QString text;
    bool forceBelow{};
  };

  TechnicalDrawingDocument& document;
  const PlanLayout& layout;
  double halfSpan{};
  double drawingBottom{};
  std::vector<PendingCallout> pending;
  double leftMost{};
  double rightMost{};

  void add(const QPointF& lowerWingTarget, const QString& text,
           const bool forceBelow = false) {
    pending.push_back({lowerWingTarget, text, forceBelow});
  }

  void finish() {
    leftMost = std::numeric_limits<double>::max();
    rightMost = std::numeric_limits<double>::lowest();
    std::vector<std::size_t> upperIndices;
    std::vector<std::size_t> lowerIndices;
    std::size_t alternatingIndex = 0;
    for (std::size_t index = 0; index < pending.size(); ++index) {
      if (pending[index].forceBelow)
        lowerIndices.push_back(index);
      else
        (alternatingIndex++ % 2 == 0 ? upperIndices : lowerIndices).push_back(index);
    }
    const auto placeRow = [&](std::vector<std::size_t>& indices,
                              const bool aboveWing) {
      std::stable_sort(indices.begin(), indices.end(), [&](const auto left,
                                                            const auto right) {
        const double leftX = aboveWing ? halfSpan - pending[left].target.x()
                                       : pending[left].target.x();
        const double rightX = aboveWing ? halfSpan - pending[right].target.x()
                                        : pending[right].target.x();
        return leftX < rightX;
      });
      std::vector<double> widths;
      widths.reserve(indices.size());
      double totalWidth = 0.0;
      for (const auto index : indices) {
        widths.push_back(drawingTextWidth(pending[index].text, 3.5));
        totalWidth += widths.back();
      }
      constexpr double minimumGap = 24.0;
      const double naturalGap = indices.empty() ? 0.0 :
          (halfSpan - totalWidth) / static_cast<double>(indices.size() + 1);
      const double gap = std::max(minimumGap, naturalGap);
      const double rowWidth = totalWidth +
          static_cast<double>(indices.empty() ? 0 : indices.size() - 1) * gap;
      double x = (halfSpan - rowWidth) * 0.5;
      for (std::size_t column = 0; column < indices.size(); ++column) {
        const auto index = indices[column];
        const double y = aboveWing ? layout.upperRow - 55.0 : drawingBottom + 25.0;
        QPointF target = pending[index].target;
        if (aboveWing) {
          target = {halfSpan - target.x(),
                    target.y() - layout.lowerRow + layout.upperRow};
        }
        addLeader(document, {x, y}, target, pending[index].text);
        leftMost = std::min(leftMost, x);
        rightMost = std::max(rightMost, x + widths[column]);
        x += widths[column] + gap;
      }
    };
    placeRow(upperIndices, true);
    placeRow(lowerIndices, false);
  }
};

double addPlanAnnotations(TechnicalDrawingDocument& document, const PlanLayout& layout,
                          const std::vector<double>& ribThicknesses,
                          const std::vector<WingPanelData>& panelParameters,
                          const bool useInches, const double drawingBottom,
                          double& annotationLeft, double& annotationRight) {
  const double dimensionY = layout.upperRow - 80.0;
  addDimension(document, {0.0, dimensionY}, {layout.halfSpan, dimensionY},
      {0.0, layout.upperRow}, {layout.halfSpan, layout.upperRow},
      {layout.halfSpan * 0.5 - 42.0, dimensionY - 15.0},
      "FLATTENED HALF-SPAN: " + formatLength(layout.halfSpan, useInches));

  const auto& firstPanel = layout.panels.front();
  const auto& lastPanel = layout.panels.back();
  const WingPanelData* firstParameters = panelParameters.empty()
      ? nullptr : &panelParameters.front();
  const WingPanelData* lastParameters = panelParameters.empty()
      ? nullptr : &panelParameters[std::min(panelParameters.size() - 1,
                                            layout.panels.size() - 1)];
  const auto& rootRib = firstPanel.wing->ribs.front().rib;
  const auto& tipRib = lastPanel.wing->ribs.back().rib;
  const double rootLeading = layout.lowerRow + rootRib.leadingEdgeOffset -
      layout.chordMinimum;
  const double rootTrailing = rootLeading + rootRib.chord;
  const double tipLeading = layout.lowerRow + tipRib.leadingEdgeOffset -
      layout.chordMinimum;
  const double tipTrailing = tipLeading + tipRib.chord;
  addDimension(document, {-28.0, rootLeading}, {-28.0, rootTrailing},
      {0.0, rootLeading}, {0.0, rootTrailing},
      {-50.0, (rootLeading + rootTrailing) * 0.5 + 34.0},
      "ROOT CHORD: " + formatParameterLength(
          rootRib.chord, firstParameters, "rootChord", useInches), -90.0);
  addDimension(document, {layout.halfSpan + 28.0, tipLeading},
      {layout.halfSpan + 28.0, tipTrailing},
      {layout.halfSpan, tipLeading}, {layout.halfSpan, tipTrailing},
      {layout.halfSpan + 50.0, (tipLeading + tipTrailing) * 0.5 - 34.0},
      "TIP CHORD: " + formatParameterLength(
          tipRib.chord, lastParameters, "tipChord", useInches), 90.0);

  const double betweenWingDimensionY =
      0.5 * (layout.upperRow + layout.rowDepth + layout.lowerRow);
  for (std::size_t panelIndex = 0; panelIndex < layout.panels.size(); ++panelIndex) {
    const auto& panel = layout.panels[panelIndex];
    if (panel.wing->ribs.empty() || panel.span.size() < 2) continue;
    const double firstSpan = panel.span.front();
    const double lastSpan = panel.span.back();
    const double panelDimensionY = betweenWingDimensionY;
    const auto& firstRib = panel.wing->ribs.front().rib;
    const auto& lastRib = panel.wing->ribs.back().rib;
    const double firstLeading = layout.lowerRow + firstRib.leadingEdgeOffset -
        layout.chordMinimum;
    const double lastLeading = layout.lowerRow + lastRib.leadingEdgeOffset -
        layout.chordMinimum;
    const WingPanelData* parameters = panelIndex < panelParameters.size()
        ? &panelParameters[panelIndex] : nullptr;
    const QString label = QString{"PANEL %1 SPAN: %2"}
        .arg(panelIndex + 1)
        .arg(formatParameterLength(lastSpan - firstSpan, parameters,
                                   "panelSpan", useInches));
    addDimension(document, {firstSpan, panelDimensionY},
        {lastSpan, panelDimensionY},
        {firstSpan, firstLeading}, {lastSpan, lastLeading},
        {(firstSpan + lastSpan - drawingTextWidth(label, 3.5)) * 0.5,
         panelDimensionY + 4.0}, label);
  }

  CalloutPlacer callouts{
      document, layout, layout.halfSpan, drawingBottom};
  for (std::size_t panelIndex = 0; panelIndex < layout.panels.size(); ++panelIndex) {
    const auto& panel = layout.panels[panelIndex];
    const auto& wing = *panel.wing;
    const double ribThickness = panelIndex < ribThicknesses.size()
        ? ribThicknesses[panelIndex] : 0.0;
    const WingPanelData* parameters = panelIndex < panelParameters.size()
        ? &panelParameters[panelIndex] : nullptr;
    const QString panelSuffix = layout.panels.size() > 1
        ? QString{" (Panel %1)"}.arg(panelIndex + 1) : QString{};

    if (parameters != nullptr && !wing.ribs.empty()) {
      const QChar degree{0x00b0};
      const QString dihedralText = QString{"DIHEDRAL %1: %2%3"}
          .arg(panelIndex + 1)
          .arg(parameters->dihedral, 0, 'f', 1)
          .arg(degree);
      const auto& panelRootRib = wing.ribs.front().rib;
      const double panelRootTrailing =
          panelRootRib.leadingEdgeOffset + panelRootRib.chord;
      document.texts.push_back({
          {panel.span.front() - drawingTextWidth(dihedralText, 3.5) * 0.5,
           layout.lowerRow + panelRootTrailing - layout.chordMinimum + 9.0},
          dihedralText, 3.5, 0.0});

      const QString twistText = QString{"TIP TWIST %1: %2%3"}
          .arg(panelIndex + 1)
          .arg(parameters->twist, 0, 'f', 1)
          .arg(degree);
      const auto& panelTipRib = wing.ribs.back().rib;
      document.texts.push_back({
          {panel.span.back() - drawingTextWidth(twistText, 3.5) * 0.5,
           layout.lowerRow + panelTipRib.leadingEdgeOffset -
               layout.chordMinimum - 20.0},
          twistText, 3.5, 0.0});
    }

    for (std::size_t ribIndex = 0; ribIndex < wing.ribs.size(); ++ribIndex) {
      const auto& structured = wing.ribs[ribIndex];
      if (structured.name.empty()) continue;
      const double chordPosition = structured.rib.leadingEdgeOffset +
          structured.rib.chord * 0.56;
      const bool isPanelTipRib = ribIndex + 1 == wing.ribs.size();
      double labelOffset = ribThickness * 0.65;
      if (isPanelTipRib) {
        const QString ribName = QString::fromStdString(structured.name);
        const TechnicalDrawingText sample{{0.0, 0.0}, ribName, 3.0, -90.0};
        constexpr double labelGap = 2.0;
        const double ribInnerEdge =
            structured.rib.ribThicknessStartFactor * ribThickness;
        labelOffset = ribInnerEdge - drawingTextBounds(sample).width() - labelGap;
      }
      document.texts.push_back({drawingPoint(layout, false, layout.lowerRow,
          panel.span[ribIndex] + labelOffset, chordPosition),
          QString::fromStdString(structured.name), 3.0, -90.0});
    }
    if (!wing.ribs.empty() && ribThickness > 0.0) {
      const std::size_t middle = wing.ribs.size() / 2;
      const auto& rib = wing.ribs[middle].rib;
      const QString firstName = QString::fromStdString(wing.ribs.front().name);
      const QString lastName = QString::fromStdString(wing.ribs.back().name);
      callouts.add(
          drawingPoint(layout, false, layout.lowerRow, panel.span[middle],
                       rib.leadingEdgeOffset + rib.chord * 0.56),
          QString{"Ribs %1-%2%3\n%4"}
              .arg(firstName, lastName, panelSuffix,
                   formatParameterLength(
                       ribThickness, parameters, "ribThickness", useInches)));
    }

    const auto memberTarget = [&](const domain::SpanMember& member) {
      const std::size_t station = std::min(member.centers.size(), wing.ribs.size()) / 2;
      const std::size_t index = std::min(station, wing.ribs.size() - 1);
      const double chord = wing.ribs[index].rib.leadingEdgeOffset +
          member.centers[index].x;
      return drawingPoint(layout, false, layout.lowerRow, panel.span[index], chord);
    };
    const auto findMember = [&](const std::string& name) -> const domain::SpanMember* {
      const auto found = std::find_if(wing.members.begin(), wing.members.end(),
          [&](const domain::SpanMember& member) { return member.name == name; });
      return found == wing.members.end() ? nullptr : &*found;
    };
    const auto* topSpar = findMember("Top spar");
    const auto* bottomSpar = findMember("Bottom spar");
    if (topSpar != nullptr || bottomSpar != nullptr) {
      const auto& reference = topSpar != nullptr ? *topSpar : *bottomSpar;
      const bool referenceIsTop = topSpar != nullptr;
      QString dimensions = formatParameterSize(reference.width,
          referenceIsTop ? "topSparWidth" : "bottomSparWidth",
          reference.height,
          referenceIsTop ? "topSparHeight" : "bottomSparHeight",
          parameters, useInches);
      if (topSpar != nullptr && bottomSpar != nullptr &&
          (std::abs(topSpar->width - bottomSpar->width) > 1.0e-8 ||
           std::abs(topSpar->height - bottomSpar->height) > 1.0e-8)) {
        dimensions = "Top: " + formatParameterSize(
            topSpar->width, "topSparWidth", topSpar->height, "topSparHeight",
            parameters, useInches) +
            "\nBottom: " + formatParameterSize(
                bottomSpar->width, "bottomSparWidth", bottomSpar->height,
                "bottomSparHeight", parameters, useInches);
      }
      callouts.add(memberTarget(reference),
          "Top and Bottom Spars" + panelSuffix + "\n" + dimensions);
    }

    const auto* topRear = findMember("Top 60% rear spar");
    const auto* bottomRear = findMember("Bottom 60% rear spar");
    if (topRear != nullptr || bottomRear != nullptr) {
      const auto& reference = topRear != nullptr ? *topRear : *bottomRear;
      callouts.add(memberTarget(reference),
          "Top and Bottom\n60% Rear Spars" + panelSuffix + "\n" +
              formatParameterSize(reference.width,
                  topRear != nullptr ? "topRearSparWidth" : "bottomRearSparWidth",
                  reference.height,
                  topRear != nullptr ? "topRearSparHeight" : "bottomRearSparHeight",
                  parameters, useInches));
    }

    for (const auto& member : wing.members) {
      if (&member == topSpar || &member == bottomSpar ||
          &member == topRear || &member == bottomRear)
        continue;
      QString dimensions;
      QString widthKey;
      QString heightKey;
      QString innerDiameterKey;
      const bool isLeadingEdge = member.name.rfind("LE", 0) == 0;
      if (isLeadingEdge) {
        widthKey = member.kind == domain::SpanMemberKind::Tube
            ? "leadingEdgeTubeOd" : "leadingEdgeRodOd";
        innerDiameterKey = "leadingEdgeTubeId";
      } else if (member.kind == domain::SpanMemberKind::Tube ||
                 member.kind == domain::SpanMemberKind::Rod) {
        widthKey = parameters != nullptr && parameters->carbonSpar == 1
            ? "cfTubeOd" : "cfRodOd";
        innerDiameterKey = "cfTubeId";
      } else if (member.kind == domain::SpanMemberKind::Turbulator) {
        widthKey = "turbulatorWidth";
        heightKey = "turbulatorHeight";
      }
      if (member.kind == domain::SpanMemberKind::Tube ||
          member.kind == domain::SpanMemberKind::Rod) {
        dimensions = "OD: " + formatParameterLength(
            member.width, parameters, widthKey, useInches);
        if (member.innerDiameter > 0.0)
          dimensions += "  ID: " + formatParameterLength(
              member.innerDiameter, parameters, innerDiameterKey, useInches);
      } else {
        dimensions = widthKey.isEmpty()
            ? formatSize(member.width, member.height, useInches)
            : formatParameterSize(member.width, widthKey, member.height,
                                  heightKey, parameters, useInches);
      }
      QString memberLabel = QString::fromStdString(member.name) + panelSuffix;
      if (isLeadingEdge) {
        memberLabel += member.kind == domain::SpanMemberKind::Tube
            ? "\nCF Tube" : "\nCF Rod";
      }
      callouts.add(memberTarget(member), memberLabel + "\n" + dimensions);
    }

    for (const auto& member : wing.profiledMembers) {
      if (member.profiles.size() < 2) continue;
      const bool isTrailingEdge = member.name.rfind("TE", 0) == 0;
      std::size_t rangeFirst = 0;
      std::size_t rangeLast = member.profiles.size() - 1;
      if (isTrailingEdge && !member.activeRanges.empty()) {
        double longestSpan = -1.0;
        for (const auto [first, last] : member.activeRanges) {
          if (first >= panel.span.size() || last >= panel.span.size() ||
              last <= first)
            continue;
          const double span = panel.span[last] - panel.span[first];
          if (span > longestSpan) {
            longestSpan = span;
            rangeFirst = first;
            rangeLast = last;
          }
        }
      }
      const std::size_t index = rangeFirst + (rangeLast - rangeFirst) / 2;
      const auto bounds = profileBounds(
          member.profiles[index], wing.ribs[index].rib.leadingEdgeOffset);
      auto dimensions = profileDimensions(member.profiles[index]);
      if (parameters != nullptr && member.name.rfind("LE", 0) == 0 &&
          parameters->leadingEdgeType == 2) {
        dimensions = {parameters->leadingEdgeWidth, parameters->leadingEdgeHeight};
      } else if (parameters != nullptr && member.name.rfind("TE", 0) == 0 &&
                 parameters->trailingEdgeType == 2) {
        dimensions = {parameters->trailingEdgeWidth, parameters->trailingEdgeHeight};
      }
      double targetSpan = panel.span[index];
      double targetChord = (bounds.first + bounds.second) * 0.5;
      if (isTrailingEdge) {
        const auto firstBounds = profileBounds(member.profiles[rangeFirst],
            wing.ribs[rangeFirst].rib.leadingEdgeOffset);
        const auto lastBounds = profileBounds(member.profiles[rangeLast],
            wing.ribs[rangeLast].rib.leadingEdgeOffset);
        const auto& firstRib = wing.ribs[rangeFirst].rib;
        const auto& lastRib = wing.ribs[rangeLast].rib;
        const double firstFace = panelSpanAtRibFace(panel, rangeFirst,
            firstRib.ribThicknessStartFactor * ribThickness);
        const double lastFace = panelSpanAtRibFace(panel, rangeLast,
            (lastRib.ribThicknessStartFactor + 1.0) * ribThickness);
        targetSpan = (firstFace + lastFace) * 0.5;
        targetChord = (firstBounds.first + firstBounds.second +
                       lastBounds.first + lastBounds.second) * 0.25;
      }
      const QPointF target = drawingPoint(
          layout, false, layout.lowerRow, targetSpan, targetChord);
      const QString label = QString::fromStdString(member.name) + panelSuffix +
          "\n" + formatParameterSize(dimensions.first,
              isTrailingEdge ? "trailingEdgeWidth" : "leadingEdgeWidth",
              dimensions.second,
              isTrailingEdge ? "trailingEdgeHeight" : "leadingEdgeHeight",
              parameters, useInches);
      callouts.add(target, label, member.name.rfind("TE", 0) == 0);
    }

    for (const auto& control : wing.controlSurfaces) {
      if (control.profiles.empty()) continue;
      const std::size_t localIndex = control.profiles.size() / 2;
      const std::size_t ribIndex = std::min(
          control.startRibIndex + localIndex, wing.ribs.size() - 1);
      const auto bounds = profileBounds(control.profiles[localIndex],
          wing.ribs[ribIndex].rib.leadingEdgeOffset);
      auto dimensions = profileDimensions(control.profiles[localIndex]);
      QString hingePost;
      if (parameters != nullptr && control.name == "Aileron") {
        dimensions = {parameters->aileronWidth, parameters->aileronHeight};
        hingePost = "\nHinge Post: " + formatParameterSize(
            parameters->aileronHingePostWidth, "aileronHingePostWidth",
            parameters->aileronHingePostHeight, "aileronHingePostHeight",
            parameters, useInches);
      } else if (parameters != nullptr && control.name == "Flap") {
        dimensions = {parameters->flapWidth, parameters->flapHeight};
        hingePost = "\nHinge Post: " + formatParameterSize(
            parameters->flapHingePostWidth, "flapHingePostWidth",
            parameters->flapHingePostHeight, "flapHingePostHeight",
            parameters, useInches);
      }
      const bool isAileron = control.name == "Aileron";
      callouts.add(
          drawingPoint(layout, false, layout.lowerRow, panel.span[ribIndex],
                       (bounds.first + bounds.second) * 0.5),
          QString::fromStdString(control.name) + panelSuffix + "\n" +
              formatParameterSize(dimensions.first,
                  isAileron ? "aileronWidth" : "flapWidth",
                  dimensions.second,
                  isAileron ? "aileronHeight" : "flapHeight",
                  parameters, useInches) + hingePost);
    }

    std::vector<std::string> annotatedSheetingNames;
    for (const auto& sheeting : wing.sheeting) {
      if (std::find(annotatedSheetingNames.begin(), annotatedSheetingNames.end(),
                    sheeting.name) != annotatedSheetingNames.end())
        continue;
      annotatedSheetingNames.push_back(sheeting.name);
      const auto& profiles = sheeting.fullProfiles.empty()
          ? sheeting.profiles : sheeting.fullProfiles;
      if (profiles.empty()) continue;
      const std::size_t index = profiles.size() / 2;
      const std::size_t ribIndex = std::min(index, wing.ribs.size() - 1);
      const auto bounds = profileBounds(
          profiles[index], wing.ribs[ribIndex].rib.leadingEdgeOffset);
      auto dimensions = profileDimensions(profiles[index]);
      QString thicknessKey;
      if (parameters != nullptr) {
        if (sheeting.name == "LE top sheeting") {
          dimensions.second = parameters->leTopSheetThickness;
          thicknessKey = "leTopSheetThickness";
        } else if (sheeting.name == "LE bottom sheeting") {
          dimensions.second = parameters->leBottomSheetThickness;
          thicknessKey = "leBottomSheetThickness";
        } else if (sheeting.name == "TE top sheeting") {
          dimensions.second = parameters->teTopSheetThickness;
          thicknessKey = "teTopSheetThickness";
        } else if (sheeting.name == "TE bottom sheeting") {
          dimensions.second = parameters->teBottomSheetThickness;
          thicknessKey = "teBottomSheetThickness";
        }
      }
      const QString sheetingDimensions = formatLength(dimensions.first, useInches) +
          " x " + (thicknessKey.isEmpty()
              ? formatLength(dimensions.second, useInches)
              : formatParameterLength(dimensions.second, parameters,
                                      thicknessKey, useInches));
      callouts.add(
          drawingPoint(layout, false, layout.lowerRow, panel.span[ribIndex],
                       (bounds.first + bounds.second) * 0.5),
          QString::fromStdString(sheeting.name) + panelSuffix + "\n" +
              sheetingDimensions);
    }

    if (!wing.shearWebs.empty()) {
      const auto& web = wing.shearWebs.front();
      const std::size_t tip = std::clamp<std::size_t>(
          web.bayIndex, 1, wing.ribs.size() - 1);
      const std::size_t root = tip - 1;
      const double chord = wing.ribs[root].rib.leadingEdgeOffset +
          (web.stationCorners.front().x + web.stationCorners.back().x) * 0.5;
      callouts.add(
          drawingPoint(layout, false, layout.lowerRow,
                       (panel.span[root] + panel.span[tip]) * 0.5, chord),
          QString{"%1-%2%3\n%4"}
              .arg(QString::fromStdString(wing.shearWebs.front().name),
                   QString::fromStdString(wing.shearWebs.back().name), panelSuffix,
                   formatParameterLength(
                       web.thickness, parameters, "shearWebWidth", useInches)));
    }

    for (const auto& joiner : wing.joiners) {
      QPointF target;
      QString dimensions;
      if (joiner.kind == domain::SpanMemberKind::Rectangular &&
          !joiner.rectangularProfiles.empty()) {
        const std::size_t count = std::min(
            joiner.rectangularProfiles.size(), wing.ribs.size());
        const auto profileCenter = [&](const std::size_t index) {
          double low = std::numeric_limits<double>::max();
          double high = std::numeric_limits<double>::lowest();
          for (const auto& point : joiner.rectangularProfiles[index]) {
            low = std::min(low, point.x);
            high = std::max(high, point.x);
          }
          return wing.ribs[index].rib.leadingEdgeOffset + (low + high) * 0.5;
        };
        const std::size_t last = count - 1;
        double firstSpan = panelSpanAtRibFace(
            panel, 0, wing.ribs.front().rib.ribThicknessStartFactor * ribThickness);
        double firstChord = profileCenter(0);
        const double lastSpan = panelSpanAtRibFace(
            panel, last,
            wing.ribs[last].rib.ribThicknessStartFactor * ribThickness);
        const double lastChord = profileCenter(last);
        if (panelIndex > 0 && !joiner.innerRectangularProfiles.empty()) {
          domain::Point3 innerCenter;
          for (const auto point : joiner.innerRectangularProfiles.front()) {
            innerCenter.x += point.x * 0.25;
            innerCenter.y += point.y * 0.25;
            innerCenter.z += point.z * 0.25;
          }
          firstSpan = flattenedSpanForModelPoint(
              layout.panels[panelIndex - 1], innerCenter);
          firstChord = innerCenter.x;
        }
        target = drawingPoint(layout, false, layout.lowerRow,
            (firstSpan + lastSpan) * 0.5, (firstChord + lastChord) * 0.5);
        dimensions = formatParameterLength(
            joiner.outerDiameter, parameters, "shearWebWidth", useInches);
      } else if (!joiner.centers.empty()) {
        const std::size_t count = std::min(joiner.centers.size(), wing.ribs.size());
        const std::size_t last = count - 1;
        double firstSpan = panel.span.front();
        double firstChord = wing.ribs.front().rib.leadingEdgeOffset +
            joiner.centers.front().x;
        double lastSpan = panel.span[last];
        double lastChord = wing.ribs[last].rib.leadingEdgeOffset +
            joiner.centers[last].x;
        if (joiner.hasExplicitEndpoints) {
          const auto& innerPanel = panelIndex == 0
              ? panel : layout.panels[panelIndex - 1];
          firstSpan = flattenedSpanForModelPoint(
              innerPanel, joiner.innerEndpoint);
          firstChord = joiner.innerEndpoint.x;
          lastSpan = flattenedSpanForModelPoint(panel, joiner.outerEndpoint);
          lastChord = joiner.outerEndpoint.x;
        }
        target = drawingPoint(layout, false, layout.lowerRow,
            (firstSpan + lastSpan) * 0.5, (firstChord + lastChord) * 0.5);
        const bool atSixtyPercent = joiner.name.find("60%") != std::string::npos;
        const QString odKey = atSixtyPercent
            ? "fiftyPercentJoinerOd" : "behindSparJoinerOd";
        const QString idKey = atSixtyPercent
            ? "fiftyPercentJoinerId" : "behindSparJoinerId";
        dimensions = "OD: " + formatParameterLength(
            joiner.outerDiameter, parameters, odKey, useInches);
        if (joiner.innerDiameter > 0.0)
          dimensions += "  ID: " + formatParameterLength(
              joiner.innerDiameter, parameters, idKey, useInches);
      } else {
        continue;
      }
      QString joinerLabel = QString::fromStdString(joiner.name);
      const bool hasPartNumber = joiner.kind == domain::SpanMemberKind::Rectangular &&
          joinerLabel.size() > 1 && joinerLabel.front() == QChar{'J'} &&
          std::all_of(joinerLabel.cbegin() + 1, joinerLabel.cend(),
                      [](const QChar character) { return character.isDigit(); });
      if (hasPartNumber)
        joinerLabel += " joiner";
      else if (joinerLabel.compare("Aluminum tube joiner behind mid spar",
                              Qt::CaseInsensitive) == 0)
        joinerLabel = "Aluminum tube joiner";
      else if (joinerLabel.compare("CF tube joiner behind mid spar",
                                   Qt::CaseInsensitive) == 0)
        joinerLabel = "CF tube\njoiner behind\nmid spar";
      else if (joinerLabel.compare("CF rod joiner behind mid spar",
                                   Qt::CaseInsensitive) == 0)
        joinerLabel = "CF rod\njoiner behind\nmid spar";
      callouts.add(target, joinerLabel + panelSuffix + "\n" + dimensions);
    }
  }
  callouts.finish();
  if (!callouts.pending.empty()) {
    annotationLeft = std::min(annotationLeft, callouts.leftMost);
    annotationRight = std::max(annotationRight, callouts.rightMost);
  }
  return callouts.pending.empty()
      ? drawingBottom
      : drawingBottom + 55.0;
}

} // namespace

TechnicalDrawingDocument buildFlattenedWingPlan(
    const std::vector<domain::StructuredWing>& panels,
    const std::vector<double>& ribThicknesses,
    const std::vector<WingPanelData>& panelParameters,
    const bool useInches,
    const QString& projectFileName) {
  TechnicalDrawingDocument document;
  const PlanLayout layout = calculateLayout(panels);
  if (layout.panels.empty() || layout.halfSpan <= 0.0) return document;

  for (std::size_t panelIndex = 0; panelIndex < layout.panels.size(); ++panelIndex) {
    const auto& panel = layout.panels[panelIndex];
    const double thickness = panelIndex < ribThicknesses.size()
        ? std::max(0.0, ribThicknesses[panelIndex]) : 0.0;
    addPanelComponents(document, layout, panel, true, layout.upperRow,
                       thickness, panelIndex);
    addPanelComponents(document, layout, panel, false, layout.lowerRow,
                       thickness, panelIndex);
  }
  addCrossPanelJoiners(document, layout, true, layout.upperRow);
  addCrossPanelJoiners(document, layout, false, layout.lowerRow);
  for (std::size_t i = 0; i < layout.panels.size(); ++i) {
    const double thickness = i < ribThicknesses.size()
        ? std::max(0.0, ribThicknesses[i]) : 0.0;
    addPanelReferenceGeometry(
        document, layout, layout.panels[i], true, layout.upperRow, thickness);
    addPanelReferenceGeometry(
        document, layout, layout.panels[i], false, layout.lowerRow, thickness);
  }

  const double drawingBottom = layout.lowerRow + layout.rowDepth;
  const double sectionRight = addRootRibSection(document, layout);
  double annotationLeft = 0.0;
  double annotationRight = layout.halfSpan;
  const double annotationBottom = addPlanAnnotations(
      document, layout, ribThicknesses, panelParameters, useInches, drawingBottom,
      annotationLeft, annotationRight);

  constexpr double horizontalMargin = 80.0;
  constexpr double topMargin = 140.0;
  const double bottomMargin = std::max(
      135.0, annotationBottom - drawingBottom + 80.0);
  double pageLeft = std::min(-horizontalMargin,
                             annotationLeft - horizontalMargin);
  double pageRight = std::max(layout.halfSpan + horizontalMargin,
      std::max(annotationRight + horizontalMargin,
               sectionRight + horizontalMargin));
  double pageTop = -topMargin;
  double pageBottom = drawingBottom + bottomMargin;
  constexpr double borderInset = 25.4;
  constexpr double textClearance = 10.0;
  for (const auto& text : document.texts) {
    const QRectF bounds = drawingTextBounds(text);
    pageLeft = std::min(pageLeft,
                        bounds.left() - borderInset - textClearance);
    pageRight = std::max(pageRight,
                         bounds.right() + borderInset + textClearance);
    pageTop = std::min(pageTop,
                       bounds.top() - borderInset - textClearance);
    pageBottom = std::max(pageBottom,
                          bounds.bottom() + borderInset + textClearance);
  }
  document.pageBoundsMm = QRectF{pageLeft, pageTop,
      pageRight - pageLeft, pageBottom - pageTop};
  const QRectF border = document.pageBoundsMm.adjusted(
      borderInset, borderInset, -borderInset, -borderInset);
  addPolyline(document, {border.topLeft(), border.topRight(),
                         border.bottomRight(), border.bottomLeft()},
      kOutlineColor, 0.50, true);

  double fullAreaMm2 = 0.0;
  for (const auto& panel : panelParameters)
    fullAreaMm2 += panel.panelSpan * (panel.rootChord + panel.tipChord);
  const double projectedFullSpan = panels.back().ribs.empty()
      ? layout.halfSpan * 2.0
      : panels.back().ribs.back().rib.spanPosition * 2.0;
  const double aspectRatio = fullAreaMm2 > 1.0e-8
      ? projectedFullSpan * projectedFullSpan / fullAreaMm2 : 0.0;
  const double displayedArea = useInches
      ? fullAreaMm2 / (25.4 * 25.4) : fullAreaMm2 / 10000.0;
  const QString areaUnit = useInches
      ? QString{"in%1"}.arg(QChar{0x00b2})
      : QString{"dm%1"}.arg(QChar{0x00b2});
  document.texts.push_back({
      {layout.halfSpan * 0.5 - 58.0, border.bottom() - 22.0},
      QString{"WING AREA: %1 %2    ASPECT RATIO: %3"}
          .arg(displayedArea, 0, 'f', 2).arg(areaUnit)
          .arg(aspectRatio, 0, 'f', 2),
      3.5, 0.0});

  constexpr double titleRowHeight = 16.0;
  constexpr double titleBlockHeight = titleRowHeight * 3.0;
  const QString projectEntry = "PROJECT: " + projectFileName;
  QFont titleFont{"Arial"};
  titleFont.setPointSizeF(3.5 * 72.0 / 25.4);
  const QFontMetricsF titleMetrics{titleFont};
  const double requestedTitleWidth = std::max(
      125.0, titleMetrics.horizontalAdvance(projectEntry) + 8.0);
  const double titleWidth = std::min(border.width() * 0.48, requestedTitleWidth);
  const double titleLeft = border.right() - titleWidth;
  const double titleTop = border.bottom() - titleBlockHeight;
  addPolyline(document, {{titleLeft, titleTop},
                         {border.right(), titleTop}}, kOutlineColor, 0.35);
  addPolyline(document, {{titleLeft, titleTop},
                         {titleLeft, border.bottom()}}, kOutlineColor, 0.35);
  for (int row = 1; row < 3; ++row) {
    const double y = titleTop + static_cast<double>(row) * titleRowHeight;
    addPolyline(document, {{titleLeft, y}, {border.right(), y}},
                kOutlineColor, 0.35);
  }
  const double textX = titleLeft + 3.0;
  document.texts.push_back({{textX, titleTop + 2.0},
                            projectEntry, 3.5, 0.0});
  document.texts.push_back({{textX, titleTop + titleRowHeight + 2.0},
                            "SCALE: 1:1", 3.5, 0.0});
  document.texts.push_back({{textX, titleTop + titleRowHeight * 2.0 + 2.0},
                            "DATE: " + QDate::currentDate().toString("yyyy-MM-dd"),
                            3.5, 0.0});
  return document;
}

} // namespace designrc::gui
