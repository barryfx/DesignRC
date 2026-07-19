#include "domain/DxfExporter.h"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <optional>
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

struct TextPlacement {
  Point2 position;
  double height{};
};

std::optional<TextPlacement> centeredTextPlacement(
    const std::string& label, const std::vector<Point2>& outline,
    const std::vector<std::vector<Point2>>& exclusions = {}) {
  if (outline.empty() || label.empty()) return std::nullopt;
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
  return TextPlacement{placement, textHeight};
}

void writeCenteredText(std::ostream& output, const std::string& label,
                       const std::vector<Point2>& outline,
                       const std::vector<std::vector<Point2>>& exclusions = {}) {
  const auto placement = centeredTextPlacement(label, outline, exclusions);
  if (!placement) return;
  const double x = placement->position.x;
  const double y = placement->position.y;
  output << "0\nTEXT\n8\nANNOTATION\n10\n" << x << "\n20\n" << y
         << "\n11\n" << x << "\n21\n" << y
         << "\n40\n" << placement->height << "\n72\n1\n73\n2\n1\n" << label << "\n";
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

std::string escapeXml(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    switch (character) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '\"': result += "&quot;"; break;
      case '\'': result += "&apos;"; break;
      default: result += character; break;
    }
  }
  return result;
}

void writeSvg(const std::filesystem::path& path,
              const std::vector<std::vector<Point2>>& polygons,
              const std::string& label, const std::vector<Point2>& labelOutline,
              const std::vector<std::vector<Point2>>& exclusions = {}) {
  const auto firstPolygon = std::find_if(polygons.begin(), polygons.end(),
      [](const auto& polygon) { return !polygon.empty(); });
  if (firstPolygon == polygons.end())
    throw std::invalid_argument("An SVG part requires a valid outline");

  double minimumX = firstPolygon->front().x, maximumX = minimumX;
  double minimumY = firstPolygon->front().y, maximumY = minimumY;
  for (const auto& polygon : polygons) for (const auto point : polygon) {
    minimumX = std::min(minimumX, point.x); maximumX = std::max(maximumX, point.x);
    minimumY = std::min(minimumY, point.y); maximumY = std::max(maximumY, point.y);
  }
  constexpr double margin = 2.0;
  const double width = std::max(0.1, maximumX - minimumX) + 2.0 * margin;
  const double height = std::max(0.1, maximumY - minimumY) + 2.0 * margin;
  const auto svgX = [=](const double x) { return x - minimumX + margin; };
  const auto svgY = [=](const double y) { return maximumY - y + margin; };

  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create SVG file: " + path.string());
  output << std::fixed << std::setprecision(6)
         << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
         << "mm\" height=\"" << height << "mm\" viewBox=\"0 0 " << width
         << ' ' << height << "\">\n"
         << "  <g fill=\"none\" stroke=\"#000000\" stroke-width=\"0.1\" "
            "stroke-linejoin=\"round\">\n";
  for (const auto& polygon : polygons) {
    if (polygon.size() < 2) continue;
    output << "    <polygon points=\"";
    for (const auto point : polygon) output << svgX(point.x) << ',' << svgY(point.y) << ' ';
    output << "\"/>\n";
  }
  output << "  </g>\n";
  if (const auto placement = centeredTextPlacement(label, labelOutline, exclusions)) {
    output << "  <text x=\"" << svgX(placement->position.x) << "\" y=\""
           << svgY(placement->position.y) << "\" font-family=\"sans-serif\" font-size=\""
           << placement->height << "\" text-anchor=\"middle\" dominant-baseline=\"middle\">"
           << escapeXml(label) << "</text>\n";
  }
  output << "</svg>\n";
  if (!output) throw std::runtime_error("Unable to finish SVG file: " + path.string());
}

std::vector<Point2> dihedralAngleOutline(const double dihedralDegrees) {
  constexpr double width = 25.4;
  constexpr double length = 38.1;
  const double halfAngle = 0.5 * dihedralDegrees * std::numbers::pi / 180.0;
  const double endOffset = width * std::tan(halfAngle);
  const double bottomEndX = endOffset >= 0.0 ? length - endOffset : length;
  const double topEndX = endOffset >= 0.0 ? length : length + endOffset;
  return {{0.0, 0.0}, {bottomEndX, 0.0}, {topEndX, width}, {0.0, width}};
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
  const auto outline = dihedralAngleOutline(dihedralDegrees);
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeHeader(output);
  writePolyline(output, outline, "DIHEDRAL_ANGLE_OUTLINE");
  writeCenteredText(output, label, outline);
  writeFooter(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportRibSvg(const RibDefinition& rib, const std::filesystem::path& path,
                  const std::string& label) {
  if (rib.chord <= 0.0 || rib.profile.outline().size() < 3)
    throw std::invalid_argument("An SVG rib requires a valid chord and outline");
  std::vector<Point2> outline;
  outline.reserve(rib.profile.outline().size());
  for (const auto point : rib.profile.outline())
    outline.push_back({point.x * rib.chord, point.y * rib.chord});
  writeSvg(path, {outline}, label, outline);
}

void exportStructuredRibSvg(const StructuredRib& rib,
                            const std::filesystem::path& path,
                            const std::string& label) {
  std::vector<std::vector<Point2>> polygons;
  bool exportedSplitPieces = false;
  if (rib.booleanCutouts.size() == 1 && rib.booleanCutouts.front().size() >= 4) {
    const auto& slot = rib.booleanCutouts.front();
    const auto [minimum, maximum] = std::minmax_element(slot.begin(), slot.end(),
        [](const Point2 a, const Point2 b) { return a.x < b.x; });
    auto leadingPiece = clipAtX(rib.outerOutline, minimum->x, true);
    auto trailingPiece = clipAtX(rib.outerOutline, maximum->x, false);
    if (leadingPiece.size() >= 3 && trailingPiece.size() >= 3) {
      polygons.push_back(std::move(leadingPiece));
      polygons.push_back(std::move(trailingPiece));
      exportedSplitPieces = true;
    }
  }
  if (!exportedSplitPieces) polygons.push_back(rib.outerOutline);
  polygons.insert(polygons.end(), rib.holes.begin(), rib.holes.end());
  if (!exportedSplitPieces)
    polygons.insert(polygons.end(), rib.booleanCutouts.begin(), rib.booleanCutouts.end());
  polygons.insert(polygons.end(), rib.booleanHoles.begin(), rib.booleanHoles.end());
  std::vector<std::vector<Point2>> exclusions = rib.holes;
  exclusions.insert(exclusions.end(), rib.booleanCutouts.begin(), rib.booleanCutouts.end());
  exclusions.insert(exclusions.end(), rib.booleanHoles.begin(), rib.booleanHoles.end());
  writeSvg(path, polygons, label, rib.outerOutline, exclusions);
}

void exportShearWebSvg(const ShearWebPart& web,
                       const std::filesystem::path& path,
                       const std::string& label) {
  writeSvg(path, {web.outline}, label, web.outline);
}

void exportSheetStockSvg(const SheetStockPart& stock,
                         const std::filesystem::path& path,
                         const std::string& label) {
  std::vector<std::vector<Point2>> polygons{stock.outline};
  polygons.insert(polygons.end(), stock.slots.begin(), stock.slots.end());
  writeSvg(path, polygons, label, stock.outline, stock.slots);
}

void exportWoodJoinerSvg(const JoinerPart& joiner,
                         const std::filesystem::path& path,
                         const std::string& label) {
  if (joiner.kind != SpanMemberKind::Rectangular || joiner.dxfOutline.size() < 3)
    throw std::invalid_argument("A wood joiner SVG requires a valid outline");
  writeSvg(path, {joiner.dxfOutline}, label, joiner.dxfOutline);
}

void exportDihedralAngleSvg(const double dihedralDegrees,
                            const std::filesystem::path& path,
                            const std::string& label) {
  const auto outline = dihedralAngleOutline(dihedralDegrees);
  writeSvg(path, {outline}, label, outline);
}

} // namespace designrc::domain
