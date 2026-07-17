#include "domain/AirfoilProfile.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace designrc::domain {
namespace {

constexpr double kEpsilon = 1.0e-9;

double interpolateY(const std::vector<Point2>& surface, double x) {
  if (x <= surface.front().x) return surface.front().y;
  if (x >= surface.back().x) return surface.back().y;
  const auto upper = std::lower_bound(surface.begin(), surface.end(), x,
      [](const Point2& point, double value) { return point.x < value; });
  const auto lower = std::prev(upper);
  const double width = upper->x - lower->x;
  if (std::abs(width) < kEpsilon) return (lower->y + upper->y) * 0.5;
  const double t = (x - lower->x) / width;
  return lower->y + t * (upper->y - lower->y);
}

std::pair<std::vector<Point2>, std::vector<Point2>> splitSurfaces(
    const std::vector<Point2>& outline) {
  const auto leading = std::min_element(outline.begin(), outline.end(),
      [](const Point2& a, const Point2& b) { return a.x < b.x; });
  if (leading == outline.begin() || std::next(leading) == outline.end())
    throw std::runtime_error("Airfoil outline must travel from trailing edge around the leading edge");

  std::vector<Point2> first(outline.begin(), std::next(leading));
  std::vector<Point2> second(leading, outline.end());
  std::reverse(first.begin(), first.end());
  if (second.front().x > second.back().x) std::reverse(second.begin(), second.end());

  auto sortAndUnique = [](std::vector<Point2>& surface) {
    std::sort(surface.begin(), surface.end(), [](const Point2& a, const Point2& b) {
      return a.x < b.x;
    });
    std::vector<Point2> unique;
    for (const auto& point : surface) {
      if (!unique.empty() && std::abs(unique.back().x - point.x) < kEpsilon)
        unique.back().y = (unique.back().y + point.y) * 0.5;
      else
        unique.push_back(point);
    }
    surface = std::move(unique);
  };
  sortAndUnique(first);
  sortAndUnique(second);

  const double probe = 0.4;
  if (interpolateY(first, probe) >= interpolateY(second, probe)) return {first, second};
  return {second, first};
}

} // namespace

AirfoilProfile::AirfoilProfile(std::string name, std::vector<Point2> normalizedOutline)
    : name_{std::move(name)}, outline_{std::move(normalizedOutline)} {}

AirfoilProfile AirfoilProfile::fromDat(std::istream& input) {
  std::string name;
  std::getline(input, name);
  std::vector<Point2> points;
  std::string line;
  while (std::getline(input, line)) {
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream values{line};
    Point2 point;
    if (values >> point.x >> point.y) points.push_back(point);
  }
  if (points.size() < 5) throw std::runtime_error("Airfoil DAT file contains too few coordinate pairs");

  const auto [minX, maxX] = std::minmax_element(points.begin(), points.end(),
      [](const Point2& a, const Point2& b) { return a.x < b.x; });
  const double chord = maxX->x - minX->x;
  if (chord < kEpsilon) throw std::runtime_error("Airfoil DAT file has zero chord");
  for (auto& point : points) {
    point.x = (point.x - minX->x) / chord;
    point.y /= chord;
  }
  return AirfoilProfile{name.empty() ? "Imported airfoil" : name, std::move(points)};
}

AirfoilProfile AirfoilProfile::fromDatFile(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) throw std::runtime_error("Unable to open airfoil DAT file: " + path.string());
  return fromDat(input);
}

AirfoilProfile AirfoilProfile::nacaSymmetric(double thicknessRatio, std::size_t surfaceSamples) {
  if (thicknessRatio <= 0.0 || thicknessRatio >= 0.5 || surfaceSamples < 3)
    throw std::invalid_argument("Invalid symmetric airfoil parameters");
  std::vector<Point2> points;
  points.reserve(surfaceSamples * 2 - 1);
  const auto thickness = [thicknessRatio](double x) {
    return 5.0 * thicknessRatio * (0.2969 * std::sqrt(x) - 0.1260 * x -
      0.3516 * x * x + 0.2843 * x * x * x - 0.1036 * x * x * x * x);
  };
  for (std::size_t i = surfaceSamples; i-- > 0;) {
    const double angle = std::numbers::pi * static_cast<double>(i) /
                         static_cast<double>(surfaceSamples - 1);
    const double x = 0.5 * (1.0 - std::cos(angle));
    points.push_back({x, thickness(x)});
  }
  for (std::size_t i = 1; i < surfaceSamples; ++i) {
    const double angle = std::numbers::pi * static_cast<double>(i) /
                         static_cast<double>(surfaceSamples - 1);
    const double x = 0.5 * (1.0 - std::cos(angle));
    points.push_back({x, -thickness(x)});
  }
  return AirfoilProfile{"NACA symmetric", std::move(points)};
}

std::vector<Point2> AirfoilProfile::resampled(std::size_t surfaceSamples) const {
  if (surfaceSamples < 3) throw std::invalid_argument("At least three samples per surface are required");
  const auto [upper, lower] = splitSurfaces(outline_);
  std::vector<Point2> result;
  result.reserve(surfaceSamples * 2 - 1);
  for (std::size_t i = surfaceSamples; i-- > 0;) {
    const double angle = std::numbers::pi * static_cast<double>(i) /
                         static_cast<double>(surfaceSamples - 1);
    const double x = 0.5 * (1.0 - std::cos(angle));
    result.push_back({x, interpolateY(upper, x)});
  }
  for (std::size_t i = 1; i < surfaceSamples; ++i) {
    const double angle = std::numbers::pi * static_cast<double>(i) /
                         static_cast<double>(surfaceSamples - 1);
    const double x = 0.5 * (1.0 - std::cos(angle));
    result.push_back({x, interpolateY(lower, x)});
  }
  return result;
}

AirfoilProfile AirfoilProfile::interpolate(
    const AirfoilProfile& root, const AirfoilProfile& tip, double fraction,
    std::size_t surfaceSamples) {
  if (fraction < 0.0 || fraction > 1.0) throw std::invalid_argument("Interpolation fraction must be 0..1");
  const auto rootPoints = root.resampled(surfaceSamples);
  const auto tipPoints = tip.resampled(surfaceSamples);
  std::vector<Point2> points;
  points.reserve(rootPoints.size());
  for (std::size_t i = 0; i < rootPoints.size(); ++i) {
    points.push_back({rootPoints[i].x,
      rootPoints[i].y + fraction * (tipPoints[i].y - rootPoints[i].y)});
  }
  return AirfoilProfile{root.name() + " to " + tip.name(), std::move(points)};
}

} // namespace designrc::domain

