#include "domain/DxfExporter.h"
#include "R2000Template.h"

#include <array>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace designrc::domain {
namespace {

std::string hexadecimalHandle(const std::size_t value) {
  std::ostringstream handle;
  handle << std::uppercase << std::hex << value;
  return handle.str();
}

constexpr std::string_view entitiesSectionMarker =
    "  0\nSECTION\n  2\nENTITIES\n";

std::string populatedR2000Template(
    const std::set<std::string>& requestedLayers) {
  std::set<std::string> layers;
  for (const auto& layer : requestedLayers)
    if (!layer.empty() && layer != "0" && layer != "Defpoints")
      layers.insert(layer);

  std::string document{detail::r2000Template};
  const auto layerTable = document.find("TABLE\n  2\nLAYER\n");
  if (layerTable == std::string::npos)
    throw std::runtime_error("The embedded R2000 layer table is invalid");
  const auto layerCount = document.find(" 70\n2\n", layerTable);
  const auto layerTableEnd = document.find("\n  0\nENDTAB\n", layerTable);
  if (layerCount == std::string::npos || layerTableEnd == std::string::npos)
    throw std::runtime_error("The embedded R2000 layer table is incomplete");
  document.replace(layerCount, 6, " 70\n" +
      std::to_string(2 + layers.size()) + "\n");

  std::ostringstream layerRecords;
  std::size_t layerHandle = 0x800;
  for (const auto& layer : layers) {
    layerRecords << "  0\nLAYER\n  5\n"
                 << hexadecimalHandle(layerHandle++)
                 << "\n330\n1\n100\nAcDbSymbolTableRecord\n"
                    "100\nAcDbLayerTableRecord\n  2\n"
                 << layer
                 << "\n 70\n0\n 62\n7\n  6\nContinuous\n"
                    "370\n-3\n390\n13\n";
  }
  document.insert(layerTableEnd + 1, layerRecords.str());

  const auto handseed = document.find("$HANDSEED\n  5\n");
  if (handseed == std::string::npos)
    throw std::runtime_error("The embedded R2000 hand seed is missing");
  const auto handseedValue = handseed + std::string{"$HANDSEED\n  5\n"}.size();
  const auto handseedEnd = document.find('\n', handseedValue);
  document.replace(handseedValue, handseedEnd - handseedValue, "FFFF");
  return document;
}

void writeDocumentStart(std::ostream& output,
                        const std::set<std::string>& requestedLayers) {
  const auto document = populatedR2000Template(requestedLayers);
  const auto entities = document.find(entitiesSectionMarker);
  if (entities == std::string::npos)
    throw std::runtime_error("The embedded R2000 entities section is missing");
  output << std::fixed << std::setprecision(6)
         << std::string_view{document}.substr(
                0, entities + entitiesSectionMarker.size());
}

class DxfEntityWriter {
 public:
  explicit DxfEntityWriter(std::ostream& output) : output_(output) {}

  std::ostream& entity(const std::string& type, const std::string& layer,
                       const std::string& subclass) {
    output_ << "0\n" << type << "\n5\n"
            << hexadecimalHandle(nextHandle_++)
            << "\n330\n17\n100\nAcDbEntity\n8\n" << layer
            << "\n100\n" << subclass << "\n";
    return output_;
  }

  void comment(const std::string& text) {
    output_ << "999\n" << text << "\n";
  }

 private:
  std::ostream& output_;
  std::size_t nextHandle_{0x1000};
};

void writePolyline(DxfEntityWriter& writer, const std::vector<Point2>& points,
                   const std::string& layer) {
  auto& output = writer.entity("LWPOLYLINE", layer, "AcDbPolyline");
  output << "90\n" << points.size() << "\n70\n1\n";
  for (const auto& point : points)
    output << "10\n" << point.x << "\n20\n" << point.y << "\n";
}

void writeOpenLines(DxfEntityWriter& writer, const std::vector<Point2>& points,
                    const std::string& layer) {
  for (std::size_t index = 0; index + 1 < points.size(); ++index) {
    auto& output = writer.entity("LINE", layer, "AcDbLine");
    output << "10\n" << points[index].x
           << "\n20\n" << points[index].y
           << "\n30\n0.0"
           << "\n11\n" << points[index + 1].x
           << "\n21\n" << points[index + 1].y
           << "\n31\n0.0\n";
  }
}

void writeSpline(DxfEntityWriter& writer, const PartDrawingPath& path) {
  if (path.points.size() < 3) {
    writeOpenLines(writer, path.points, path.layer);
    return;
  }
  const int degree = std::min(3, static_cast<int>(path.points.size()) - 1);
  auto& output = writer.entity("SPLINE", path.layer, "AcDbSpline");
  output << "70\n" << (path.closed ? 9 : 8)
         << "\n210\n0.0\n220\n0.0\n230\n1.0"
         << "\n71\n" << degree
         << "\n72\n0\n73\n0\n74\n" << path.points.size()
         << "\n42\n0.000001\n43\n0.000001\n44\n0.000001\n";
  for (const auto& point : path.points)
    output << "11\n" << point.x << "\n21\n" << point.y << "\n31\n0.0\n";
}

void writeDrawingPath(DxfEntityWriter& writer, const PartDrawingPath& path) {
  if (path.spline) {
    writeSpline(writer, path);
  } else if (path.closed) {
    writePolyline(writer, path.points, path.layer);
  } else {
    writeOpenLines(writer, path.points, path.layer);
  }
}

using GlyphRows = std::array<unsigned char, 7>;

GlyphRows glyphRows(const char character) {
  switch (static_cast<char>(std::toupper(static_cast<unsigned char>(character)))) {
    case 'A': return {14, 17, 17, 31, 17, 17, 17};
    case 'B': return {30, 17, 17, 30, 17, 17, 30};
    case 'C': return {15, 16, 16, 16, 16, 16, 15};
    case 'D': return {30, 17, 17, 17, 17, 17, 30};
    case 'E': return {31, 16, 16, 30, 16, 16, 31};
    case 'F': return {31, 16, 16, 30, 16, 16, 16};
    case 'G': return {15, 16, 16, 19, 17, 17, 15};
    case 'H': return {17, 17, 17, 31, 17, 17, 17};
    case 'I': return {31, 4, 4, 4, 4, 4, 31};
    case 'J': return {1, 1, 1, 1, 17, 17, 14};
    case 'K': return {17, 18, 20, 24, 20, 18, 17};
    case 'L': return {16, 16, 16, 16, 16, 16, 31};
    case 'M': return {17, 27, 21, 21, 17, 17, 17};
    case 'N': return {17, 25, 21, 19, 17, 17, 17};
    case 'O': return {14, 17, 17, 17, 17, 17, 14};
    case 'P': return {30, 17, 17, 30, 16, 16, 16};
    case 'Q': return {14, 17, 17, 17, 21, 18, 13};
    case 'R': return {30, 17, 17, 30, 20, 18, 17};
    case 'S': return {15, 16, 16, 14, 1, 1, 30};
    case 'T': return {31, 4, 4, 4, 4, 4, 4};
    case 'U': return {17, 17, 17, 17, 17, 17, 14};
    case 'V': return {17, 17, 17, 17, 17, 10, 4};
    case 'W': return {17, 17, 17, 21, 21, 21, 10};
    case 'X': return {17, 17, 10, 4, 10, 17, 17};
    case 'Y': return {17, 17, 10, 4, 4, 4, 4};
    case 'Z': return {31, 1, 2, 4, 8, 16, 31};
    case '0': return {14, 17, 19, 21, 25, 17, 14};
    case '1': return {4, 12, 4, 4, 4, 4, 14};
    case '2': return {14, 17, 1, 2, 4, 8, 31};
    case '3': return {30, 1, 1, 14, 1, 1, 30};
    case '4': return {2, 6, 10, 18, 31, 2, 2};
    case '5': return {31, 16, 16, 30, 1, 1, 30};
    case '6': return {14, 16, 16, 30, 17, 17, 14};
    case '7': return {31, 1, 2, 4, 8, 8, 8};
    case '8': return {14, 17, 17, 14, 17, 17, 14};
    case '9': return {14, 17, 17, 15, 1, 1, 14};
    case '-': return {0, 0, 0, 31, 0, 0, 0};
    case '_': return {0, 0, 0, 0, 0, 0, 31};
    case '.': return {0, 0, 0, 0, 0, 12, 12};
    case ',': return {0, 0, 0, 0, 0, 4, 8};
    case ':': return {0, 12, 12, 0, 12, 12, 0};
    case '/': return {1, 2, 2, 4, 8, 8, 16};
    case '+': return {0, 4, 4, 31, 4, 4, 0};
    case '%': return {25, 26, 2, 4, 8, 11, 19};
    case '&': return {12, 18, 20, 8, 21, 18, 13};
    case '(': return {2, 4, 8, 8, 8, 4, 2};
    case ')': return {8, 4, 2, 2, 2, 4, 8};
    case '#': return {10, 31, 10, 10, 31, 10, 0};
    case '\'': return {4, 4, 8, 0, 0, 0, 0};
    case ' ': return {};
    default: return {14, 17, 1, 2, 4, 0, 4};
  }
}

void writeLineSegment(DxfEntityWriter& writer, const Point2 start,
                      const Point2 end, const std::string& layer) {
  auto& output = writer.entity("LINE", layer, "AcDbLine");
  output << "10\n" << start.x << "\n20\n" << start.y << "\n30\n0.0"
         << "\n11\n" << end.x << "\n21\n" << end.y << "\n31\n0.0\n";
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

std::optional<PartLabelPlacement> centeredTextPlacement(
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
  return PartLabelPlacement{placement, textHeight};
}

void writeCenteredText(DxfEntityWriter& writer, const std::string& label,
                       const std::vector<Point2>& outline,
                       const std::vector<std::vector<Point2>>& exclusions = {},
                       const std::optional<PartLabelPlacement>& preferred =
                           std::nullopt) {
  const auto placement = preferred
      ? preferred : centeredTextPlacement(label, outline, exclusions);
  if (!placement) return;
  writer.comment("Part label: " + label);
  const double scale = placement->height / 8.0;
  const double width = label.empty()
      ? 0.0 : (static_cast<double>(label.size()) * 6.0 - 1.0) * scale;
  const double originX = placement->position.x - 0.5 * width;
  const double originY = placement->position.y + 3.0 * scale;
  const double halfDash = 0.32 * scale;
  for (std::size_t characterIndex = 0; characterIndex < label.size();
       ++characterIndex) {
    const auto rows = glyphRows(label[characterIndex]);
    for (std::size_t row = 0; row < rows.size(); ++row) {
      for (int column = 0; column < 5; ++column) {
        if ((rows[row] & (1u << (4 - column))) == 0) continue;
        const double x = originX +
            (static_cast<double>(characterIndex) * 6.0 + column) * scale;
        const double y = originY - static_cast<double>(row) * scale;
        writeLineSegment(
            writer, {x - halfDash, y}, {x + halfDash, y}, "ANNOTATION");
      }
    }
  }
}

void writeDocumentEnd(std::ostream& output) {
  const auto entities = detail::r2000Template.find(entitiesSectionMarker);
  if (entities == std::string_view::npos)
    throw std::runtime_error("The embedded R2000 entities section is missing");
  output << detail::r2000Template.substr(
      entities + entitiesSectionMarker.size());
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

void writeSvgDrawingPath(std::ostream& output, const PartDrawingPath& path,
                         const double minimumX, const double maximumY,
                         const double margin) {
  if (path.points.size() < 2) return;
  const auto svgX = [=](const double x) { return x - minimumX + margin; };
  const auto svgY = [=](const double y) { return maximumY - y + margin; };
  if (!path.spline || path.points.size() < 3) {
    output << (path.closed ? "    <polygon points=\"" : "    <polyline points=\"");
    for (const auto point : path.points)
      output << svgX(point.x) << ',' << svgY(point.y) << ' ';
    output << "\"/>\n";
    return;
  }
  output << "    <path d=\"M " << svgX(path.points.front().x) << ' '
         << svgY(path.points.front().y);
  for (std::size_t index = 0; index + 1 < path.points.size(); ++index) {
    const auto& previous = index == 0 ? path.points[index] : path.points[index - 1];
    const auto& first = path.points[index];
    const auto& second = path.points[index + 1];
    const auto& next = index + 2 < path.points.size()
        ? path.points[index + 2] : second;
    const Point2 control1{
        first.x + (second.x - previous.x) / 6.0,
        first.y + (second.y - previous.y) / 6.0};
    const Point2 control2{
        second.x - (next.x - first.x) / 6.0,
        second.y - (next.y - first.y) / 6.0};
    output << " C " << svgX(control1.x) << ' ' << svgY(control1.y)
           << ' ' << svgX(control2.x) << ' ' << svgY(control2.y)
           << ' ' << svgX(second.x) << ' ' << svgY(second.y);
  }
  if (path.closed) output << " Z";
  output << "\"/>\n";
}

void writeSvgPaths(const std::filesystem::path& path,
                   const std::vector<PartDrawingPath>& paths,
                   const std::string& label,
                   const std::vector<Point2>& labelOutline,
                   const std::vector<std::vector<Point2>>& exclusions = {},
                   const std::optional<PartLabelPlacement>& preferred =
                       std::nullopt) {
  const auto firstPath = std::find_if(paths.begin(), paths.end(),
      [](const auto& drawingPath) { return !drawingPath.points.empty(); });
  if (firstPath == paths.end())
    throw std::invalid_argument("An SVG part requires a valid outline");
  double minimumX = firstPath->points.front().x, maximumX = minimumX;
  double minimumY = firstPath->points.front().y, maximumY = minimumY;
  for (const auto& drawingPath : paths) for (const auto point : drawingPath.points) {
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
  for (const auto& drawingPath : paths)
    writeSvgDrawingPath(output, drawingPath, minimumX, maximumY, margin);
  output << "  </g>\n";
  if (const auto placement = preferred
          ? preferred : centeredTextPlacement(label, labelOutline, exclusions)) {
    output << "  <text x=\"" << svgX(placement->position.x) << "\" y=\""
           << svgY(placement->position.y) << "\" font-family=\"sans-serif\" font-size=\""
           << placement->height << "\" text-anchor=\"middle\" dominant-baseline=\"middle\">"
           << escapeXml(label) << "</text>\n";
  }
  output << "</svg>\n";
  if (!output) throw std::runtime_error("Unable to finish SVG file: " + path.string());
}

void writeSvg(const std::filesystem::path& path,
              const std::vector<std::vector<Point2>>& polygons,
              const std::string& label, const std::vector<Point2>& labelOutline,
              const std::vector<std::vector<Point2>>& exclusions = {},
              const std::optional<PartLabelPlacement>& preferred =
                  std::nullopt) {
  std::vector<PartDrawingPath> paths;
  paths.reserve(polygons.size());
  for (const auto& polygon : polygons) paths.push_back({polygon, "OUTLINE"});
  writeSvgPaths(path, paths, label, labelOutline, exclusions, preferred);
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

std::optional<PartLabelPlacement> partLabelPlacement(const PartDrawing& part) {
  if (part.preferredLabelPlacement)
    return part.preferredLabelPlacement;
  return centeredTextPlacement(
      part.label, part.labelOutline, part.labelExclusions);
}

PartDrawing makeStructuredRibPartDrawing(const StructuredRib& rib,
                                         const std::string& label) {
  PartDrawing drawing;
  drawing.label = label;
  const auto& finishedOutline = rib.partOutline.empty()
      ? rib.outerOutline : rib.partOutline;
  const double originalMinimumX = std::min_element(
      rib.outerOutline.begin(), rib.outerOutline.end(),
      [](const Point2 left, const Point2 right) {
        return left.x < right.x;
      })->x;
  const auto isOpenOuterCut = [&](const std::vector<Point2>& hole) {
    return !rib.partOutline.empty() &&
        std::any_of(hole.begin(), hole.end(), [&](const Point2 point) {
          return point.x < originalMinimumX - 1.0e-8;
        });
  };
  const auto appendOutline = [&](const std::vector<Point2>& outline) {
    const auto segments = makeRibOutlineSegments(outline);
    for (const auto& segment : segments)
      drawing.paths.push_back(
          {segment.points, "RIB_OUTLINE", segment.spline, false});
  };
  const auto sameCutout = [](const std::vector<Point2>& left,
                             const std::vector<Point2>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
      if (std::hypot(left[index].x - right[index].x,
                     left[index].y - right[index].y) > 1.0e-8)
        return false;
    return true;
  };
  const auto isSplitCutout = [&](const std::vector<Point2>& cutout) {
    return std::any_of(
        rib.ribSplitCutouts.begin(), rib.ribSplitCutouts.end(),
        [&](const std::vector<Point2>& splitCutout) {
          return sameCutout(cutout, splitCutout);
        });
  };
  bool exportedSplitPieces = false;
  if (!rib.ribSplitCutouts.empty()) {
    std::vector<std::pair<double, double>> splitRanges;
    splitRanges.reserve(rib.ribSplitCutouts.size());
    for (const auto& slot : rib.ribSplitCutouts) {
      if (slot.size() < 4) continue;
      const auto [minimum, maximum] = std::minmax_element(
          slot.begin(), slot.end(),
          [](const Point2 a, const Point2 b) { return a.x < b.x; });
      if (maximum->x > minimum->x + 1.0e-8)
        splitRanges.emplace_back(minimum->x, maximum->x);
    }
    std::sort(splitRanges.begin(), splitRanges.end());
    std::vector<std::pair<double, double>> mergedRanges;
    for (const auto range : splitRanges) {
      if (mergedRanges.empty() ||
          range.first > mergedRanges.back().second + 1.0e-8)
        mergedRanges.push_back(range);
      else
        mergedRanges.back().second =
            std::max(mergedRanges.back().second, range.second);
    }
    std::vector<std::vector<Point2>> pieces;
    if (!mergedRanges.empty()) {
      pieces.push_back(clipAtX(
          finishedOutline, mergedRanges.front().first, true));
      for (std::size_t index = 1; index < mergedRanges.size(); ++index) {
        auto middle = clipAtX(
            finishedOutline, mergedRanges[index - 1].second, false);
        pieces.push_back(
            clipAtX(middle, mergedRanges[index].first, true));
      }
      pieces.push_back(clipAtX(
          finishedOutline, mergedRanges.back().second, false));
    }
    if (!pieces.empty() &&
        std::all_of(pieces.begin(), pieces.end(),
            [](const std::vector<Point2>& piece) {
              return piece.size() >= 3;
            })) {
      for (const auto& piece : pieces) appendOutline(piece);
      exportedSplitPieces = true;
    }
  }
  if (!exportedSplitPieces) {
    const auto segments = !rib.partOutlineSegments.empty()
        ? rib.partOutlineSegments
        : !rib.outlineSegments.empty() ? rib.outlineSegments
                                       : makeRibOutlineSegments(finishedOutline);
    for (const auto& segment : segments)
      drawing.paths.push_back(
          {segment.points, "RIB_OUTLINE", segment.spline, false});
  }
  for (const auto& hole : rib.holes)
    drawing.paths.push_back({hole, "RIB_HOLES"});
  for (const auto& cutout : rib.booleanCutouts)
    if (!exportedSplitPieces || !isSplitCutout(cutout))
      drawing.paths.push_back({cutout, "RIB_HOLES"});
  for (const auto& hole : rib.booleanHoles)
    if (!isOpenOuterCut(hole))
      drawing.paths.push_back({hole, "RIB_HOLES"});
  for (const auto& hole : rib.positiveHalfBooleanHoles)
    if (!isOpenOuterCut(hole))
      drawing.paths.push_back({hole, "RIB_HOLES"});
  for (const auto& opening : rib.internalCutouts)
    drawing.paths.push_back({opening, "RIB_HOLES"});
  drawing.labelOutline = finishedOutline;
  drawing.labelExclusions = rib.holes;
  drawing.labelExclusions.insert(drawing.labelExclusions.end(),
      rib.booleanCutouts.begin(), rib.booleanCutouts.end());
  for (const auto& hole : rib.booleanHoles)
    if (!isOpenOuterCut(hole))
      drawing.labelExclusions.push_back(hole);
  for (const auto& hole : rib.positiveHalfBooleanHoles)
    if (!isOpenOuterCut(hole))
      drawing.labelExclusions.push_back(hole);
  drawing.labelExclusions.insert(drawing.labelExclusions.end(),
      rib.internalCutouts.begin(), rib.internalCutouts.end());
  return drawing;
}

PartDrawing makeShearWebPartDrawing(const ShearWebPart& web,
                                    const std::string& label) {
  return {label, {{web.outline, "SHEAR_WEB_OUTLINE"}}, web.outline, {}};
}

PartDrawing makeSheetStockPartDrawing(const SheetStockPart& stock,
                                      const std::string& label) {
  PartDrawing drawing{label, {{stock.outline, "SHEET_TE_OUTLINE"}},
                      stock.outline, stock.slots};
  drawing.rotateForComposite = true;
  for (const auto& slot : stock.slots)
    drawing.paths.push_back({slot, "SHEET_TE_SLOTS"});
  return drawing;
}

PartDrawing makeWoodJoinerPartDrawing(const JoinerPart& joiner,
                                      const std::string& label) {
  if (joiner.kind != SpanMemberKind::Rectangular || joiner.dxfOutline.size() < 3)
    throw std::invalid_argument("A wood joiner export requires a valid outline");
  return {label, {{joiner.dxfOutline, "WOOD_JOINER_OUTLINE"}},
          joiner.dxfOutline, {}};
}

PartDrawing makeSpoilerPartDrawing(const SpoilerPart& spoiler,
                                   const std::string& label) {
  if (spoiler.dxfOutline.size() < 3)
    throw std::invalid_argument("A spoiler export requires a valid outline");
  PartDrawing drawing{label, {{spoiler.dxfOutline, "SPOILER_OUTLINE"}},
                      spoiler.dxfOutline, spoiler.lighteningHoleOutlines};
  for (const auto& hole : spoiler.lighteningHoleOutlines)
    drawing.paths.push_back({hole, "SPOILER_LIGHTENING_HOLES"});
  if (!spoiler.lighteningHoleOutlines.empty()) {
    const auto maximumY = std::max_element(
        spoiler.dxfOutline.begin(), spoiler.dxfOutline.end(),
        [](const Point2 left, const Point2 right) {
          return left.y < right.y;
        });
    const auto [minimumX, maximumX] = std::minmax_element(
        spoiler.dxfOutline.begin(), spoiler.dxfOutline.end(),
        [](const Point2 left, const Point2 right) {
          return left.x < right.x;
        });
    double holeTop = std::numeric_limits<double>::lowest();
    for (const auto& hole : spoiler.lighteningHoleOutlines)
      for (const auto point : hole)
        holeTop = std::max(holeTop, point.y);
    const double clearHeight = std::max(0.0, maximumY->y - holeTop);
    drawing.preferredLabelPlacement = PartLabelPlacement{
        {0.5 * (minimumX->x + maximumX->x),
         holeTop + 0.5 * clearHeight},
        std::max(0.2, std::min(5.0, clearHeight * 0.6))};
  }
  return drawing;
}

PartDrawing makeDihedralAnglePartDrawing(const double dihedralDegrees,
                                         const std::string& label) {
  const auto outline = dihedralAngleOutline(dihedralDegrees);
  return {label, {{outline, "DIHEDRAL_ANGLE_OUTLINE"}}, outline, {}};
}

std::vector<PartDrawing> arrangePartDrawings(
    const std::vector<PartDrawing>& parts, const double gap) {
  if (gap < 0.0) throw std::invalid_argument("Part spacing cannot be negative");
  struct Bounds {
    double minimumX{};
    double maximumX{};
    double minimumY{};
    double maximumY{};
  };
  const auto boundsFor = [](const PartDrawing& part) {
    Bounds bounds{std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::lowest()};
    for (const auto& path : part.paths) for (const auto point : path.points) {
      bounds.minimumX = std::min(bounds.minimumX, point.x);
      bounds.maximumX = std::max(bounds.maximumX, point.x);
      bounds.minimumY = std::min(bounds.minimumY, point.y);
      bounds.maximumY = std::max(bounds.maximumY, point.y);
    }
    if (bounds.minimumX > bounds.maximumX)
      throw std::invalid_argument("A composite export requires valid part outlines");
    return bounds;
  };
  std::vector<PartDrawing> layoutParts = parts;
  const auto rotateMinusNinety = [](std::vector<Point2>& points) {
    for (auto& point : points) point = {point.y, -point.x};
  };
  for (auto& part : layoutParts) {
    if (!part.rotateForComposite) continue;
    for (auto& path : part.paths) rotateMinusNinety(path.points);
    rotateMinusNinety(part.labelOutline);
    for (auto& exclusion : part.labelExclusions)
      rotateMinusNinety(exclusion);
    if (part.preferredLabelPlacement) {
      auto& point = part.preferredLabelPlacement->position;
      point = {point.y, -point.x};
    }
  }
  std::stable_sort(layoutParts.begin(), layoutParts.end(),
      [&](const PartDrawing& left, const PartDrawing& right) {
        const auto leftBounds = boundsFor(left);
        const auto rightBounds = boundsFor(right);
        return leftBounds.maximumY - leftBounds.minimumY >
               rightBounds.maximumY - rightBounds.minimumY;
      });
  std::vector<Bounds> bounds;
  bounds.reserve(layoutParts.size());
  double totalArea = 0.0;
  double widest = 0.0;
  for (const auto& part : layoutParts) {
    const auto partBounds = boundsFor(part);
    bounds.push_back(partBounds);
    const double width = partBounds.maximumX - partBounds.minimumX;
    const double height = partBounds.maximumY - partBounds.minimumY;
    widest = std::max(widest, width);
    totalArea += (width + gap) * (height + gap);
  }
  const double targetWidth = std::max(widest, std::sqrt(totalArea) * 1.6);
  std::vector<PartDrawing> arranged;
  arranged.reserve(layoutParts.size());
  double cursorX = 0.0;
  double cursorY = 0.0;
  double rowHeight = 0.0;
  const auto translate = [](std::vector<Point2>& points,
                            const double dx, const double dy) {
    for (auto& point : points) {
      point.x += dx;
      point.y += dy;
    }
  };
  for (std::size_t index = 0; index < layoutParts.size(); ++index) {
    const double width = bounds[index].maximumX - bounds[index].minimumX;
    const double height = bounds[index].maximumY - bounds[index].minimumY;
    if (cursorX > 0.0 && cursorX + width > targetWidth) {
      cursorX = 0.0;
      cursorY += rowHeight + gap;
      rowHeight = 0.0;
    }
    auto part = layoutParts[index];
    const double dx = cursorX - bounds[index].minimumX;
    const double dy = cursorY - bounds[index].minimumY;
    for (auto& path : part.paths) translate(path.points, dx, dy);
    translate(part.labelOutline, dx, dy);
    for (auto& exclusion : part.labelExclusions) translate(exclusion, dx, dy);
    if (part.preferredLabelPlacement) {
      part.preferredLabelPlacement->position.x += dx;
      part.preferredLabelPlacement->position.y += dy;
    }
    arranged.push_back(std::move(part));
    cursorX += width + gap;
    rowHeight = std::max(rowHeight, height);
  }
  return arranged;
}

void exportPartsDxf(const std::vector<PartDrawing>& parts,
                    const std::filesystem::path& path) {
  const auto arranged = arrangePartDrawings(parts);
  if (arranged.empty())
    throw std::invalid_argument("A composite DXF requires at least one part");
  std::set<std::string> layers{"ANNOTATION"};
  for (const auto& part : arranged)
    for (const auto& drawingPath : part.paths)
      layers.insert(drawingPath.layer);
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(output, layers);
  DxfEntityWriter writer{output};
  for (const auto& part : arranged) {
    for (const auto& drawingPath : part.paths)
      writeDrawingPath(writer, drawingPath);
    writeCenteredText(writer, part.label, part.labelOutline,
                      part.labelExclusions, part.preferredLabelPlacement);
  }
  writeDocumentEnd(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportPartsSvg(const std::vector<PartDrawing>& parts,
                    const std::filesystem::path& path) {
  const auto arranged = arrangePartDrawings(parts);
  if (arranged.empty())
    throw std::invalid_argument("A composite SVG requires at least one part");
  double minimumX = std::numeric_limits<double>::max();
  double maximumX = std::numeric_limits<double>::lowest();
  double minimumY = std::numeric_limits<double>::max();
  double maximumY = std::numeric_limits<double>::lowest();
  for (const auto& part : arranged)
    for (const auto& drawingPath : part.paths)
      for (const auto point : drawingPath.points) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
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
  for (const auto& part : arranged) for (const auto& drawingPath : part.paths) {
    writeSvgDrawingPath(output, drawingPath, minimumX, maximumY, margin);
  }
  output << "  </g>\n";
  for (const auto& part : arranged) {
    if (const auto placement = partLabelPlacement(part)) {
      output << "  <text x=\"" << svgX(placement->position.x) << "\" y=\""
             << svgY(placement->position.y)
             << "\" font-family=\"sans-serif\" font-size=\""
             << placement->height
             << "\" text-anchor=\"middle\" dominant-baseline=\"middle\">"
             << escapeXml(part.label) << "</text>\n";
    }
  }
  output << "</svg>\n";
  if (!output) throw std::runtime_error("Unable to finish SVG file: " + path.string());
}

void exportRibDxf(
    const RibDefinition& rib,
    const std::filesystem::path& path,
    const std::string& label) {
  if (rib.chord <= 0.0 || rib.profile.outline().size() < 3)
    throw std::invalid_argument("A DXF rib requires a valid chord and outline");

  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(output, {"RIB_OUTLINE", "ANNOTATION"});
  DxfEntityWriter writer{output};
  std::vector<Point2> outline;
  outline.reserve(rib.profile.outline().size());
  for (const auto& point : rib.profile.outline()) outline.push_back({point.x * rib.chord, point.y * rib.chord});
  for (const auto& segment : makeRibOutlineSegments(outline))
    writeDrawingPath(writer,
        {segment.points, "RIB_OUTLINE", segment.spline, false});
  writeCenteredText(writer, label, outline);
  writeDocumentEnd(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportStructuredRibDxf(const StructuredRib& rib, const std::filesystem::path& path,
                            const std::string& label) {
  const auto drawing = makeStructuredRibPartDrawing(rib, label);
  std::set<std::string> layers{"ANNOTATION"};
  for (const auto& drawingPath : drawing.paths) layers.insert(drawingPath.layer);
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(output, layers);
  DxfEntityWriter writer{output};
  for (const auto& drawingPath : drawing.paths)
    writeDrawingPath(writer, drawingPath);
  writeCenteredText(
      writer, drawing.label, drawing.labelOutline, drawing.labelExclusions);
  writeDocumentEnd(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportShearWebDxf(const ShearWebPart& web, const std::filesystem::path& path,
                       const std::string& label) {
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(output, {"SHEAR_WEB_OUTLINE", "ANNOTATION"});
  DxfEntityWriter writer{output};
  writePolyline(writer, web.outline, "SHEAR_WEB_OUTLINE");
  writeCenteredText(writer, label, web.outline);
  writeDocumentEnd(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportSheetStockDxf(const SheetStockPart& stock, const std::filesystem::path& path,
                         const std::string& label) {
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  std::set<std::string> layers{"SHEET_TE_OUTLINE", "ANNOTATION"};
  if (!stock.slots.empty()) layers.insert("SHEET_TE_SLOTS");
  writeDocumentStart(output, layers);
  DxfEntityWriter writer{output};
  writePolyline(writer, stock.outline, "SHEET_TE_OUTLINE");
  for (const auto& slot : stock.slots)
    writePolyline(writer, slot, "SHEET_TE_SLOTS");
  writeCenteredText(writer, label, stock.outline, stock.slots);
  writeDocumentEnd(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportWoodJoinerDxf(const JoinerPart& joiner, const std::filesystem::path& path,
                         const std::string& label) {
  if (joiner.kind != SpanMemberKind::Rectangular || joiner.dxfOutline.size() < 3)
    throw std::invalid_argument("A wood joiner DXF requires a valid outline");
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(output, {"WOOD_JOINER_OUTLINE", "ANNOTATION"});
  DxfEntityWriter writer{output};
  writePolyline(writer, joiner.dxfOutline, "WOOD_JOINER_OUTLINE");
  writeCenteredText(writer, label, joiner.dxfOutline);
  writeDocumentEnd(output);
  if (!output) throw std::runtime_error("Unable to finish DXF file: " + path.string());
}

void exportSpoilerDxf(const SpoilerPart& spoiler, const std::filesystem::path& path,
                      const std::string& label) {
  if (spoiler.dxfOutline.size() < 3)
    throw std::invalid_argument("A spoiler DXF requires a valid outline");
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(
      output, {"SPOILER_OUTLINE", "SPOILER_LIGHTENING_HOLES", "ANNOTATION"});
  DxfEntityWriter writer{output};
  writePolyline(writer, spoiler.dxfOutline, "SPOILER_OUTLINE");
  for (const auto& hole : spoiler.lighteningHoleOutlines)
    writePolyline(writer, hole, "SPOILER_LIGHTENING_HOLES");
  const auto drawing = makeSpoilerPartDrawing(spoiler, label);
  writeCenteredText(
      writer, label, spoiler.dxfOutline, spoiler.lighteningHoleOutlines,
      drawing.preferredLabelPlacement);
  writeDocumentEnd(output);
}

void exportDihedralAngleDxf(const double dihedralDegrees,
                            const std::filesystem::path& path,
                            const std::string& label) {
  const auto outline = dihedralAngleOutline(dihedralDegrees);
  std::ofstream output{path};
  if (!output) throw std::runtime_error("Unable to create DXF file: " + path.string());
  writeDocumentStart(output, {"DIHEDRAL_ANGLE_OUTLINE", "ANNOTATION"});
  DxfEntityWriter writer{output};
  writePolyline(writer, outline, "DIHEDRAL_ANGLE_OUTLINE");
  writeCenteredText(writer, label, outline);
  writeDocumentEnd(output);
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
  std::vector<PartDrawingPath> paths;
  for (const auto& segment : makeRibOutlineSegments(outline))
    paths.push_back({segment.points, "RIB_OUTLINE", segment.spline, false});
  writeSvgPaths(path, paths, label, outline);
}

void exportStructuredRibSvg(const StructuredRib& rib,
                            const std::filesystem::path& path,
                            const std::string& label) {
  const auto drawing = makeStructuredRibPartDrawing(rib, label);
  writeSvgPaths(path, drawing.paths, drawing.label,
                drawing.labelOutline, drawing.labelExclusions);
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

void exportSpoilerSvg(const SpoilerPart& spoiler,
                      const std::filesystem::path& path,
                      const std::string& label) {
  if (spoiler.dxfOutline.size() < 3)
    throw std::invalid_argument("A spoiler SVG requires a valid outline");
  std::vector<std::vector<Point2>> polygons{spoiler.dxfOutline};
  polygons.insert(polygons.end(), spoiler.lighteningHoleOutlines.begin(),
                  spoiler.lighteningHoleOutlines.end());
  const auto drawing = makeSpoilerPartDrawing(spoiler, label);
  writeSvg(path, polygons, label, spoiler.dxfOutline,
           spoiler.lighteningHoleOutlines, drawing.preferredLabelPlacement);
}

void exportDihedralAngleSvg(const double dihedralDegrees,
                            const std::filesystem::path& path,
                            const std::string& label) {
  const auto outline = dihedralAngleOutline(dihedralDegrees);
  writeSvg(path, {outline}, label, outline);
}

} // namespace designrc::domain
