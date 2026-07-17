#include "domain/DxfExporter.h"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace designrc::domain {
namespace {

void writeHeader(std::ostream& output) {
  output << std::fixed << std::setprecision(6)
         << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n"
         << "9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
         << "0\nSECTION\n2\nENTITIES\n";
}

void writePolyline(std::ostream& output, const std::vector<Point2>& points,
                   const std::string& layer) {
  output << "0\nLWPOLYLINE\n8\n" << layer << "\n90\n" << points.size() << "\n70\n1\n";
  for (const auto& point : points)
    output << "10\n" << point.x << "\n20\n" << point.y << "\n";
}

double distanceToSegment(const Point2 point, const Point2 a, const Point2 b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double lengthSquared = dx * dx + dy * dy;
  const double t = lengthSquared > 1.0e-16
      ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0.0, 1.0)
      : 0.0;
  return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

bool pointInPolygon(const Point2 point, const std::vector<Point2>& polygon) {
  bool inside = false;
  for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const auto a = polygon[i], b = polygon[j];
    if (((a.y > point.y) != (b.y > point.y)) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
      inside = !inside;
  }
  return inside;
}

void writeCenteredText(std::ostream& output, const std::string& label,
                       const std::vector<Point2>& outline,
                       const std::vector<std::vector<Point2>>& exclusions = {}) {
  if (outline.empty() || label.empty()) return;
  double minimumX = outline.front().x, maximumX = minimumX;
  double minimumY = outline.front().y, maximumY = minimumY;
  for (const auto& point : outline) {
    minimumX = std::min(minimumX, point.x); maximumX = std::max(maximumX, point.x);
    minimumY = std::min(minimumY, point.y); maximumY = std::max(maximumY, point.y);
  }
  const double width = std::max(0.1, maximumX - minimumX);
  const double height = std::max(0.1, maximumY - minimumY);
  Point2 placement{0.5 * (minimumX + maximumX), 0.5 * (minimumY + maximumY)};
  double bestClearance = 0.0;
  double closestToMiddle = std::numeric_limits<double>::max();
  bool foundMiddlePlacement = false;
  const double requiredClearance = std::max(1.0, std::min(3.0, height * 0.1));
  const auto distanceToEdges = [](const Point2 point, const std::vector<Point2>& polygon) {
    double distance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < polygon.size(); ++i)
      distance = std::min(distance,
          distanceToSegment(point, polygon[i], polygon[(i + 1) % polygon.size()]));
    return distance;
  };
  for (int row = 1; row < 30; ++row) {
    for (int column = 1; column < 50; ++column) {
      const Point2 candidate{minimumX + width * column / 50.0,
                             minimumY + height * row / 30.0};
      if (!pointInPolygon(candidate, outline)) continue;
      bool excluded = false;
      double clearance = distanceToEdges(candidate, outline);
      for (const auto& polygon : exclusions) {
        if (pointInPolygon(candidate, polygon)) { excluded = true; break; }
        clearance = std::min(clearance, distanceToEdges(candidate, polygon));
      }
      if (!excluded) {
        const double middleDistance = std::hypot(
            (candidate.x - 0.5 * (minimumX + maximumX)) / width,
            (candidate.y - 0.5 * (minimumY + maximumY)) / height);
        if (clearance >= requiredClearance && middleDistance < closestToMiddle) {
          closestToMiddle = middleDistance;
          placement = candidate;
          bestClearance = clearance;
          foundMiddlePlacement = true;
        } else if (!foundMiddlePlacement && clearance > bestClearance) {
          bestClearance = clearance;
          placement = candidate;
        }
      }
    }
  }
  const double textHeight = std::max(0.8, std::min({5.0, height * 0.16,
      width / std::max(2.0, 0.75 * static_cast<double>(label.size())),
      bestClearance > 0.0 ? bestClearance * 0.7 : 5.0}));
  const double x = placement.x;
  const double y = placement.y;
  output << "0\nTEXT\n8\nANNOTATION\n10\n" << x << "\n20\n" << y
         << "\n11\n" << x << "\n21\n" << y
         << "\n40\n" << textHeight << "\n72\n1\n73\n2\n1\n" << label << "\n";
}

void writeFooter(std::ostream& output) {
  output << "0\nENDSEC\n0\nEOF\n";
}

std::vector<Point2> clipAtX(const std::vector<Point2>& polygon,
                            const double boundary, const bool keepLess) {
  std::vector<Point2> result;
  if (polygon.empty()) return result;
  const auto inside = [=](const Point2 point) {
    return keepLess ? point.x <= boundary + 1.0e-8 : point.x >= boundary - 1.0e-8;
  };
  Point2 previous = polygon.back();
  bool previousInside = inside(previous);
  for (const auto current : polygon) {
    const bool currentInside = inside(current);
    if (currentInside != previousInside) {
      const double denominator = current.x - previous.x;
      const double t = std::abs(denominator) > 1.0e-12
          ? (boundary - previous.x) / denominator : 0.0;
      result.push_back({boundary, previous.y + t * (current.y - previous.y)});
    }
    if (currentInside) result.push_back(current);
    previous = current;
    previousInside = currentInside;
  }
  result.erase(std::unique(result.begin(), result.end(), [](const Point2 a, const Point2 b) {
    return std::hypot(a.x - b.x, a.y - b.y) < 1.0e-8;
  }), result.end());
  if (result.size() > 1 && std::hypot(result.front().x - result.back().x,
                                      result.front().y - result.back().y) < 1.0e-8)
    result.pop_back();
  return result;
}

} // namespace

void exportRibDxf(
    const RibDefinition& rib,
    const std::filesystem::path& path,
    const std::string& label) {
  if (rib.chord <= 0.0 || rib.profile.outline().size() < 3)
    throw std::invalid_argument("A DXF rib requires a valid chord and outline");

  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  std::vector<Point2> outline;
  outline.reserve(rib.profile.outline().size());
  for (const auto& point : rib.profile.outline()) outline.push_back({point.x * rib.chord, point.y * rib.chord});
  writePolyline(output, outline, "RIB_OUTLINE");
  writeCenteredText(output, label, outline);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportStructuredRibDxf(const StructuredRib& rib, const std::filesystem::path& path,
                            const std::string& label) {
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  bool exportedSplitPieces = false;
  if (rib.booleanCutouts.size() == 1 && rib.booleanCutouts.front().size() >= 4) {
    const auto& slot = rib.booleanCutouts.front();
    const auto [minimum, maximum] = std::minmax_element(slot.begin(), slot.end(),
        [](const Point2 a, const Point2 b) { return a.x < b.x; });
    const auto leadingPiece = clipAtX(rib.outerOutline, minimum->x, true);
    const auto trailingPiece = clipAtX(rib.outerOutline, maximum->x, false);
    if (leadingPiece.size() >= 3 && trailingPiece.size() >= 3) {
      writePolyline(output, leadingPiece, "RIB_OUTLINE");
      writePolyline(output, trailingPiece, "RIB_OUTLINE");
      exportedSplitPieces = true;
    }
  }
  if (!exportedSplitPieces) writePolyline(output, rib.outerOutline, "RIB_OUTLINE");
  for (const auto& hole : rib.holes) writePolyline(output, hole, "RIB_HOLES");
  if (!exportedSplitPieces)
    for (const auto& cutout : rib.booleanCutouts) writePolyline(output, cutout, "RIB_HOLES");
  for (const auto& hole : rib.booleanHoles) writePolyline(output, hole, "RIB_HOLES");
  std::vector<std::vector<Point2>> exclusions = rib.holes;
  exclusions.insert(exclusions.end(), rib.booleanCutouts.begin(), rib.booleanCutouts.end());
  exclusions.insert(exclusions.end(), rib.booleanHoles.begin(), rib.booleanHoles.end());
  writeCenteredText(output, label, rib.outerOutline, exclusions);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportShearWebDxf(const ShearWebPart& web, const std::filesystem::path& path,
                       const std::string& label) {
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  writePolyline(output, web.outline, "SHEAR_WEB_OUTLINE");
  writeCenteredText(output, label, web.outline);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportSheetStockDxf(const SheetStockPart& stock, const std::filesystem::path& path,
                         const std::string& label) {
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  writePolyline(output, stock.outline, "SHEET_TE_OUTLINE");
  for (const auto& slot : stock.slots) writePolyline(output, slot, "SHEET_TE_SLOTS");
  writeCenteredText(output, label, stock.outline, stock.slots);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportWoodJoinerDxf(const JoinerPart& joiner, const std::filesystem::path& path,
                         const std::string& label) {
  if (joiner.kind != SpanMemberKind::Rectangular || joiner.dxfOutline.size() < 3)
    throw std::invalid_argument("A wood joiner DXF requires a valid outline");
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  writePolyline(output, joiner.dxfOutline, "WOOD_JOINER_OUTLINE");
  writeCenteredText(output, label, joiner.dxfOutline);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportDihedralAngleDxf(const double dihedralDegrees,
                            const std::filesystem::path& path,
                            const std::string& label) {
  constexpr double width = 25.4;
  constexpr double length = 38.1;
  const double halfAngle = 0.5 * dihedralDegrees * std::numbers::pi / 180.0;
  const double endOffset = width * std::tan(halfAngle);
  const double bottomEndX = endOffset >= 0.0 ? length - endOffset : length;
  const double topEndX = endOffset >= 0.0 ? length : length + endOffset;
  const std::vector<Point2> outline{{0.0, 0.0}, {bottomEndX, 0.0},
                                    {topEndX, width}, {0.0, width}};
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  writePolyline(output, outline, "DIHEDRAL_ANGLE_OUTLINE");
  writeCenteredText(output, label, outline);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

} // namespace designrc::domain
