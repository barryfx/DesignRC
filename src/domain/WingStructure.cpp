#include "domain/WingStructure.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <thread>
#include <utility>

namespace designrc::domain {

EdgeHeightError::EdgeHeightError(std::string edgeName, const std::size_t ribIndex,
                                 const double cutHeightMm,
                                 const double specifiedHeightMm)
    : std::invalid_argument(edgeName + " cut edge at rib " +
          std::to_string(ribIndex) + " is " + std::to_string(cutHeightMm) +
          " mm, not smaller than the specified " + edgeName + " Height of " +
          std::to_string(specifiedHeightMm) + " mm"),
      edgeName_{std::move(edgeName)}, ribIndex_{ribIndex},
      cutHeightMm_{cutHeightMm}, specifiedHeightMm_{specifiedHeightMm} {}

namespace {

struct Notch {
  double centerX{};
  double width{};
  double depth{};
};

struct SurfaceRecess {
  double left{};
  double right{};
  double depth{};
  std::vector<Point2> depthProfile;
};

double interpolateY(const std::vector<Point2>& surface, const double x) {
  if (x <= surface.front().x) return surface.front().y;
  if (x >= surface.back().x) return surface.back().y;
  const auto upper = std::lower_bound(surface.begin(), surface.end(), x,
      [](const Point2& point, const double value) { return point.x < value; });
  const auto lower = std::prev(upper);
  const double t = (x - lower->x) / (upper->x - lower->x);
  return lower->y + t * (upper->y - lower->y);
}

std::pair<std::vector<Point2>, std::vector<Point2>> localSurfaces(const RibDefinition& rib) {
  const auto normalized = rib.profile.resampled(81);
  const std::size_t leading = normalized.size() / 2;
  std::vector<Point2> upper;
  std::vector<Point2> lower;
  upper.reserve(leading + 1);
  lower.reserve(leading + 1);
  for (std::size_t i = 0; i <= leading; ++i) {
    const auto& point = normalized[leading - i];
    upper.push_back({point.x * rib.chord, point.y * rib.chord});
  }
  for (std::size_t i = leading; i < normalized.size(); ++i) {
    const auto& point = normalized[i];
    lower.push_back({point.x * rib.chord, point.y * rib.chord});
  }
  return {upper, lower};
}

std::vector<Point2> clippedSurface(const std::vector<Point2>& surface,
                                   const double minimumX, const double maximumX) {
  std::vector<Point2> clipped;
  clipped.push_back({minimumX, interpolateY(surface, minimumX)});
  for (const auto& point : surface)
    if (point.x > minimumX + 1.0e-8 && point.x < maximumX - 1.0e-8)
      clipped.push_back(point);
  clipped.push_back({maximumX, interpolateY(surface, maximumX)});
  return clipped;
}

std::vector<Point2> leadingEdgeProfile(const std::vector<Point2>& upper,
                                       const std::vector<Point2>& lower,
                                       const double cutX) {
  const auto noseUpper = clippedSurface(upper, 0.0, cutX);
  const auto noseLower = clippedSurface(lower, 0.0, cutX);
  std::vector<Point2> profile;
  for (auto it = noseUpper.rbegin(); it != noseUpper.rend(); ++it) profile.push_back(*it);
  profile.insert(profile.end(), std::next(noseLower.begin()), noseLower.end());
  if (std::hypot(profile.front().x - profile.back().x,
                 profile.front().y - profile.back().y) < 1.0e-8)
    profile.pop_back();
  return profile;
}

std::vector<Point2> trailingEdgeProfile(const std::vector<Point2>& upper,
                                        const std::vector<Point2>& lower,
                                        const double cutX, const double chord) {
  const auto tailUpper = clippedSurface(upper, cutX, chord);
  const auto tailLower = clippedSurface(lower, cutX, chord);
  std::vector<Point2> profile = tailUpper;
  auto lowerIt = tailLower.rbegin();
  if (std::hypot(profile.back().x - lowerIt->x, profile.back().y - lowerIt->y) < 1.0e-8)
    ++lowerIt;
  for (; lowerIt != tailLower.rend(); ++lowerIt) profile.push_back(*lowerIt);
  return profile;
}

std::vector<Point2> resampleOpenProfile(const std::vector<Point2>& profile,
                                        const std::size_t sampleCount = 48) {
  std::vector<double> cumulative{0.0};
  cumulative.reserve(profile.size());
  for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
    const auto& a = profile[i];
    const auto& b = profile[i + 1];
    cumulative.push_back(cumulative.back() + std::hypot(b.x - a.x, b.y - a.y));
  }
  const double lengthTotal = cumulative.back();
  std::vector<Point2> result;
  result.reserve(sampleCount);
  for (std::size_t sample = 0; sample < sampleCount; ++sample) {
    const double distance = lengthTotal * static_cast<double>(sample) /
                            static_cast<double>(sampleCount - 1);
    const auto upper = std::upper_bound(cumulative.begin(), cumulative.end(), distance);
    const std::size_t segment = std::min<std::size_t>(
        static_cast<std::size_t>(std::distance(cumulative.begin(), upper) - 1),
        profile.size() - 2);
    const auto& a = profile[segment];
    const auto& b = profile[segment + 1];
    const double length = cumulative[segment + 1] - cumulative[segment];
    const double t = length > 1.0e-12 ? (distance - cumulative[segment]) / length : 0.0;
    result.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
  }
  return result;
}

std::vector<Point2> sheetingProfile(const std::vector<Point2>& surface,
                                    const double left, const double right,
                                    const double thickness, const bool top) {
  auto outer = resampleOpenProfile(clippedSurface(surface, left, right));
  std::vector<Point2> profile = outer;
  const double offset = top ? -thickness : thickness;
  for (auto it = outer.rbegin(); it != outer.rend(); ++it)
    profile.push_back({it->x, it->y + offset});
  return profile;
}

struct TeSheetingDefinition {
  bool enabled{};
  double left{};
  double thickness{};
  bool tapered{};
  double taperStart{};
  double closureStart{};
};

double requestedTeSheetingDepth(const TeSheetingDefinition& sheet,
                                const double x, const double right) {
  if (!sheet.enabled || x < sheet.left - 1.0e-8 || sheet.thickness <= 0.0)
    return 0.0;
  if (!sheet.tapered || x <= sheet.taperStart + 1.0e-8)
    return sheet.thickness;
  const double taperLength = right - sheet.taperStart;
  if (taperLength <= 1.0e-8) return 0.0;
  return sheet.thickness * std::clamp((right - x) / taperLength, 0.0, 1.0);
}

double teSheetingClosureStart(const TeSheetingDefinition& topSheet,
                              const TeSheetingDefinition& bottomSheet,
                              const double fixedTopDepth,
                              const double fixedBottomDepth,
                              const double right,
                              const std::vector<Point2>& upper,
                              const std::vector<Point2>& lower) {
  const double firstStart = std::min(
      topSheet.enabled ? topSheet.left : right,
      bottomSheet.enabled ? bottomSheet.left : right);
  if (firstStart >= right - 1.0e-8) return right;
  // End the rib at a fine, manufacturable point. The outline is explicitly
  // terminated here, so this can remain much thinner than the sheeting
  // without recreating the former zero-width exported tail.
  constexpr double minimumRetainedDepth = 0.25;
  const auto remainingDepth = [&](const double x) {
    const double available = std::max(0.0,
        interpolateY(upper, x) - interpolateY(lower, x));
    return available - requestedTeSheetingDepth(topSheet, x, right) -
        requestedTeSheetingDepth(bottomSheet, x, right) -
        fixedTopDepth - fixedBottomDepth;
  };
  constexpr int samples = 1024;
  double inside = right;
  for (int sample = 1; sample <= samples; ++sample) {
    const double x = right - (right - firstStart) *
        static_cast<double>(sample) / static_cast<double>(samples);
    if (remainingDepth(x) <= minimumRetainedDepth) {
      inside = x;
      continue;
    }
    double outside = x;
    for (int iteration = 0; iteration < 40; ++iteration) {
      const double middle = 0.5 * (outside + inside);
      if (remainingDepth(middle) <= minimumRetainedDepth)
        inside = middle;
      else
        outside = middle;
    }
    return inside;
  }
  return firstStart;
}

double resolvedTeSheetingDepth(const TeSheetingDefinition& topSheet,
                               const TeSheetingDefinition& bottomSheet,
                               const double fixedTopDepth,
                               const double fixedBottomDepth,
                               const bool top, const double x,
                               const double right,
                               const std::vector<Point2>& upper,
                               const std::vector<Point2>& lower) {
  const auto& definition = top ? topSheet : bottomSheet;
  double ownDepth = requestedTeSheetingDepth(definition, x, right);
  const double otherDepth = requestedTeSheetingDepth(
      top ? bottomSheet : topSheet, x, right);
  const double available = std::max(0.0,
      interpolateY(upper, x) - interpolateY(lower, x));
  const double requestedTeTotal = ownDepth + otherDepth;
  const double fixedDepth = fixedTopDepth + fixedBottomDepth;
  const double requestedTotal = requestedTeTotal + fixedDepth;
  if (requestedTeTotal > 1.0e-12 &&
      (requestedTotal > available ||
       x >= std::min(topSheet.closureStart, bottomSheet.closureStart) - 1.0e-8))
    ownDepth *= std::max(0.0, available - fixedDepth) /
        requestedTeTotal;
  return ownDepth;
}

std::pair<std::vector<Point2>, std::vector<Point2>> teSheetingProfile(
    const std::vector<Point2>& upper, const std::vector<Point2>& lower,
    const TeSheetingDefinition& topSheet,
    const TeSheetingDefinition& bottomSheet,
    const double fixedTopDepth, const double fixedBottomDepth, const bool top,
    const double right) {
  const auto& definition = top ? topSheet : bottomSheet;
  const auto& surface = top ? upper : lower;
  auto outer = resampleOpenProfile(
      clippedSurface(surface, definition.left, right));
  std::vector<Point2> depths;
  depths.reserve(outer.size());
  for (const auto point : outer) {
    const double ownDepth = resolvedTeSheetingDepth(
        topSheet, bottomSheet, fixedTopDepth, fixedBottomDepth,
        top, point.x, right, upper, lower);
    depths.push_back({point.x, ownDepth});
  }
  std::vector<Point2> profile = outer;
  const double direction = top ? -1.0 : 1.0;
  for (std::size_t reverse = outer.size(); reverse-- > 0;)
    profile.push_back({outer[reverse].x,
        outer[reverse].y + direction * depths[reverse].y});
  return {std::move(profile), std::move(depths)};
}

std::optional<Point2> aftCircleSurfaceIntersection(
    const std::vector<Point2>& surface, const Point2 center,
    const double radius) {
  std::optional<Point2> aftIntersection;
  for (std::size_t index = 0; index + 1 < surface.size(); ++index) {
    const auto& start = surface[index];
    const auto& end = surface[index + 1];
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double offsetX = start.x - center.x;
    const double offsetY = start.y - center.y;
    const double a = dx * dx + dy * dy;
    if (a <= 1.0e-16) continue;
    const double b = 2.0 * (offsetX * dx + offsetY * dy);
    const double c = offsetX * offsetX + offsetY * offsetY -
                     radius * radius;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < -1.0e-10) continue;
    const double root = std::sqrt(std::max(0.0, discriminant));
    for (const double t : {(-b - root) / (2.0 * a),
                           (-b + root) / (2.0 * a)}) {
      if (t < -1.0e-8 || t > 1.0 + 1.0e-8) continue;
      const double clampedT = std::clamp(t, 0.0, 1.0);
      const Point2 candidate{
          start.x + clampedT * dx, start.y + clampedT * dy};
      if (!aftIntersection || candidate.x > aftIntersection->x)
        aftIntersection = candidate;
    }
  }
  return aftIntersection;
}

std::vector<Point2> applySurfaceRecesses(const std::vector<Point2>& surface,
                                         std::vector<SurfaceRecess> recesses,
                                         const bool top) {
  if (recesses.empty()) return surface;
  for (auto& recess : recesses) {
    recess.left = std::max(recess.left, surface.front().x);
    recess.right = std::min(recess.right, surface.back().x);
  }
  recesses.erase(std::remove_if(recesses.begin(), recesses.end(),
      [](const SurfaceRecess& value) { return value.right <= value.left || value.depth <= 0.0; }),
      recesses.end());
  if (recesses.empty()) return surface;
  std::vector<double> coordinates;
  coordinates.reserve(surface.size() + recesses.size() * 2);
  for (const auto& point : surface) coordinates.push_back(point.x);
  for (const auto& recess : recesses) {
    coordinates.push_back(recess.left); coordinates.push_back(recess.right);
    for (const auto point : recess.depthProfile)
      if (point.x >= recess.left - 1.0e-8 &&
          point.x <= recess.right + 1.0e-8)
        coordinates.push_back(point.x);
  }
  std::sort(coordinates.begin(), coordinates.end());
  coordinates.erase(std::unique(coordinates.begin(), coordinates.end(),
      [](double a, double b) { return std::abs(a - b) < 1.0e-8; }), coordinates.end());
  const auto depthAtSide = [&recesses](const double x, const bool leftSide) {
    double depth = 0.0;
    for (const auto& recess : recesses) {
      const bool active = leftSide
          ? x > recess.left + 1.0e-8 && x <= recess.right + 1.0e-8
          : x >= recess.left - 1.0e-8 && x < recess.right - 1.0e-8;
      if (active)
        depth = std::max(depth, recess.depthProfile.empty()
            ? recess.depth : interpolateY(recess.depthProfile, x));
    }
    return depth;
  };
  std::vector<Point2> result;
  for (std::size_t i = 0; i < coordinates.size(); ++i) {
    const double x = coordinates[i];
    double leftDepth = depthAtSide(x, true);
    double rightDepth = depthAtSide(x, false);
    // There is no unrecessed surface outside the retained rib at its front or
    // rear boundary. When sheeting reaches one of those boundaries, keeping
    // the zero-depth side creates a narrow extruded tab at the edge.
    if (x <= surface.front().x + 1.0e-8) leftDepth = rightDepth;
    if (x >= surface.back().x - 1.0e-8) rightDepth = leftDepth;
    const double y = interpolateY(surface, x);
    const double direction = top ? -1.0 : 1.0;
    result.push_back({x, y + direction * leftDepth});
    if (std::abs(leftDepth - rightDepth) > 1.0e-8)
      result.push_back({x, y + direction * rightDepth});
  }
  return result;
}

std::vector<Point2> applyNotches(const std::vector<Point2>& surface,
                                 std::vector<Notch> notches,
                                 const bool top) {
  std::sort(notches.begin(), notches.end(), [](const Notch& a, const Notch& b) {
    return a.centerX < b.centerX;
  });
  std::vector<Point2> result;
  std::size_t source = 0;
  for (const auto& notch : notches) {
    const double left = std::max(surface.front().x, notch.centerX - notch.width * 0.5);
    const double right = std::min(surface.back().x, notch.centerX + notch.width * 0.5);
    if (right <= left) continue;
    while (source < surface.size() && surface[source].x < left) result.push_back(surface[source++]);
    // Sample the surface on the side that remains after the notch. At a
    // sheeting-to-spar boundary the recessed outline has two points at the
    // same X. Sampling exactly on that vertical step selected the unrecessed
    // point and created a triangular spike beside the spar notch in DXF.
    constexpr double sideSample = 1.0e-6;
    const double surfaceLeft = interpolateY(surface, std::max(surface.front().x, left - sideSample));
    const double surfaceRight = interpolateY(surface, std::min(surface.back().x, right + sideSample));
    const double floor = interpolateY(surface, notch.centerX) + (top ? -notch.depth : notch.depth);
    result.push_back({left, surfaceLeft});
    result.push_back({left, floor});
    result.push_back({right, floor});
    result.push_back({right, surfaceRight});
    while (source < surface.size() && surface[source].x <= right) ++source;
  }
  result.insert(result.end(), surface.begin() + static_cast<std::ptrdiff_t>(source), surface.end());
  return result;
}

std::vector<Point2> applySlopedTopNotch(const std::vector<Point2>& surface,
                                        const double left, const double right,
                                        const double leftFloor,
                                        const double rightFloor) {
  if (right <= left || left < surface.front().x || right > surface.back().x)
    return surface;
  std::vector<Point2> result;
  for (const auto& point : surface)
    if (point.x < left - 1.0e-8) result.push_back(point);
  result.push_back({left, interpolateY(surface, left)});
  result.push_back({left, leftFloor});
  result.push_back({right, rightFloor});
  result.push_back({right, interpolateY(surface, right)});
  for (const auto& point : surface)
    if (point.x > right + 1.0e-8) result.push_back(point);
  return result;
}

std::vector<Point2> circle(const Point2 center, const double diameter) {
  constexpr std::size_t samples = 48;
  std::vector<Point2> points;
  points.reserve(samples);
  for (std::size_t i = 0; i < samples; ++i) {
    const double angle = -2.0 * std::numbers::pi * static_cast<double>(i) / samples;
    points.push_back({center.x + std::cos(angle) * diameter * 0.5,
                      center.y + std::sin(angle) * diameter * 0.5});
  }
  return points;
}

double distanceToSegment(const Point2 point, const Point2 first,
                         const Point2 second) {
  const double dx = second.x - first.x;
  const double dy = second.y - first.y;
  const double lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= 1.0e-16)
    return std::hypot(point.x - first.x, point.y - first.y);
  const double projection = std::clamp(
      ((point.x - first.x) * dx + (point.y - first.y) * dy) /
          lengthSquared,
      0.0, 1.0);
  return std::hypot(
      point.x - (first.x + projection * dx),
      point.y - (first.y + projection * dy));
}

double distanceToPolygon(const Point2 point,
                         const std::vector<Point2>& polygon) {
  if (polygon.size() < 2)
    return std::numeric_limits<double>::infinity();
  double distance = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < polygon.size(); ++index)
    distance = std::min(distance, distanceToSegment(
        point, polygon[index], polygon[(index + 1) % polygon.size()]));
  return distance;
}

bool pointInPolygon(const Point2 point, const std::vector<Point2>& polygon) {
  if (polygon.size() < 3) return false;
  bool inside = false;
  for (std::size_t current = 0, previous = polygon.size() - 1;
       current < polygon.size(); previous = current++) {
    const auto& a = polygon[current];
    const auto& b = polygon[previous];
    if (((a.y > point.y) != (b.y > point.y)) &&
        point.x < (b.x - a.x) * (point.y - a.y) /
                          (b.y - a.y) +
                      a.x)
      inside = !inside;
  }
  return inside;
}

std::vector<std::vector<Point2>> ribLighteningHoleLayout(
    const StructuredRib& rib, const double borderDistance,
    const double holeDistance) {
  const auto& boundary = rib.partOutline.empty()
      ? rib.outerOutline : rib.partOutline;
  if (boundary.size() < 3) return {};
  std::vector<std::vector<Point2>> exclusions = rib.holes;
  exclusions.insert(exclusions.end(), rib.booleanCutouts.begin(),
                    rib.booleanCutouts.end());
  exclusions.insert(exclusions.end(), rib.booleanHoles.begin(),
                    rib.booleanHoles.end());
  exclusions.insert(exclusions.end(),
                    rib.positiveHalfBooleanHoles.begin(),
                    rib.positiveHalfBooleanHoles.end());
  exclusions.insert(exclusions.end(),
                    rib.negativeHalfBooleanHoles.begin(),
                    rib.negativeHalfBooleanHoles.end());
  exclusions.insert(exclusions.end(), rib.internalCutouts.begin(),
                    rib.internalCutouts.end());

  const auto [minimumX, maximumX] = std::minmax_element(
      boundary.begin(), boundary.end(),
      [](const Point2 left, const Point2 right) {
        return left.x < right.x;
      });
  const auto [minimumY, maximumY] = std::minmax_element(
      boundary.begin(), boundary.end(),
      [](const Point2 left, const Point2 right) {
        return left.y < right.y;
      });
  const auto availableRadius = [&](const Point2 point) {
    if (!pointInPolygon(point, boundary))
      return -std::numeric_limits<double>::infinity();
    double radius = distanceToPolygon(point, boundary) - borderDistance;
    for (const auto& exclusion : exclusions) {
      if (pointInPolygon(point, exclusion))
        return -std::numeric_limits<double>::infinity();
      radius = std::min(
          radius, distanceToPolygon(point, exclusion) - holeDistance);
    }
    return radius;
  };

  const double initialStep = std::clamp(borderDistance / 4.0, 0.5, 2.0);
  constexpr double minimumRadius = 1.0;
  std::vector<std::vector<Point2>> holes;
  const auto bestAtChord = [&](const double x) {
    Point2 best{x, 0.0};
    double bestRadius = -std::numeric_limits<double>::infinity();
    for (double y = minimumY->y + borderDistance;
         y <= maximumY->y - borderDistance + 1.0e-8;
         y += initialStep) {
      const Point2 candidate{x, y};
      const double radius = availableRadius(candidate);
      if (radius > bestRadius) {
        best = candidate;
        bestRadius = radius;
      }
    }
    double refinementStep = initialStep * 0.5;
    for (int refinement = 0; refinement < 5; ++refinement) {
      Point2 refinedBest = best;
      double refinedRadius = bestRadius;
      for (int yOffset = -2; yOffset <= 2; ++yOffset) {
        const Point2 candidate{
            x, best.y + yOffset * refinementStep};
        const double radius = availableRadius(candidate);
        if (radius > refinedRadius) {
          refinedBest = candidate;
          refinedRadius = radius;
        }
      }
      best = refinedBest;
      bestRadius = refinedRadius;
      refinementStep *= 0.5;
    }
    return std::pair{best, bestRadius};
  };

  double chordCursor = minimumX->x + borderDistance + minimumRadius;
  const double chordLimit =
      maximumX->x - borderDistance - minimumRadius;
  while (chordCursor <= chordLimit + 1.0e-8) {
    Point2 best{};
    double bestRadius = -std::numeric_limits<double>::infinity();
    bool found = false;
    for (double x = chordCursor; x <= chordLimit + 1.0e-8;
         x += initialStep) {
      auto [candidate, radius] = bestAtChord(x);
      if (radius + 1.0e-8 < minimumRadius) continue;
      best = candidate;
      bestRadius = radius;
      found = true;
      break;
    }
    if (!found) break;

    // Grow the circle locally without allowing it to jump ahead and leave
    // an unused chordwise pocket. This preserves deliberate LE-to-TE coverage
    // while still adapting every diameter to the local airfoil depth.
    double xStep = initialStep * 0.5;
    for (int refinement = 0; refinement < 5; ++refinement) {
      for (int xOffset = -1; xOffset <= 1; ++xOffset) {
        const double x = best.x + xOffset * xStep;
        if (x < chordCursor || x > chordLimit) continue;
        auto [candidate, radius] = bestAtChord(x);
        if (radius > bestRadius) {
          best = candidate;
          bestRadius = radius;
        }
      }
      xStep *= 0.5;
    }
    auto hole = circle(best, bestRadius * 2.0);
    exclusions.push_back(hole);
    holes.push_back(std::move(hole));
    chordCursor =
        best.x + bestRadius + holeDistance + minimumRadius;
  }
  return holes;
}

std::vector<std::vector<Point2>> spoilerLighteningHoleLayout(
    const double span, const double width, const double borderDistance,
    const double circleDistance, const bool spansCenter) {
  const double diameter = width - 2.0 * borderDistance;
  if (span <= 0.0 || diameter <= 0.0)
    throw std::invalid_argument(
        "Spoiler dimensions do not leave room for lightening holes");
  std::vector<std::vector<Point2>> holes;
  const auto addSegment = [&](const double start, const double end) {
    const double length = end - start;
    const double usableLength = length - 2.0 * borderDistance;
    if (usableLength + 1.0e-8 < diameter)
      throw std::invalid_argument(
          "Spoiler span does not leave room for a lightening hole at the "
          "specified Min Border Distance");
    const std::size_t count = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::floor(
            (usableLength + circleDistance) /
                (diameter + circleDistance) +
            1.0e-10)));
    const double radius = diameter * 0.5;
    if (count == 1) {
      holes.push_back(circle({0.5 * (start + end), width * 0.5}, diameter));
      return;
    }
    const double firstCenter = start + borderDistance + radius;
    const double lastCenter = end - borderDistance - radius;
    const double step = (lastCenter - firstCenter) /
        static_cast<double>(count - 1);
    for (std::size_t index = 0; index < count; ++index)
      holes.push_back(circle(
          {firstCenter + step * static_cast<double>(index), width * 0.5},
          diameter));
  };
  if (spansCenter) {
    const double center = span * 0.5;
    addSegment(0.0, center);
    addSegment(center, span);
  } else {
    addSegment(0.0, span);
  }
  return holes;
}

std::vector<Point2> rectangle(const std::array<Point2, 4>& corners) {
  return {corners.begin(), corners.end()};
}

Point2 surfaceCenter(const RibDefinition& rib, const double fraction, const bool top,
                     const double height) {
  const auto [upper, lower] = localSurfaces(rib);
  const double x = fraction * rib.chord;
  const double surface = interpolateY(top ? upper : lower, x);
  return {x, surface + (top ? -height * 0.5 : height * 0.5)};
}

Point2 camberCenter(const RibDefinition& rib, const double x) {
  const auto [upper, lower] = localSurfaces(rib);
  return {x, (interpolateY(upper, x) + interpolateY(lower, x)) * 0.5};
}

Point2 modelPlanePoint(const RibDefinition& rib, const Point2 local) {
  const double twist = rib.twistDegrees * std::numbers::pi / 180.0;
  const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
  const double sectionX = std::cos(twist) * local.x - std::sin(twist) * local.y;
  const double sectionZ = std::sin(twist) * local.x + std::cos(twist) * local.y;
  return {rib.leadingEdgeOffset + sectionX,
          rib.dihedralHeight + std::cos(plane) * sectionZ};
}

Point2 localPlanePoint(const RibDefinition& rib, const Point2 model) {
  const double angle = rib.twistDegrees * std::numbers::pi / 180.0;
  const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
  const double x = model.x - rib.leadingEdgeOffset;
  const double planeCosine = std::cos(plane);
  const double y = (model.y - rib.dihedralHeight) /
      (std::abs(planeCosine) < 1.0e-9 ? 1.0 : planeCosine);
  return {std::cos(angle) * x + std::sin(angle) * y,
          -std::sin(angle) * x + std::cos(angle) * y};
}

std::vector<Point2> straightMemberCenters(const std::vector<RibDefinition>& ribs,
                                          const std::vector<Point2>& nominalCenters) {
  const Point2 start = modelPlanePoint(ribs.front(), nominalCenters.front());
  const Point2 finish = modelPlanePoint(ribs.back(), nominalCenters.back());
  const double span = ribs.back().spanPosition - ribs.front().spanPosition;
  std::vector<Point2> centers;
  centers.reserve(ribs.size());
  for (std::size_t i = 0; i < ribs.size(); ++i) {
    const double t = span > 1.0e-12
        ? (ribs[i].spanPosition - ribs.front().spanPosition) / span : 0.0;
    centers.push_back(localPlanePoint(ribs[i],
        {start.x + (finish.x - start.x) * t, start.y + (finish.y - start.y) * t}));
  }
  return centers;
}

std::vector<Point2> angledMemberCenters(const std::vector<RibDefinition>& ribs,
                                        const std::vector<Point2>& nominalCenters,
                                        const double axisAngleDegrees) {
  const Point2 start = modelPlanePoint(ribs.front(), nominalCenters.front());
  const double slope = std::tan(axisAngleDegrees * std::numbers::pi / 180.0);
  std::vector<Point2> centers;
  centers.reserve(ribs.size());
  for (std::size_t i = 0; i < ribs.size(); ++i) {
    const double modelX = start.x;
    const double modelZ = start.y + slope * (ribs[i].spanPosition - ribs.front().spanPosition);
    centers.push_back(localPlanePoint(ribs[i], {modelX, modelZ}));
  }
  return centers;
}

std::vector<Point2> straightExposedLeadingEdgeCenters(
    const std::vector<RibDefinition>& ribs, const double diameter) {
  const double radius = diameter * 0.5;
  constexpr double exposure = 0.1;
  const double centerX = std::max(0.001, radius - exposure);
  std::vector<Point2> nominal;
  nominal.reserve(ribs.size());
  for (const auto& rib : ribs)
    nominal.push_back(camberCenter(rib, centerX));
  return straightMemberCenters(ribs, nominal);
}

struct FinishedRibOutline {
  std::vector<Point2> points;
  std::vector<RibOutlineSegment> segments;
};

std::vector<RibOutlineSegment> makeOpenRibOutlineSegments(
    const std::vector<Point2>& points) {
  if (points.size() < 2) return {};
  const std::size_t edgeCount = points.size() - 1;
  const auto isVertical = [&](const std::size_t edge) {
    return std::abs(points[edge].x - points[edge + 1].x) < 1.0e-8 &&
        std::hypot(points[edge].x - points[edge + 1].x,
                   points[edge].y - points[edge + 1].y) > 1.0e-8;
  };
  std::vector<bool> straight(edgeCount, false);
  for (std::size_t edge = 0; edge < edgeCount; ++edge)
    straight[edge] = isVertical(edge);
  for (std::size_t edge = 1; edge + 1 < edgeCount; ++edge)
    if (isVertical(edge - 1) && isVertical(edge + 1))
      straight[edge] = true;
  std::vector<RibOutlineSegment> result;
  std::vector<Point2> splinePoints;
  const auto flushSpline = [&] {
    if (splinePoints.size() >= 2) {
      const bool spline = splinePoints.size() >= 3;
      result.push_back({std::move(splinePoints), spline});
    }
    splinePoints.clear();
  };
  for (std::size_t edge = 0; edge < edgeCount; ++edge) {
    if (straight[edge]) {
      flushSpline();
      result.push_back({{points[edge], points[edge + 1]}, false});
    } else {
      if (splinePoints.empty()) splinePoints.push_back(points[edge]);
      splinePoints.push_back(points[edge + 1]);
    }
  }
  flushSpline();
  return result;
}

FinishedRibOutline exposedLeadingEdgeOutline(
    const std::vector<Point2>& outline, const Point2 center,
    const double radius) {
  if (outline.size() < 3 || radius <= 0.0)
    return {outline, makeRibOutlineSegments(outline)};
  const auto inside = [&](const Point2 point) {
    return std::hypot(point.x - center.x, point.y - center.y) <
        radius - 1.0e-8;
  };
  const auto intersection = [&](const Point2 first, const Point2 second) {
    const double dx = second.x - first.x;
    const double dy = second.y - first.y;
    const double fx = first.x - center.x;
    const double fy = first.y - center.y;
    const double a = dx * dx + dy * dy;
    const double b = 2.0 * (fx * dx + fy * dy);
    const double c = fx * fx + fy * fy - radius * radius;
    const double discriminant = std::max(0.0, b * b - 4.0 * a * c);
    const double root = std::sqrt(discriminant);
    const double firstT = (-b - root) / (2.0 * a);
    const double secondT = (-b + root) / (2.0 * a);
    const double t = firstT >= -1.0e-8 && firstT <= 1.0 + 1.0e-8
        ? firstT : secondT;
    return Point2{first.x + std::clamp(t, 0.0, 1.0) * dx,
                  first.y + std::clamp(t, 0.0, 1.0) * dy};
  };
  std::optional<std::size_t> entryEdge;
  std::optional<std::size_t> exitEdge;
  Point2 upperIntersection;
  Point2 lowerIntersection;
  for (std::size_t index = 0; index + 1 < outline.size(); ++index) {
    const auto first = outline[index];
    const auto second = outline[index + 1];
    const bool firstInside = inside(first);
    const bool secondInside = inside(second);
    if (!entryEdge && !firstInside && secondInside) {
      entryEdge = index;
      upperIntersection = intersection(first, second);
    }
    if (entryEdge && firstInside && !secondInside) {
      exitEdge = index;
      lowerIntersection = intersection(first, second);
    }
  }
  if (!entryEdge || !exitEdge || *exitEdge <= *entryEdge)
    return {outline, makeRibOutlineSegments(outline)};

  // Sheeting recess walls can cross the exposed-LE circle several times.
  // Retaining the first temporary exit leaves those manufactured steps inside
  // the circular cradle, and a spline fitted through them produces a hook at
  // the lower tip of the notch. Remove the complete run from the first entry
  // through the final exit instead.
  std::vector<Point2> before(
      outline.begin(),
      outline.begin() + static_cast<std::ptrdiff_t>(*entryEdge + 1));
  before.push_back(upperIntersection);
  std::vector<Point2> after{lowerIntersection};
  after.insert(after.end(),
      outline.begin() + static_cast<std::ptrdiff_t>(*exitEdge + 1),
      outline.end());
  std::vector<Point2> result = before;
  double upperAngle = std::atan2(
      upperIntersection.y - center.y, upperIntersection.x - center.x);
  double lowerAngle = std::atan2(
      lowerIntersection.y - center.y, lowerIntersection.x - center.x);
  while (lowerAngle > upperAngle) lowerAngle -= 2.0 * std::numbers::pi;
  constexpr int arcSegments = 24;
  std::vector<Point2> arc{upperIntersection};
  for (int sample = 1; sample < arcSegments; ++sample) {
    const double t = static_cast<double>(sample) / arcSegments;
    const double angle = upperAngle + (lowerAngle - upperAngle) * t;
    const Point2 point{
        center.x + radius * std::cos(angle),
        center.y + radius * std::sin(angle)};
    result.push_back(point);
    arc.push_back(point);
  }
  arc.push_back(lowerIntersection);
  result.insert(result.end(), after.begin(), after.end());
  std::vector<RibOutlineSegment> segments;
  auto beforeSegments = makeOpenRibOutlineSegments(before);
  segments.insert(segments.end(),
      std::make_move_iterator(beforeSegments.begin()),
      std::make_move_iterator(beforeSegments.end()));
  segments.push_back({std::move(arc), true});
  auto afterSegments = makeOpenRibOutlineSegments(after);
  segments.insert(segments.end(),
      std::make_move_iterator(afterSegments.begin()),
      std::make_move_iterator(afterSegments.end()));
  if (std::hypot(result.back().x - result.front().x,
                 result.back().y - result.front().y) > 1.0e-8)
    segments.push_back({{result.back(), result.front()}, false});
  return {std::move(result), std::move(segments)};
}

void addRectMember(StructuredWing& wing, const std::string& name, const double fraction,
                   const bool top, const double width, const double height,
                   const SpanMemberKind kind = SpanMemberKind::Rectangular,
                   const std::vector<double>* surfaceInsets = nullptr) {
  SpanMember member{name, kind, width, height, 0.0, {}};
  member.verticalLocation = top ? 0 : 1;
  member.cutsSheeting = kind == SpanMemberKind::Rectangular;
  member.centers.reserve(wing.ribs.size());
  for (std::size_t index = 0; index < wing.ribs.size(); ++index) {
    auto center = surfaceCenter(wing.ribs[index].rib, fraction, top, height);
    if (surfaceInsets && index < surfaceInsets->size())
      center.y += (top ? -1.0 : 1.0) * (*surfaceInsets)[index];
    member.centers.push_back(center);
  }
  wing.members.push_back(std::move(member));
}

} // namespace

std::vector<RibOutlineSegment> makeRibOutlineSegments(
    const std::vector<Point2>& closedOutline) {
  if (closedOutline.size() < 3) return {};
  const std::size_t count = closedOutline.size();
  const auto isVerticalEdge = [&](const std::size_t edge) {
    const auto& first = closedOutline[edge];
    const auto& second = closedOutline[(edge + 1) % count];
    return std::abs(first.x - second.x) < 1.0e-8 &&
        std::hypot(first.x - second.x, first.y - second.y) > 1.0e-8;
  };
  std::vector<bool> straight(count, false);
  for (std::size_t edge = 0; edge < count; ++edge)
    straight[edge] = isVerticalEdge(edge);
  // The implicit final edge closes the lower trailing-edge point back to the
  // upper trailing-edge point. It is a manufactured closure even when a DAT
  // file gives the two points slightly different X coordinates.
  if (std::hypot(closedOutline.back().x - closedOutline.front().x,
                 closedOutline.back().y - closedOutline.front().y) > 1.0e-8)
    straight.back() = true;
  // A notch or recess floor is bounded by the two vertical manufactured
  // edges. Keep that floor straight while allowing the adjoining DAT-derived
  // airfoil runs to remain smooth.
  for (std::size_t edge = 0; edge < count; ++edge) {
    const std::size_t previous = (edge + count - 1) % count;
    const std::size_t next = (edge + 1) % count;
    if (isVerticalEdge(previous) && isVerticalEdge(next))
      straight[edge] = true;
  }
  const auto separator = std::find(straight.begin(), straight.end(), true);
  std::vector<RibOutlineSegment> result;
  if (separator == straight.end()) {
    result.push_back({closedOutline, true});
  } else {
    const std::size_t separatorEdge = static_cast<std::size_t>(
        std::distance(straight.begin(), separator));
    std::vector<Point2> splinePoints;
    const auto flushSpline = [&] {
      if (splinePoints.size() >= 2) {
        const bool spline = splinePoints.size() >= 3;
        result.push_back({std::move(splinePoints), spline});
      }
      splinePoints.clear();
    };
    for (std::size_t step = 0; step < count; ++step) {
      const std::size_t edge = (separatorEdge + 1 + step) % count;
      const auto& first = closedOutline[edge];
      const auto& second = closedOutline[(edge + 1) % count];
      if (straight[edge]) {
        flushSpline();
        result.push_back({{first, second}, false});
      } else {
        if (splinePoints.empty()) splinePoints.push_back(first);
        splinePoints.push_back(second);
      }
    }
    flushSpline();
  }
  std::vector<RibOutlineSegment> splitAtLeadingEdge;
  splitAtLeadingEdge.reserve(result.size() + 1);
  for (auto& segment : result) {
    if (!segment.spline) {
      splitAtLeadingEdge.push_back(std::move(segment));
      continue;
    }
    const auto leading = std::min_element(
        segment.points.begin(), segment.points.end(),
        [](const Point2& left, const Point2& right) {
          return left.x < right.x;
        });
    const std::size_t leadingIndex = static_cast<std::size_t>(
        std::distance(segment.points.begin(), leading));
    if (leadingIndex < 2 || leadingIndex + 2 >= segment.points.size()) {
      splitAtLeadingEdge.push_back(std::move(segment));
      continue;
    }
    std::vector<Point2> upper{
        segment.points.begin(),
        segment.points.begin() + static_cast<std::ptrdiff_t>(leadingIndex + 1)};
    std::vector<Point2> lower{
        segment.points.begin() + static_cast<std::ptrdiff_t>(leadingIndex),
        segment.points.end()};
    splitAtLeadingEdge.push_back({std::move(upper), true});
    splitAtLeadingEdge.push_back({std::move(lower), true});
  }
  return splitAtLeadingEdge;
}

StructuredWing applyWingStructure(const std::vector<RibDefinition>& ribs,
                                   const StructureParameters& p) {
  if (ribs.size() < 2) throw std::invalid_argument("Wing structure requires at least two ribs");
  if (p.trailingEdgeType == 2 && (p.topTeSheeting || p.bottomTeSheeting))
    throw std::invalid_argument(
        "Sheet TE Stock cannot be combined with Top or Bottom TE Sheeting");
  const auto validateTeSheeting = [](const bool enabled,
      const double width, const double thickness, const bool tapered,
      const double taperStartLocationPercent, const char* name) {
    if (!enabled) return;
    if (width <= 0.0 || thickness <= 0.0)
      throw std::invalid_argument(std::string{name} +
          " width and thickness must be greater than zero");
    if (tapered && (taperStartLocationPercent < 0.0 ||
                    taperStartLocationPercent > 100.0))
      throw std::invalid_argument(std::string{name} +
          " Taper Start Location must be between 0% and 100%");
  };
  const bool bothTeSheeting = p.topTeSheeting && p.bottomTeSheeting;
  validateTeSheeting(p.topTeSheeting, p.topTeSheetingWidth,
      p.topTeSheetingThickness, p.topTeSheetingTaper || bothTeSheeting,
      p.topTeSheetingTaperStartLocationPercent, "Top TE Sheeting");
  validateTeSheeting(p.bottomTeSheeting, p.bottomTeSheetingWidth,
      p.bottomTeSheetingThickness, p.bottomTeSheetingTaper || bothTeSheeting,
      p.bottomTeSheetingTaperStartLocationPercent, "Bottom TE Sheeting");
  StructuredWing wing;
  wing.ribs.reserve(ribs.size());
  ProfiledSpanMember leadingStock{"Block leading edge", {}};
  ProfiledSpanMember trailingStock{"Sheet trailing edge", {}};
  const double trailingEdgeSlotDepth = std::max(0.0,
      std::min(p.trailingEdgeSlotDepth, p.trailingEdgeWidth));
  std::vector<ControlSurfacePart> controlParts;
  SpoilerPart spoiler;
  const bool buildSpoiler = p.spoilers;
  if (p.wiringHoles) {
    if (p.wiringHoleStartRib < 1 ||
        p.wiringHoleEndRib > static_cast<int>(ribs.size()) ||
        p.wiringHoleStartRib > p.wiringHoleEndRib)
      throw std::invalid_argument("Wiring Hole rib range must be within the panel");
    if (p.wiringHoleChordLocationPercent < 0 ||
        p.wiringHoleChordLocationPercent > 100 ||
        p.wiringHoleWidth <= 0.0 || p.wiringHoleHeight <= 0.0)
      throw std::invalid_argument("Wiring Hole location and dimensions are invalid");
  }
  if (buildSpoiler) {
    if (ribs.size() < 4)
      throw std::invalid_argument("Spoilers require at least four ribs");
    if (p.spoilerStartRib < 1 || p.spoilerEndRib > static_cast<int>(ribs.size()) ||
        p.spoilerEndRib < p.spoilerStartRib + 3)
      throw std::invalid_argument("Spoiler End Rib must be at least Start Rib + 3 and within the panel");
    if (p.spoilerStartRib < 2 &&
        std::abs(p.joinerDihedralDegrees) > 1.0e-8)
      throw std::invalid_argument(
          "Dihedral must be 0 degrees for a center spoiler");
    if (p.spoilerWidth <= 0.0 || p.spoilerThickness <= 0.0 ||
        p.spoilerFrameRailWidth <= 0.0 || p.spoilerSupportRailHeight <= 0.0)
      throw std::invalid_argument("Spoiler and rail dimensions must be greater than zero");
    if (p.spoilerLighteningHoles &&
        (p.spoilerMinimumWoodMargin <= 0.0 ||
         p.spoilerMinimumCircleDistance < 0.0 ||
         p.spoilerWidth <= 2.0 * p.spoilerMinimumWoodMargin))
      throw std::invalid_argument(
          "Spoiler Min Border Distance must leave room for a lightening hole");
    spoiler.startRibIndex = static_cast<std::size_t>(p.spoilerStartRib - 1);
    spoiler.endRibIndex = static_cast<std::size_t>(p.spoilerEndRib - 1);
    spoiler.chordLocationPercent = p.spoilerChordLocationPercent;
    spoiler.width = p.spoilerWidth;
    spoiler.thickness = p.spoilerThickness;
    spoiler.frameRailWidth = p.spoilerFrameRailWidth;
    spoiler.supportRailHeight = p.spoilerSupportRailHeight;
    spoiler.minimumWoodMargin = p.spoilerMinimumWoodMargin;
    spoiler.spansCenter = spoiler.startRibIndex == 0;
  }
  const auto addControl = [&](const bool enabled, const std::string& name,
                              const double width, const double hingeWidth,
                              const double hingeHeight, const int startRib,
                              const int stopRib, const bool allowTipRib) {
    if (!enabled) return;
    if (ribs.size() < 4) return;
    const int lastInternalRib = static_cast<int>(ribs.size()) - 1;
    const int lastStopRib = allowTipRib
        ? static_cast<int>(ribs.size()) : lastInternalRib;
    const auto start = static_cast<std::size_t>(
        std::clamp(startRib, 2, lastInternalRib) - 1);
    const auto stop = static_cast<std::size_t>(
        std::clamp(stopRib, 2, lastStopRib) - 1);
    if (stop <= start) return;
    controlParts.push_back({name, start, stop, width, p.controlSurfaceGap,
                            hingeWidth, hingeHeight, {}, {}});
    if (allowTipRib && stop + 1 == ribs.size()) {
      controlParts.back().cutStopRib = true;
      controlParts.back().extendThroughStopRib = true;
    }
    controlParts.back().profiles.reserve(stop - start + 1);
    controlParts.back().hingePostCenters.reserve(stop - start + 1);
  };
  if (p.flaps && p.ailerons && p.aileronStartRib < p.flapStopRib)
    throw std::invalid_argument(
        "Aileron Start Rib cannot be less than Flap Stop Rib");
  if (p.flaps && p.ailerons && p.aileronStartRib == p.flapStopRib &&
      std::abs(p.aileronWidth - p.flapWidth) > 1.0e-8)
    throw std::invalid_argument(
        "Flap Width and Aileron Width must match when their rib ranges meet");
  addControl(p.flaps, "Flap", p.flapWidth, p.flapHingePostWidth,
             p.flapHingePostHeight, p.flapStartRib, p.flapStopRib, false);
  addControl(p.ailerons, "Aileron", p.aileronWidth, p.aileronHingePostWidth,
             p.aileronHingePostHeight, p.aileronStartRib, p.aileronStopRib, true);
  if (controlParts.size() == 2 &&
      controlParts[0].name == "Flap" && controlParts[1].name == "Aileron" &&
      controlParts[0].stopRibIndex == controlParts[1].startRibIndex) {
    controlParts[0].cutStopRib = true;
    controlParts[0].extendThroughStopRib = true;
    controlParts[1].cutStartRib = true;
  }
  std::vector<Point2> carbonSparCenters;
  if (p.carbonSpar != 0) {
    std::vector<Point2> nominal;
    for (const auto& rib : ribs) nominal.push_back(camberCenter(rib, 0.25 * rib.chord));
    carbonSparCenters = straightMemberCenters(ribs, nominal);
  }
  std::vector<std::vector<Point2>> sparCenters;
  sparCenters.reserve(p.spars.size());
  for (const auto& spar : p.spars) {
    const double rootFraction =
        std::clamp(spar.chordLocationPercent, 0.0, 90.0) / 100.0;
    const double tipFraction = std::clamp(
        spar.tipChordLocationPercent >= 0.0
            ? spar.tipChordLocationPercent : spar.chordLocationPercent,
        0.0, 90.0) / 100.0;
    const bool rectangular = spar.material == 0 || spar.type == 2;
    const double height = spar.material == 0 ? spar.woodHeight :
        rectangular ? spar.stripThickness : spar.type == 0 ? spar.tubeOd : spar.rodOd;
    std::vector<Point2> centers;
    centers.reserve(ribs.size());
    const double span = ribs.back().spanPosition - ribs.front().spanPosition;
    for (const auto& rib : ribs) {
      const double along = span > 1.0e-12
          ? (rib.spanPosition - ribs.front().spanPosition) / span : 0.0;
      const double fraction =
          rootFraction + (tipFraction - rootFraction) * along;
      if (spar.verticalLocation == 0)
        centers.push_back(surfaceCenter(rib, fraction, true, height));
      else if (spar.verticalLocation == 1)
        centers.push_back(surfaceCenter(rib, fraction, false, height));
      else
        centers.push_back(camberCenter(rib, fraction * rib.chord));
    }
    // Every spar is one straight spanwise member between its specified root
    // and tip locations. Intermediate rib cuts follow that centerline.
    centers = straightMemberCenters(ribs, centers);
    sparCenters.push_back(std::move(centers));
  }
  std::vector<Point2> carbonLeadingEdgeCenters;
  if (p.leadingEdgeType == 3 || p.leadingEdgeType == 4) {
    const double diameter = p.leadingEdgeType == 3 ? p.leadingEdgeTubeOd : p.leadingEdgeRodOd;
    carbonLeadingEdgeCenters = straightExposedLeadingEdgeCenters(ribs, diameter);
  }
  const std::size_t rib2Index = std::min<std::size_t>(p.rib1aPresent ? 2 : 1, ribs.size() - 1);
  const auto addCircularJoiner = [&](const bool enabled, const int type,
                                     const double fraction, const double od, const double id,
                                     const std::string& name, const bool behindSpar) {
    if (!enabled || type == 0) return;
    std::vector<RibDefinition> joinerRibs(ribs.begin(), ribs.begin() + rib2Index + 1);
    std::vector<Point2> nominal;
    nominal.reserve(joinerRibs.size());
    for (const auto& rib : joinerRibs) {
      double x = fraction * rib.chord;
      nominal.push_back(camberCenter(rib, x));
    }
    if (behindSpar && p.carbonSpar != 0 && !carbonSparCenters.empty()) {
      const double sparDiameter = p.carbonSpar == 1 ? p.cfTubeOd : p.cfRodOd;
      constexpr double machiningClearance = 0.5;
      double requiredModelX = modelPlanePoint(joinerRibs.front(), nominal.front()).x;
      for (std::size_t i = 0; i < joinerRibs.size(); ++i)
        requiredModelX = std::max(requiredModelX,
            modelPlanePoint(joinerRibs[i], carbonSparCenters[i]).x +
                sparDiameter * 0.5 + od * 0.5 + machiningClearance);
      const auto rootModel = modelPlanePoint(joinerRibs.front(), nominal.front());
      nominal.front() = localPlanePoint(joinerRibs.front(),
          {requiredModelX, rootModel.y});
    }
    // Keep the joint-rib penetration above the section centerline. Because
    // the axis is normal to the joint rib while the adjacent panels diverge
    // from that bisector, the next-rib penetrations move below the centerline.
    nominal.front().y += od * 0.25;
    JoinerPart joiner;
    joiner.name = name;
    joiner.kind = type == 1 ? SpanMemberKind::Rod : SpanMemberKind::Tube;
    joiner.outerDiameter = od;
    joiner.innerDiameter = type == 1 ? 0.0 : id;
    joiner.stopRibIndex = rib2Index;
    joiner.centers = angledMemberCenters(
        joinerRibs, nominal, p.circularJoinerAxisAngleDegrees);
    joiner.mirrorPlaneAngleDegrees = p.joinerMirrorAngleDegrees;
    joiner.axisAngleDegrees = p.circularJoinerAxisAngleDegrees;
    joiner.spansJoint = p.circularJoinerSpansJoint;
    joiner.annotateOnBothPlanHalves = p.circularJoinerSpansJoint;
    wing.joiners.push_back(std::move(joiner));
  };
  addCircularJoiner(p.behindSparJoiner, p.behindSparJoinerType, 0.30,
      p.behindSparJoinerOd, p.behindSparJoinerId,
      p.behindSparJoinerType == 3 ? "Aluminum tube joiner behind mid spar" :
      p.behindSparJoinerType == 2 ? "CF tube joiner behind mid spar" :
                                   "CF rod joiner behind mid spar", true);
  addCircularJoiner(p.fiftyPercentJoiner, p.fiftyPercentJoinerType, 0.60,
      p.fiftyPercentJoinerOd, p.fiftyPercentJoinerId,
      p.fiftyPercentJoinerType == 3 ? "Aluminum tube joiner at 60% chord" :
      p.fiftyPercentJoinerType == 2 ? "CF tube joiner at 60% chord" :
                                     "CF rod joiner at 60% chord", false);
  if (p.centerSparWoodJoiner && (p.topSpar || p.bottomSpar)) {
    JoinerPart joiner;
    joiner.name = "Center spar wood joiner";
    joiner.kind = SpanMemberKind::Rectangular;
    joiner.outerDiameter = std::max(0.1, p.shearWebThickness);
    joiner.stopRibIndex = rib2Index;
    joiner.mirrorPlaneAngleDegrees = p.joinerMirrorAngleDegrees;
    joiner.annotateOnBothPlanHalves = true;
    double rootBottomZ = 0.0;
    double rootTopZ = 0.0;
    for (std::size_t i = 0; i <= rib2Index; ++i) {
      const auto [upper, lower] = localSurfaces(ribs[i]);
      const double x = 0.25 * ribs[i].chord;
      const double top = p.topSpar
          ? surfaceCenter(ribs[i], 0.25, true, p.topSparHeight).y - p.topSparHeight * 0.5
          : interpolateY(upper, x);
      const double bottom = p.bottomSpar
          ? surfaceCenter(ribs[i], 0.25, false, p.bottomSparHeight).y + p.bottomSparHeight * 0.5
          : interpolateY(lower, x);
      const double halfWidth = joiner.outerDiameter * 0.5;
      if (i == 0) {
        rootBottomZ = modelPlanePoint(ribs[i], {x, bottom}).y;
        rootTopZ = modelPlanePoint(ribs[i], {x, top}).y;
      }
      const double rise = std::tan(p.joinerAxisAngleDegrees * std::numbers::pi / 180.0) *
          (ribs[i].spanPosition - ribs.front().spanPosition);
      const double targetBottomZ = rootBottomZ + rise;
      const double targetTopZ = rootTopZ + rise;
      const auto localAt = [&](const double localX, const double targetZ) {
        const double modelX = modelPlanePoint(ribs[i], {localX, 0.0}).x;
        return localPlanePoint(ribs[i], {modelX, targetZ});
      };
      joiner.rectangularProfiles.push_back({localAt(x - halfWidth, targetBottomZ),
          localAt(x + halfWidth, targetBottomZ), localAt(x + halfWidth, targetTopZ),
          localAt(x - halfWidth, targetTopZ)});
    }
    const double edgeLength = std::hypot(
        ribs[rib2Index].spanPosition - ribs.front().spanPosition,
        std::tan(p.joinerAxisAngleDegrees * std::numbers::pi / 180.0) *
            (ribs[rib2Index].spanPosition - ribs.front().spanPosition));
    const auto& rootProfile = joiner.rectangularProfiles.front();
    rootBottomZ = modelPlanePoint(ribs.front(), rootProfile[0]).y;
    rootTopZ = modelPlanePoint(ribs.front(), rootProfile[2]).y;
    const double halfJointAngle = p.joinerDihedralDegrees * 0.5 *
        std::numbers::pi / 180.0;
    const double dxfRun = edgeLength * std::cos(halfJointAngle);
    const double dxfRise = edgeLength * std::sin(halfJointAngle);
    joiner.dxfOutline = {{-dxfRun, rootBottomZ + dxfRise}, {0.0, rootBottomZ},
        {dxfRun, rootBottomZ + dxfRise}, {dxfRun, rootTopZ + dxfRise},
        {0.0, rootTopZ}, {-dxfRun, rootTopZ + dxfRise}};
    wing.joiners.push_back(std::move(joiner));
  }
  const auto stopIndex = [&ribs](const int ribNumber) {
    return static_cast<std::size_t>(
        std::clamp(ribNumber, 2, static_cast<int>(ribs.size())) - 1);
  };
  const int turbulatorCount = std::clamp(p.turbulatorCount, 1, 4);
  const std::size_t leTopSheetPartCount = p.leTopSheet && p.turbulators
      ? static_cast<std::size_t>(turbulatorCount + 1) : 1;
  std::vector<SheetingPart> leTopSheets;
  leTopSheets.reserve(leTopSheetPartCount);
  for (std::size_t i = 0; i < leTopSheetPartCount; ++i)
    leTopSheets.push_back(
        {"Front top sheeting", stopIndex(p.leTopSheetStopRib), {}});
  SheetingPart leBottomSheet{"Front bottom sheeting", stopIndex(p.leBottomSheetStopRib), {}};
  SheetingPart teTopSheet{"Rear top sheeting", stopIndex(p.teTopSheetStopRib), {}};
  SheetingPart teBottomSheet{"Rear bottom sheeting", stopIndex(p.teBottomSheetStopRib), {}};
  SheetingPart topTeSheeting{"Top TE sheeting", ribs.size() - 1, {}};
  SheetingPart bottomTeSheeting{"Bottom TE sheeting", ribs.size() - 1, {}};

  const auto sparWidth = [](const SparParameters& spar) {
    return spar.material == 0 ? spar.woodWidth :
        spar.type == 2 ? spar.stripWidth :
        spar.type == 0 ? spar.tubeOd : spar.rodOd;
  };
  std::vector<double> frontTopEnds(ribs.size());
  std::vector<double> frontBottomEnds(ribs.size());
  std::vector<double> legacyTopSparInsets(ribs.size());
  std::vector<double> legacyBottomSparInsets(ribs.size());
  std::vector<double> legacyTopRearSparInsets(ribs.size());
  std::vector<double> legacyBottomRearSparInsets(ribs.size());
  const auto resolveFrontEnd = [&](const std::size_t ribIndex,
                                   const int verticalLocation,
                                   const bool upToSpar,
                                   const double stopPercent) {
    const auto& rib = ribs[ribIndex];
    double frontEnd = 0.25 * rib.chord;
    std::size_t selected = p.spars.size();
    double closestToLegacyLocation = 101.0;
    for (std::size_t index = 0; index < p.spars.size(); ++index) {
      const auto& spar = p.spars[index];
      if (spar.verticalLocation != verticalLocation) continue;
      const double distance = std::abs(spar.chordLocationPercent - 25.0);
      if (distance < closestToLegacyLocation) {
        selected = index;
        closestToLegacyLocation = distance;
      }
    }
    if (upToSpar) {
      if (selected != p.spars.size())
        frontEnd = sparCenters[selected][ribIndex].x -
            sparWidth(p.spars[selected]) * 0.5;
      else if (verticalLocation == 0 && p.topSpar)
        frontEnd -= p.topSparWidth * 0.5;
      else if (verticalLocation == 1 && p.bottomSpar)
        frontEnd -= p.bottomSparWidth * 0.5;
      return frontEnd;
    }
    frontEnd = std::clamp(stopPercent, 0.0, 100.0) / 100.0 * rib.chord;
    const auto movePastContainingSpar = [&](const double center,
                                            const double width) {
      const double forward = center - width * 0.5;
      const double aft = center + width * 0.5;
      if (frontEnd >= forward - 1.0e-8 && frontEnd <= aft + 1.0e-8)
        frontEnd = std::max(frontEnd, aft);
    };
    if (verticalLocation == 0) {
      if (p.topSpar) movePastContainingSpar(0.25 * rib.chord, p.topSparWidth);
      if (p.topRearSpar) movePastContainingSpar(0.60 * rib.chord, p.topRearSparWidth);
    } else {
      if (p.bottomSpar) movePastContainingSpar(0.25 * rib.chord, p.bottomSparWidth);
      if (p.bottomRearSpar) movePastContainingSpar(0.60 * rib.chord, p.bottomRearSparWidth);
    }
    for (std::size_t index = 0; index < p.spars.size(); ++index)
      if (p.spars[index].verticalLocation == verticalLocation)
        movePastContainingSpar(sparCenters[index][ribIndex].x,
                               sparWidth(p.spars[index]));
    return frontEnd;
  };
  for (std::size_t ribIndex = 0; ribIndex < ribs.size(); ++ribIndex) {
    frontTopEnds[ribIndex] = resolveFrontEnd(
        ribIndex, 0, p.leTopSheetUpToSpar, p.leTopSheetStopChordPercent);
    frontBottomEnds[ribIndex] = resolveFrontEnd(
        ribIndex, 1, p.leBottomSheetUpToSpar, p.leBottomSheetStopChordPercent);
    const bool topSheetActive = p.leTopSheet &&
        ribIndex <= stopIndex(p.leTopSheetStopRib);
    const bool bottomSheetActive = p.leBottomSheet &&
        ribIndex <= stopIndex(p.leBottomSheetStopRib);
    const auto covered = [](const double frontEnd, const double center,
                            const double width) {
      return frontEnd >= center + width * 0.5 - 1.0e-8;
    };
    for (std::size_t index = 0; index < p.spars.size(); ++index) {
      auto& center = sparCenters[index][ribIndex];
      const auto& spar = p.spars[index];
      const double width = sparWidth(spar);
      if (topSheetActive && spar.verticalLocation == 0 &&
          covered(frontTopEnds[ribIndex], center.x, width))
        center.y -= p.leTopSheetThickness;
      if (bottomSheetActive && spar.verticalLocation == 1 &&
          covered(frontBottomEnds[ribIndex], center.x, width))
        center.y += p.leBottomSheetThickness;
    }
    if (topSheetActive) {
      if (p.topSpar && covered(frontTopEnds[ribIndex], 0.25 * ribs[ribIndex].chord,
                              p.topSparWidth))
        legacyTopSparInsets[ribIndex] = p.leTopSheetThickness;
      if (p.topRearSpar && covered(frontTopEnds[ribIndex], 0.60 * ribs[ribIndex].chord,
                                  p.topRearSparWidth))
        legacyTopRearSparInsets[ribIndex] = p.leTopSheetThickness;
    }
    if (bottomSheetActive) {
      if (p.bottomSpar && covered(frontBottomEnds[ribIndex], 0.25 * ribs[ribIndex].chord,
                                 p.bottomSparWidth))
        legacyBottomSparInsets[ribIndex] = p.leBottomSheetThickness;
      if (p.bottomRearSpar && covered(frontBottomEnds[ribIndex], 0.60 * ribs[ribIndex].chord,
                                     p.bottomRearSparWidth))
        legacyBottomRearSparInsets[ribIndex] = p.leBottomSheetThickness;
    }
  }

  const auto topSparAftFace = [&](const std::size_t ribIndex) {
    double aftFace = 0.0;
    if (p.topSpar)
      aftFace = std::max(aftFace,
          0.25 * ribs[ribIndex].chord + p.topSparWidth * 0.5);
    if (p.topRearSpar)
      aftFace = std::max(aftFace,
          0.60 * ribs[ribIndex].chord + p.topRearSparWidth * 0.5);
    for (std::size_t sparIndex = 0; sparIndex < p.spars.size(); ++sparIndex) {
      const auto& spar = p.spars[sparIndex];
      if (spar.verticalLocation != 0) continue;
      const double width = spar.material == 0 ? spar.woodWidth :
          spar.type == 2 ? spar.stripWidth :
          spar.type == 0 ? spar.tubeOd : spar.rodOd;
      aftFace = std::max(
          aftFace, sparCenters[sparIndex][ribIndex].x + width * 0.5);
    }
    return aftFace;
  };

  for (std::size_t ribIndex = 0; ribIndex < ribs.size(); ++ribIndex) {
    const auto& rib = ribs[ribIndex];
    const auto [upper, lower] = localSurfaces(rib);
    double spoilerLeft = 0.0;
    double spoilerRight = 0.0;
    double spoilerTopLeft = 0.0;
    double spoilerTopRight = 0.0;
    if (buildSpoiler && ribIndex >= spoiler.startRibIndex && ribIndex <= spoiler.endRibIndex) {
      spoilerLeft = std::clamp(p.spoilerChordLocationPercent / 100.0 * rib.chord,
                               0.001, rib.chord - 0.001);
      // A spoiler cannot precede a top-mounted spar. If its requested position
      // reaches or crosses a spar, put the forward frame rail directly against
      // the spar's aft face. Calculating this per station preserves contact on
      // tapered wings and for straight circular spars.
      const double sparAftFace = topSparAftFace(ribIndex);
      // A nominally coincident rail/spar face can acquire a small overlapping
      // volume when the two independently lofted solids are evaluated by
      // OpenCascade. Keep a negligible machining clearance so "immediately
      // behind" remains visually touching without producing a false collision.
      constexpr double sparClearance = 0.01;
      const double minimumSpoilerLeft = sparAftFace > 0.0
          ? sparAftFace + sparClearance : 0.0;
      if (p.spoilerImmediatelyBehindSpar && sparAftFace > 0.0)
        spoilerLeft = minimumSpoilerLeft;
      else
        spoilerLeft = std::max(spoilerLeft, minimumSpoilerLeft);
      spoilerRight = spoilerLeft + 2.0 * p.spoilerFrameRailWidth +
          2.0 * spoiler.gap + p.spoilerWidth;
      if (spoilerRight >= rib.chord - 0.001)
        throw std::invalid_argument("Spoiler assembly extends beyond the trailing edge at rib " +
                                    std::to_string(ribIndex + 1));
      spoilerTopLeft = interpolateY(upper, spoilerLeft);
      spoilerTopRight = interpolateY(upper, spoilerRight);
      const auto topAt = [&](const double x) {
        const double t = (x - spoilerLeft) / (spoilerRight - spoilerLeft);
        return spoilerTopLeft + t * (spoilerTopRight - spoilerTopLeft);
      };
      const auto profile = [&](const double left, const double right,
                               const double height) {
        return std::array<Point2, 4>{{{left, topAt(left) - height},
            {right, topAt(right) - height}, {right, topAt(right)},
            {left, topAt(left)}}};
      };
      const double spoilerStart = spoilerLeft + p.spoilerFrameRailWidth + spoiler.gap;
      spoiler.forwardRailProfiles.push_back(profile(
          spoilerLeft, spoilerLeft + p.spoilerFrameRailWidth, p.spoilerThickness));
      spoiler.spoilerProfiles.push_back(profile(
          spoilerStart, spoilerStart + p.spoilerWidth, p.spoilerThickness));
      spoiler.aftRailProfiles.push_back(profile(
          spoilerRight - p.spoilerFrameRailWidth, spoilerRight, p.spoilerThickness));
      if (ribIndex == spoiler.startRibIndex || ribIndex == spoiler.endRibIndex)
        spoiler.supportProfiles.push_back(profile(
            spoilerLeft, spoilerRight, p.spoilerThickness + p.spoilerSupportRailHeight));
    }
    const bool solidLeadingEdge = p.leadingEdgeType == 2;
    const bool solidTrailingEdge = p.trailingEdgeType == 2;
    const double minimumX = solidLeadingEdge
        ? std::clamp(p.leadingEdgeWidth, 0.001, rib.chord - 0.001) : 0.0;
    const double maximumX = solidTrailingEdge
        ? std::clamp(rib.chord - p.trailingEdgeWidth, 0.001, rib.chord - 0.001)
        : rib.chord;
    const bool slottedSheet = p.trailingEdgeType == 2 && p.trailingEdgeSlotted;
    double retainedMaximumX = slottedSheet
        ? std::min(rib.chord, maximumX + trailingEdgeSlotDepth) : maximumX;
    // The slot extends the rib into the TE stock, but sheeting must stop at
    // the stock's inner face rather than following the rib into that slot.
    const double fullSheetingMaximumX = maximumX;
    double controlSheetingMaximumX = maximumX;
    if (minimumX >= maximumX)
      throw std::invalid_argument("Leading- and trailing-edge cuts overlap");
    const auto validateCutHeight = [&](const char* edgeName, const double cutX,
                                       const double specifiedHeight) {
      const double cutHeight = interpolateY(upper, cutX) - interpolateY(lower, cutX);
      if (cutHeight >= specifiedHeight - 1.0e-8) {
        throw EdgeHeightError{edgeName, ribIndex + 1, cutHeight, specifiedHeight};
      }
    };
    if (solidLeadingEdge) {
      validateCutHeight("LE", minimumX, p.leadingEdgeHeight);
      leadingStock.profiles.push_back(resampleOpenProfile(
          leadingEdgeProfile(upper, lower, minimumX)));
    }
    if (solidTrailingEdge) {
      validateCutHeight("TE", maximumX, p.trailingEdgeHeight);
      trailingStock.profiles.push_back(resampleOpenProfile(
          trailingEdgeProfile(upper, lower, maximumX, rib.chord)));
    }
    if (slottedSheet)
      trailingStock.slotProfiles.push_back(resampleOpenProfile(
          trailingEdgeProfile(upper, lower, maximumX, retainedMaximumX)));
    for (auto& control : controlParts) {
      if (ribIndex < control.startRibIndex || ribIndex > control.stopRibIndex) continue;
      const double controlLeadingX = std::max(0.001, rib.chord - control.width);
      control.profiles.push_back(resampleOpenProfile(
          trailingEdgeProfile(upper, lower, controlLeadingX, rib.chord)));
      const double hingeCenterX = std::max(0.001,
          controlLeadingX - control.gap - control.hingePostWidth * 0.5);
      control.hingePostCenters.push_back(camberCenter(rib, hingeCenterX));
      // Do not let TE sheeting fan back to the trailing edge on a control's
      // start/stop ribs. That loft wedge closes the intended spanwise corner
      // clearance even though the boundary rib itself must remain intact.
      controlSheetingMaximumX = std::min(controlSheetingMaximumX,
          std::max(0.001, controlLeadingX - control.gap - control.hingePostWidth));
      const bool cutBoundary =
          (control.cutStartRib && ribIndex == control.startRibIndex) ||
          (control.cutStopRib && ribIndex == control.stopRibIndex);
      if ((ribIndex > control.startRibIndex && ribIndex < control.stopRibIndex) ||
          cutBoundary) {
        retainedMaximumX = std::min(retainedMaximumX,
            std::max(0.001, controlLeadingX - control.gap - control.hingePostWidth));
      }
    }
    auto retainedUpper = clippedSurface(upper, minimumX, retainedMaximumX);
    auto retainedLower = clippedSurface(lower, minimumX, retainedMaximumX);
    const double topWoodWidth = p.topSpar ? p.topSparWidth :
        p.bottomSpar ? p.bottomSparWidth : 0.0;
    const double bottomWoodWidth = p.bottomSpar ? p.bottomSparWidth :
        p.topSpar ? p.topSparWidth : 0.0;
    const double sparCenter = 0.25 * rib.chord;
    double topLeEnd = p.carbonSpar != 0 ? sparCenter : sparCenter - topWoodWidth * 0.5;
    double topTeStart = p.carbonSpar != 0 ? sparCenter : sparCenter + topWoodWidth * 0.5;
    double bottomLeEnd = p.carbonSpar != 0 ? sparCenter : sparCenter - bottomWoodWidth * 0.5;
    double bottomTeStart = p.carbonSpar != 0 ? sparCenter : sparCenter + bottomWoodWidth * 0.5;
    const auto applyNewSparSheetingBoundary = [&](const int verticalLocation,
                                                   double& leEnd,
                                                   double& teStart) {
      std::size_t selected = p.spars.size();
      double closestToLegacyLocation = 101.0;
      for (std::size_t index = 0; index < p.spars.size(); ++index) {
        const auto& spar = p.spars[index];
        if (spar.verticalLocation != verticalLocation) continue;
        const double distance = std::abs(spar.chordLocationPercent - 25.0);
        if (distance < closestToLegacyLocation) {
          selected = index;
          closestToLegacyLocation = distance;
        }
      }
      if (selected == p.spars.size()) return;
      const auto& spar = p.spars[selected];
      const double width = spar.material == 0 ? spar.woodWidth :
          spar.type == 2 ? spar.stripWidth : spar.type == 0 ? spar.tubeOd : spar.rodOd;
      const double centerX = sparCenters[selected][ribIndex].x;
      leEnd = centerX - width * 0.5;
      teStart = centerX + width * 0.5;
    };
    applyNewSparSheetingBoundary(0, topLeEnd, topTeStart);
    applyNewSparSheetingBoundary(1, bottomLeEnd, bottomTeStart);
    topLeEnd = frontTopEnds[ribIndex];
    bottomLeEnd = frontBottomEnds[ribIndex];
    const double leadingCarbonDiameter = p.leadingEdgeType == 3
        ? p.leadingEdgeTubeOd : p.leadingEdgeType == 4 ? p.leadingEdgeRodOd : 0.0;
    double topSheetingMinimumX = minimumX;
    double bottomSheetingMinimumX = minimumX;
    if (p.leadingEdgeType == 3 || p.leadingEdgeType == 4) {
      // Extend the sheet recess just inside the carbon-LE notch. The circular
      // rib notch is cut afterward, removing the overlap. This avoids an
      // unreliable coincident edge while leaving no gap at the notch.
      constexpr double carbonLeadingEdgeOvercut = 0.05;
      const auto center = carbonLeadingEdgeCenters[ribIndex];
      const double radius = leadingCarbonDiameter * 0.5;
      const auto topIntersection =
          aftCircleSurfaceIntersection(upper, center, radius);
      const auto bottomIntersection =
          aftCircleSurfaceIntersection(lower, center, radius);
      if (!topIntersection || !bottomIntersection)
        throw std::invalid_argument(
            "Carbon leading edge does not intersect both rib surfaces at rib " +
            std::to_string(ribIndex + 1));
      topSheetingMinimumX = topIntersection->x - carbonLeadingEdgeOvercut;
      bottomSheetingMinimumX =
          bottomIntersection->x - carbonLeadingEdgeOvercut;
    }
    std::vector<SurfaceRecess> upperRecesses;
    std::vector<SurfaceRecess> lowerRecesses;
    const auto addSheet = [&](SheetingPart& part, const bool enabled, const double thickness,
                              const std::vector<Point2>& surface, const double left,
                              const double right, const bool top,
                              std::vector<SurfaceRecess>& recesses) {
      if (!enabled || ribIndex > part.stopRibIndex) return;
      const double clippedLeft = std::clamp(left, minimumX, retainedMaximumX);
      const double clippedRight = std::clamp(right, minimumX, retainedMaximumX);
      if (clippedRight <= clippedLeft + 1.0e-6 || thickness <= 0.0) return;
      part.profiles.push_back(sheetingProfile(surface, clippedLeft, clippedRight, thickness, top));
      recesses.push_back({clippedLeft, clippedRight, thickness});
    };
    if (p.leTopSheet && p.turbulators &&
        ribIndex <= leTopSheets.front().stopRibIndex) {
      double stripStart = topSheetingMinimumX;
      for (int i = 1; i <= turbulatorCount; ++i) {
        const double center = 0.25 * static_cast<double>(i) /
            static_cast<double>(turbulatorCount + 1) * rib.chord;
        const double stripEnd = center - p.turbulatorWidth * 0.5;
        if (stripEnd <= stripStart + 1.0e-6)
          throw std::invalid_argument(
              "Turbulator width leaves no room for an LE top sheeting strip at rib " +
              std::to_string(ribIndex + 1));
        addSheet(leTopSheets[static_cast<std::size_t>(i - 1)], true,
            p.leTopSheetThickness, upper, stripStart, stripEnd, true,
            upperRecesses);
        stripStart = center + p.turbulatorWidth * 0.5;
      }
      if (topLeEnd <= stripStart + 1.0e-6)
        throw std::invalid_argument(
            "Turbulator width leaves no room for the final LE top sheeting strip at rib " +
            std::to_string(ribIndex + 1));
      addSheet(leTopSheets.back(), true, p.leTopSheetThickness, upper,
          stripStart, topLeEnd, true, upperRecesses);
    } else {
      addSheet(leTopSheets.front(), p.leTopSheet, p.leTopSheetThickness, upper,
               topSheetingMinimumX, topLeEnd, true, upperRecesses);
    }
    addSheet(leBottomSheet, p.leBottomSheet, p.leBottomSheetThickness, lower,
             bottomSheetingMinimumX, bottomLeEnd, false, lowerRecesses);
    const auto teSheetingStart = [&](const bool enabled, const double width,
                                     const char* name) {
      if (enabled && width >= rib.chord - minimumX - 1.0e-6)
        throw std::invalid_argument(std::string{name} +
            " Width must be less than the available chord at rib " +
            std::to_string(ribIndex + 1));
      return std::max(minimumX, rib.chord - width);
    };
    const double topTeSheetingStart = teSheetingStart(
        p.topTeSheeting, p.topTeSheetingWidth, "Top TE Sheeting");
    const double bottomTeSheetingStart = teSheetingStart(
        p.bottomTeSheeting, p.bottomTeSheetingWidth, "Bottom TE Sheeting");
    TeSheetingDefinition topTeDefinition{
        p.topTeSheeting, topTeSheetingStart, p.topTeSheetingThickness,
        p.topTeSheetingTaper || bothTeSheeting,
        topTeSheetingStart + p.topTeSheetingTaperStartLocationPercent /
            100.0 * (rib.chord - topTeSheetingStart)};
    TeSheetingDefinition bottomTeDefinition{
        p.bottomTeSheeting, bottomTeSheetingStart, p.bottomTeSheetingThickness,
        p.bottomTeSheetingTaper || bothTeSheeting,
        bottomTeSheetingStart + p.bottomTeSheetingTaperStartLocationPercent /
            100.0 * (rib.chord - bottomTeSheetingStart)};
    const double fixedTopTeDepth = p.teTopSheet &&
        ribIndex <= teTopSheet.stopRibIndex && !p.topTeSheeting
        ? p.teTopSheetThickness : 0.0;
    const double fixedBottomTeDepth = p.teBottomSheet &&
        ribIndex <= teBottomSheet.stopRibIndex && !p.bottomTeSheeting
        ? p.teBottomSheetThickness : 0.0;
    const double teClosureStart = teSheetingClosureStart(
        topTeDefinition, bottomTeDefinition,
        fixedTopTeDepth, fixedBottomTeDepth, rib.chord, upper, lower);
    topTeDefinition.closureStart = teClosureStart;
    bottomTeDefinition.closureStart = teClosureStart;
    // Once TE sheeting consumes the full airfoil depth, the rib ends. Keeping
    // coincident upper and lower outline paths aft of this point exports as a
    // zero-width sliver; rear sheeting continuing into that tail can also
    // distort the first ribs where it is active.
    if ((topTeDefinition.enabled || bottomTeDefinition.enabled) &&
        teClosureStart < retainedMaximumX - 1.0e-8) {
      retainedMaximumX = std::max(minimumX, teClosureStart);
      retainedUpper = clippedSurface(
          retainedUpper, minimumX, retainedMaximumX);
      retainedLower = clippedSurface(
          retainedLower, minimumX, retainedMaximumX);
    }
    std::vector<double> teRecessCoordinates;
    const double firstTeStart = std::min(
        topTeDefinition.enabled ? topTeDefinition.left : rib.chord,
        bottomTeDefinition.enabled ? bottomTeDefinition.left : rib.chord);
    if (firstTeStart < rib.chord) {
      const auto appendSurfaceCoordinates = [&](const std::vector<Point2>& surface) {
        for (const auto point : surface)
          if (point.x >= firstTeStart - 1.0e-8)
            teRecessCoordinates.push_back(point.x);
      };
      appendSurfaceCoordinates(upper);
      appendSurfaceCoordinates(lower);
      const auto appendProfileCoordinates = [&](const TeSheetingDefinition& definition,
                                                const std::vector<Point2>& surface) {
        if (!definition.enabled) return;
        for (const auto point : resampleOpenProfile(
                 clippedSurface(surface, definition.left, rib.chord)))
          teRecessCoordinates.push_back(point.x);
        teRecessCoordinates.push_back(definition.left);
        teRecessCoordinates.push_back(definition.taperStart);
      };
      appendProfileCoordinates(topTeDefinition, upper);
      appendProfileCoordinates(bottomTeDefinition, lower);
      teRecessCoordinates.push_back(teClosureStart);
      teRecessCoordinates.push_back(rib.chord);
      std::sort(teRecessCoordinates.begin(), teRecessCoordinates.end());
      teRecessCoordinates.erase(std::unique(
          teRecessCoordinates.begin(), teRecessCoordinates.end(),
          [](const double first, const double second) {
            return std::abs(first - second) < 1.0e-8;
          }), teRecessCoordinates.end());
    }
    const auto addTeSheet = [&](SheetingPart& part,
                                const TeSheetingDefinition& definition,
                                const bool top,
                                std::vector<SurfaceRecess>& recesses) {
      if (!definition.enabled) return;
      auto profileAndDepths = teSheetingProfile(
          upper, lower, topTeDefinition, bottomTeDefinition,
          fixedTopTeDepth, fixedBottomTeDepth, top, rib.chord);
      part.profiles.push_back(std::move(profileAndDepths.first));
      std::vector<Point2> recessDepths;
      recessDepths.reserve(teRecessCoordinates.size());
      for (const double x : teRecessCoordinates) {
        if (x < definition.left - 1.0e-8) continue;
        recessDepths.push_back({x, resolvedTeSheetingDepth(
            topTeDefinition, bottomTeDefinition,
            fixedTopTeDepth, fixedBottomTeDepth,
            top, x, rib.chord, upper, lower)});
      }
      recesses.push_back({definition.left, rib.chord,
                          definition.thickness, std::move(recessDepths)});
    };
    addTeSheet(topTeSheeting, topTeDefinition, true, upperRecesses);
    addTeSheet(bottomTeSheeting, bottomTeDefinition, false, lowerRecesses);
    const double rearTopRight = p.topTeSheeting
        ? std::min(topTeSheetingStart, retainedMaximumX)
        : std::min(fullSheetingMaximumX, retainedMaximumX);
    const double rearBottomRight = p.bottomTeSheeting
        ? std::min(bottomTeSheetingStart, retainedMaximumX)
        : std::min(fullSheetingMaximumX, retainedMaximumX);
    const double rearTopStart = p.leTopSheet &&
        ribIndex <= leTopSheets.front().stopRibIndex
        ? std::max(topTeStart, topLeEnd) : topTeStart;
    const double rearBottomStart = p.leBottomSheet &&
        ribIndex <= leBottomSheet.stopRibIndex
        ? std::max(bottomTeStart, bottomLeEnd) : bottomTeStart;
    addSheet(teTopSheet, p.teTopSheet, p.teTopSheetThickness, upper,
             rearTopStart, rearTopRight,
             true, upperRecesses);
    addSheet(teBottomSheet, p.teBottomSheet, p.teBottomSheetThickness, lower,
             rearBottomStart, rearBottomRight,
             false, lowerRecesses);
    const auto addTeAlternatives = [&](SheetingPart& part, const bool enabled,
                                       const double thickness,
                                       const std::vector<Point2>& surface,
                                       const double left, const bool top) {
      if (!enabled || ribIndex > part.stopRibIndex || thickness <= 0.0) return;
      const auto append = [&](std::vector<std::vector<Point2>>& destination,
                              const double right) {
        const double clippedLeft = std::clamp(left, minimumX, fullSheetingMaximumX);
        const double clippedRight = std::clamp(right, minimumX, fullSheetingMaximumX);
        if (clippedRight <= clippedLeft + 1.0e-6) return;
        destination.push_back(sheetingProfile(
            surface, clippedLeft, clippedRight, thickness, top));
      };
      const double rearRight = top
          ? (p.topTeSheeting ? topTeSheetingStart : fullSheetingMaximumX)
          : (p.bottomTeSheeting ? bottomTeSheetingStart : fullSheetingMaximumX);
      append(part.fullProfiles, rearRight);
      append(part.controlProfiles, std::min(rearRight, controlSheetingMaximumX));
    };
    addTeAlternatives(teTopSheet, p.teTopSheet, p.teTopSheetThickness,
                      upper, rearTopStart, true);
    addTeAlternatives(teBottomSheet, p.teBottomSheet, p.teBottomSheetThickness,
                      lower, rearBottomStart, false);
    retainedUpper = applySurfaceRecesses(retainedUpper, std::move(upperRecesses), true);
    retainedLower = applySurfaceRecesses(retainedLower, std::move(lowerRecesses), false);
    std::vector<Notch> topNotches;
    std::vector<Notch> bottomNotches;
    if (p.topSpar) topNotches.push_back({0.25 * rib.chord, p.topSparWidth, p.topSparHeight});
    if (p.bottomSpar) bottomNotches.push_back({0.25 * rib.chord, p.bottomSparWidth, p.bottomSparHeight});
    if (p.topRearSpar) topNotches.push_back({0.60 * rib.chord, p.topRearSparWidth, p.topRearSparHeight});
    if (p.bottomRearSpar) bottomNotches.push_back({0.60 * rib.chord, p.bottomRearSparWidth, p.bottomRearSparHeight});
    if (p.turbulators) {
      const int count = std::clamp(p.turbulatorCount, 1, 4);
      for (int i = 1; i <= count; ++i) {
        const double fraction = 0.25 * static_cast<double>(i) / static_cast<double>(count + 1);
        topNotches.push_back({fraction * rib.chord, p.turbulatorWidth, p.turbulatorHeight});
      }
    }
    std::vector<std::vector<Point2>> sparBooleanCutouts;
    std::vector<std::vector<Point2>> sparBooleanHoles;
    for (std::size_t sparIndex = 0; sparIndex < p.spars.size(); ++sparIndex) {
      const auto& spar = p.spars[sparIndex];
      const auto center = sparCenters[sparIndex][ribIndex];
      const bool rectangular = spar.material == 0 || spar.type == 2;
      const double width = spar.material == 0 ? spar.woodWidth :
          spar.type == 2 ? spar.stripWidth : spar.type == 0 ? spar.tubeOd : spar.rodOd;
      const double height = spar.material == 0 ? spar.woodHeight :
          spar.type == 2 ? spar.stripThickness : width;
      if (rectangular && spar.verticalLocation == 0)
        topNotches.push_back({center.x, width, height});
      else if (rectangular && spar.verticalLocation == 1)
        bottomNotches.push_back({center.x, width, height});
      else if (rectangular) {
        const double halfWidth = width * 0.5;
        const double halfHeight = height * 0.5;
        sparBooleanCutouts.push_back(rectangle({{{center.x - halfWidth, center.y - halfHeight},
            {center.x + halfWidth, center.y - halfHeight},
            {center.x + halfWidth, center.y + halfHeight},
            {center.x - halfWidth, center.y + halfHeight}}}));
      } else {
        sparBooleanHoles.push_back(circle(center, width));
      }
    }
    for (const auto& topNotch : topNotches) {
      const double topLeft = topNotch.centerX - topNotch.width * 0.5;
      const double topRight = topNotch.centerX + topNotch.width * 0.5;
      for (const auto& bottomNotch : bottomNotches) {
        const double bottomLeft = bottomNotch.centerX - bottomNotch.width * 0.5;
        const double bottomRight = bottomNotch.centerX + bottomNotch.width * 0.5;
        if (std::min(topRight, bottomRight) <= std::max(topLeft, bottomLeft) + 1.0e-8)
          continue;
        const double topFloor = interpolateY(retainedUpper, topNotch.centerX) -
            topNotch.depth;
        const double bottomFloor = interpolateY(retainedLower, bottomNotch.centerX) +
            bottomNotch.depth;
        if (bottomFloor >= topFloor - 1.0e-8) {
          throw std::invalid_argument("Top and bottom wood-spar notches overlap at rib " +
              std::to_string(ribIndex + 1) +
              "; reduce the spar heights or increase the local airfoil thickness");
        }
      }
    }
    auto notchedUpper = applyNotches(retainedUpper, topNotches, true);
    const bool spoilerInteriorRib = buildSpoiler &&
        ((ribIndex > spoiler.startRibIndex && ribIndex < spoiler.endRibIndex) ||
         (spoiler.spansCenter && ribIndex == 0));
    if (spoilerInteriorRib)
      notchedUpper = applySlopedTopNotch(notchedUpper, spoilerLeft, spoilerRight,
          spoilerTopLeft - p.spoilerThickness,
          spoilerTopRight - p.spoilerThickness);
    auto notchedLower = applyNotches(retainedLower, bottomNotches, false);
    std::vector<Point2> outline;
    outline.reserve(notchedUpper.size() + notchedLower.size() - 1);
    for (auto it = notchedUpper.rbegin(); it != notchedUpper.rend(); ++it) outline.push_back(*it);
    auto lowerBegin = notchedLower.begin();
    if (std::hypot(outline.back().x - lowerBegin->x,
                   outline.back().y - lowerBegin->y) < 1.0e-8)
      ++lowerBegin;
    outline.insert(outline.end(), lowerBegin, notchedLower.end());
    if (std::hypot(outline.front().x - outline.back().x,
                   outline.front().y - outline.back().y) < 1.0e-8)
      outline.pop_back();

    StructuredRib structured{rib, std::move(outline), {}, {}, {}};
    structured.outlineSegments = makeRibOutlineSegments(structured.outerOutline);
    structured.booleanCutouts.insert(structured.booleanCutouts.end(),
        sparBooleanCutouts.begin(), sparBooleanCutouts.end());
    structured.booleanHoles.insert(structured.booleanHoles.end(),
        sparBooleanHoles.begin(), sparBooleanHoles.end());
    if (p.carbonSpar != 0) {
      const double diameter = p.carbonSpar == 1 ? p.cfTubeOd : p.cfRodOd;
      structured.holes.push_back(circle(carbonSparCenters[ribIndex], diameter));
    }
    if (p.leadingEdgeType == 3 || p.leadingEdgeType == 4) {
      const double diameter = p.leadingEdgeType == 3 ? p.leadingEdgeTubeOd : p.leadingEdgeRodOd;
      // Form the exposed carbon-LE cradle from the completed recessed outline
      // so the sheeting cut reaches the circular notch without a separate,
      // tangent Boolean operation.
      auto finishedOutline = exposedLeadingEdgeOutline(
          structured.outerOutline, carbonLeadingEdgeCenters[ribIndex],
          diameter * 0.5);
      structured.outerOutline = finishedOutline.points;
      structured.outlineSegments = finishedOutline.segments;
      structured.partOutline = std::move(finishedOutline.points);
      structured.partOutlineSegments = std::move(finishedOutline.segments);
    }
    if (p.wiringHoles &&
        ribIndex + 1 >= static_cast<std::size_t>(p.wiringHoleStartRib) &&
        ribIndex + 1 <= static_cast<std::size_t>(p.wiringHoleEndRib)) {
      const double left = p.wiringHoleChordLocationPercent / 100.0 * rib.chord;
      const double right = left + p.wiringHoleWidth;
      const double centerX = (left + right) * 0.5;
      const double centerY = camberCenter(rib, centerX).y;
      const double bottom = centerY - p.wiringHoleHeight * 0.5;
      const double top = centerY + p.wiringHoleHeight * 0.5;
      if (left <= 0.0 || right >= rib.chord ||
          bottom <= std::max(interpolateY(lower, left), interpolateY(lower, right)) ||
          top >= std::min(interpolateY(upper, left), interpolateY(upper, right))) {
        throw std::invalid_argument("Wiring Hole does not fit inside rib " +
            std::to_string(ribIndex + 1));
      }
      auto opening = rectangle({{{left, bottom}, {right, bottom},
                                  {right, top}, {left, top}}});
      structured.internalCutouts.push_back(opening);
      const std::string ribName = p.rib1aPresent && ribIndex == 1
          ? "R1a"
          : "R" + std::to_string(ribIndex + 1 -
                (p.rib1aPresent && ribIndex > 1 ? 1 : 0));
      wing.wiringHoles.push_back(
          {"Wiring Hole " + ribName, ribIndex, std::move(opening)});
    }
    for (const auto& joiner : wing.joiners) {
      if (ribIndex > joiner.stopRibIndex) continue;
      if (joiner.kind == SpanMemberKind::Rectangular) {
        // The wood joiner stops at Rib 2 and therefore does not cut it.
        if (ribIndex < joiner.stopRibIndex) {
          auto cut = joiner.rectangularProfiles[ribIndex];
          constexpr double machiningClearance = 0.01;
          cut[0].y += machiningClearance; cut[1].y += machiningClearance;
          cut[2].y -= machiningClearance; cut[3].y -= machiningClearance;
          auto splitCutout = rectangle(cut);
          structured.ribSplitCutouts.push_back(splitCutout);
          structured.booleanCutouts.push_back(std::move(splitCutout));
        }
      } else {
        const double angleDifference = (joiner.axisAngleDegrees -
            structured.rib.ribPlaneAngleDegrees) * std::numbers::pi / 180.0;
        const double normalProjection = std::abs(std::cos(angleDifference));
        structured.booleanHoles.push_back(circle(joiner.centers[ribIndex],
            joiner.outerDiameter / std::max(0.25, normalProjection)));
      }
    }
    wing.ribs.push_back(std::move(structured));
  }

  if (buildSpoiler) {
    const auto& start = ribs[spoiler.startRibIndex];
    const auto& end = ribs[spoiler.endRibIndex];
    double span = std::hypot(end.spanPosition - start.spanPosition,
                             end.dihedralHeight - start.dihedralHeight);
    span -= p.ribThickness + 2.0 * spoiler.gap;
    if (spoiler.spansCenter)
      span = 2.0 * std::hypot(end.spanPosition, end.dihedralHeight) -
          p.ribThickness - 2.0 * spoiler.gap;
    span = std::max(0.001, span);
    spoiler.dxfOutline = {{0.0, 0.0}, {span, 0.0},
                          {span, spoiler.width}, {0.0, spoiler.width}};
    if (p.spoilerLighteningHoles)
      spoiler.lighteningHoleOutlines = spoilerLighteningHoleLayout(
          span, spoiler.width, p.spoilerMinimumWoodMargin,
          p.spoilerMinimumCircleDistance, spoiler.spansCenter);
    wing.spoilers.push_back(std::move(spoiler));
  }

  if (p.leTopSheet) {
    for (auto& sheet : leTopSheets)
      if (!sheet.profiles.empty()) wing.sheeting.push_back(std::move(sheet));
  }
  if (p.leBottomSheet && !leBottomSheet.profiles.empty()) wing.sheeting.push_back(std::move(leBottomSheet));
  const auto markControlBays = [&controlParts](SheetingPart& part) {
    std::vector<bool> bays(part.stopRibIndex, false);
    bool anyControlBay = false;
    for (const auto& control : controlParts) {
      for (std::size_t bay = control.startRibIndex;
           bay < control.stopRibIndex && bay < bays.size(); ++bay) {
        bays[bay] = true;
        anyControlBay = true;
      }
    }
    if (anyControlBay) part.controlBays = std::move(bays);
  };
  if (p.topTeSheeting && !topTeSheeting.profiles.empty())
    wing.sheeting.push_back(std::move(topTeSheeting));
  if (p.bottomTeSheeting && !bottomTeSheeting.profiles.empty())
    wing.sheeting.push_back(std::move(bottomTeSheeting));
  if (p.teTopSheet && !teTopSheet.profiles.empty()) {
    markControlBays(teTopSheet);
    wing.sheeting.push_back(std::move(teTopSheet));
  }
  if (p.teBottomSheet && !teBottomSheet.profiles.empty()) {
    markControlBays(teBottomSheet);
    wing.sheeting.push_back(std::move(teBottomSheet));
  }

  if (!leadingStock.profiles.empty()) wing.profiledMembers.push_back(std::move(leadingStock));
  struct ControlExclusion {
    std::size_t first{};
    std::size_t last{};
    bool cutsLast{};
  };
  std::vector<ControlExclusion> excludedRanges;
  for (const auto& control : controlParts)
    excludedRanges.push_back({control.startRibIndex, control.stopRibIndex,
                              control.cutStopRib});
  std::sort(excludedRanges.begin(), excludedRanges.end(),
      [](const ControlExclusion& a, const ControlExclusion& b) {
        return std::tie(a.first, a.last) < std::tie(b.first, b.last);
      });
  std::vector<ControlExclusion> mergedExclusions;
  for (const auto& range : excludedRanges) {
    if (mergedExclusions.empty() || range.first > mergedExclusions.back().last)
      mergedExclusions.push_back(range);
    else if (range.last >= mergedExclusions.back().last) {
      if (range.last > mergedExclusions.back().last)
        mergedExclusions.back().cutsLast = range.cutsLast;
      else
        mergedExclusions.back().cutsLast =
            mergedExclusions.back().cutsLast || range.cutsLast;
      mergedExclusions.back().last = range.last;
    }
  }
  std::vector<std::pair<std::size_t, std::size_t>> trailingRanges;
  std::size_t rangeStart = 0;
  for (const auto& excluded : mergedExclusions) {
    if (rangeStart <= excluded.first) trailingRanges.emplace_back(rangeStart, excluded.first);
    rangeStart = std::max(rangeStart,
        excluded.last + static_cast<std::size_t>(excluded.cutsLast));
  }
  if (rangeStart <= ribs.size() - 1) trailingRanges.emplace_back(rangeStart, ribs.size() - 1);
  trailingStock.activeRanges = trailingRanges;
  if (p.trailingEdgeType == 2) {
    for (std::size_t segment = 0; segment < trailingRanges.size(); ++segment) {
      const auto [firstRib, lastRib] = trailingRanges[segment];
      SheetStockPart stock{"Sheet trailing edge segment " + std::to_string(segment + 1), {}, {}};
      const auto& root = ribs[firstRib];
      const auto& tip = ribs[lastRib];
      const double halfRibThickness = p.ribThickness * 0.5;
      const double rootLeading = root.leadingEdgeOffset + root.chord - p.trailingEdgeWidth;
      const double tipLeading = tip.leadingEdgeOffset + tip.chord - p.trailingEdgeWidth;
      stock.outline = {{rootLeading, root.spanPosition - halfRibThickness},
                       {tipLeading, tip.spanPosition + halfRibThickness},
                       {tip.leadingEdgeOffset + tip.chord, tip.spanPosition + halfRibThickness},
                       {root.leadingEdgeOffset + root.chord, root.spanPosition - halfRibThickness}};
      if (p.trailingEdgeSlotted) {
        for (std::size_t i = firstRib; i <= lastRib; ++i) {
          const auto& rib = ribs[i];
        const double leading = rib.leadingEdgeOffset + rib.chord - p.trailingEdgeWidth;
        const double halfThickness = 0.5 * p.ribThickness;
          const double slotBottom = std::max(root.spanPosition - halfRibThickness,
                                           rib.spanPosition - halfThickness);
          const double slotTop = std::min(tip.spanPosition + halfRibThickness,
                                        rib.spanPosition + halfThickness);
          stock.slots.push_back({{leading, slotBottom},
                                 {leading + trailingEdgeSlotDepth, slotBottom},
                                 {leading + trailingEdgeSlotDepth, slotTop},
                                 {leading, slotTop}});
        }
        // Every rib slot opens through the stock's leading edge, so every one
        // belongs in the outside contour. Closed slot rectangles would add a
        // line across each notch mouth and make the laser cut invalid.
        if (firstRib == lastRib && !stock.slots.empty()) {
          const auto slot = stock.slots.front();
          stock.outline = {{slot[1].x, slot[1].y},
                           {root.leadingEdgeOffset + root.chord, slot[1].y},
                           {root.leadingEdgeOffset + root.chord, slot[2].y},
                           {slot[2].x, slot[2].y}};
          stock.slots.clear();
        } else if (stock.slots.size() >= 2) {
          const auto rootSlot = stock.slots.front();
          const auto tipSlot = stock.slots.back();
          std::vector<Point2> notchedOutline;
          notchedOutline.reserve(stock.slots.size() * 4);
          notchedOutline.push_back(rootSlot[3]);
          for (std::size_t slotIndex = 1; slotIndex + 1 < stock.slots.size(); ++slotIndex) {
            const auto& slot = stock.slots[slotIndex];
            notchedOutline.insert(notchedOutline.end(), slot.begin(), slot.end());
          }
          notchedOutline.push_back(tipSlot[0]);
          notchedOutline.push_back(tipSlot[1]);
          notchedOutline.push_back(tipSlot[2]);
          notchedOutline.push_back(
              {tip.leadingEdgeOffset + tip.chord, tipSlot[2].y});
          notchedOutline.push_back(
              {root.leadingEdgeOffset + root.chord, rootSlot[1].y});
          notchedOutline.push_back(rootSlot[1]);
          notchedOutline.push_back(rootSlot[2]);
          stock.outline = std::move(notchedOutline);
          stock.slots.clear();
        }
      }
      wing.sheetStockParts.push_back(std::move(stock));
    }
  }
  if (!trailingStock.profiles.empty()) wing.profiledMembers.push_back(std::move(trailingStock));
  wing.controlSurfaces = std::move(controlParts);

  for (std::size_t sparIndex = 0; sparIndex < p.spars.size(); ++sparIndex) {
    const auto& spar = p.spars[sparIndex];
    SpanMember member;
    member.name = "Spar " + std::to_string(sparIndex + 1);
    const bool rectangular = spar.material == 0 || spar.type == 2;
    member.kind = rectangular ? SpanMemberKind::Rectangular :
        spar.type == 0 ? SpanMemberKind::Tube : SpanMemberKind::Rod;
    member.width = spar.material == 0 ? spar.woodWidth :
        spar.type == 2 ? spar.stripWidth : spar.type == 0 ? spar.tubeOd : spar.rodOd;
    member.height = spar.material == 0 ? spar.woodHeight :
        spar.type == 2 ? spar.stripThickness : member.width;
    member.innerDiameter = spar.material != 0 && spar.type == 0 ? spar.tubeId : 0.0;
    member.centers = sparCenters[sparIndex];
    member.carbonFiber = spar.material != 0;
    member.verticalLocation = spar.verticalLocation;
    member.cutsSheeting = spar.verticalLocation == 0 || spar.verticalLocation == 1;
    wing.members.push_back(std::move(member));
  }

  if (p.topSpar) addRectMember(wing, "Top spar", 0.25, true, p.topSparWidth,
      p.topSparHeight, SpanMemberKind::Rectangular, &legacyTopSparInsets);
  if (p.bottomSpar) addRectMember(wing, "Bottom spar", 0.25, false, p.bottomSparWidth,
      p.bottomSparHeight, SpanMemberKind::Rectangular, &legacyBottomSparInsets);
  if (p.topRearSpar) addRectMember(wing, "Top 60% rear spar", 0.60, true,
      p.topRearSparWidth, p.topRearSparHeight, SpanMemberKind::Rectangular,
      &legacyTopRearSparInsets);
  if (p.bottomRearSpar) addRectMember(wing, "Bottom 60% rear spar", 0.60, false,
      p.bottomRearSparWidth, p.bottomRearSparHeight, SpanMemberKind::Rectangular,
      &legacyBottomRearSparInsets);
  if (p.turbulators) {
    const int count = std::clamp(p.turbulatorCount, 1, 4);
    for (int i = 1; i <= count; ++i) {
      const double fraction = 0.25 * static_cast<double>(i) / static_cast<double>(count + 1);
      addRectMember(wing, "Turbulator " + std::to_string(i), fraction, true,
                    p.turbulatorWidth, p.turbulatorHeight, SpanMemberKind::Turbulator);
    }
  }
  if (p.carbonSpar != 0) {
    SpanMember member;
    member.name = p.carbonSpar == 1 ? "CF tube" : "CF rod";
    member.kind = p.carbonSpar == 1 ? SpanMemberKind::Tube : SpanMemberKind::Rod;
    member.width = member.height = p.carbonSpar == 1 ? p.cfTubeOd : p.cfRodOd;
    member.innerDiameter = p.carbonSpar == 1 ? p.cfTubeId : 0.0;
    member.centers = carbonSparCenters;
    wing.members.push_back(std::move(member));
  }
  if (p.leadingEdgeType == 3 || p.leadingEdgeType == 4) {
    SpanMember member;
    member.name = p.leadingEdgeType == 3 ? "CF tube leading edge" : "CF rod leading edge";
    member.kind = p.leadingEdgeType == 3 ? SpanMemberKind::Tube : SpanMemberKind::Rod;
    member.width = member.height = p.leadingEdgeType == 3
        ? p.leadingEdgeTubeOd : p.leadingEdgeRodOd;
    member.innerDiameter = p.leadingEdgeType == 3 ? p.leadingEdgeTubeId : 0.0;
    member.centers = carbonLeadingEdgeCenters;
    wing.members.push_back(std::move(member));
  }

  const auto addShearWebSet = [&](const std::vector<Point2>& topCenters,
                                  const std::vector<Point2>& bottomCenters,
                                  const double topHeight,
                                  const double bottomHeight,
                                  const double thickness,
                                  const std::string& namePrefix) {
    for (std::size_t i = 0; i + 1 < wing.ribs.size(); ++i) {
      const bool occupiedByWoodJoiner = std::any_of(
          wing.joiners.begin(), wing.joiners.end(), [i](const JoinerPart& joiner) {
            return joiner.kind == SpanMemberKind::Rectangular &&
                   i < joiner.stopRibIndex;
          });
      if (occupiedByWoodJoiner) continue;
      auto top0 = topCenters[i];
      auto bottom0 = bottomCenters[i];
      auto top1 = topCenters[i + 1];
      auto bottom1 = bottomCenters[i + 1];
      top0.y -= topHeight * 0.5;
      top1.y -= topHeight * 0.5;
      bottom0.y += bottomHeight * 0.5;
      bottom1.y += bottomHeight * 0.5;
      // Webs extend from the centerline of the bottom spar to the centerline
      // of the top spar at both rib stations.
      const auto& rootRib = wing.ribs[i].rib;
      const auto& tipRib = wing.ribs[i + 1].rib;
      const double panelDy = ribs.back().spanPosition - ribs.front().spanPosition;
      const double panelDz = ribs.back().dihedralHeight - ribs.front().dihedralHeight;
      const double panelLength = std::hypot(panelDy, panelDz);
      const auto projectedFaceOffset = [&](const RibDefinition& rib,
                                           const double materialOffset) {
        if (panelLength < 1.0e-9) return 0.0;
        const double plane = rib.ribPlaneAngleDegrees *
            std::numbers::pi / 180.0;
        return materialOffset *
            (std::cos(plane) * panelDy + std::sin(plane) * panelDz) /
            panelLength;
      };
      const double centerBay = std::hypot(
          tipRib.spanPosition - rootRib.spanPosition,
          tipRib.dihedralHeight - rootRib.dihedralHeight);
      const double rootEnd = projectedFaceOffset(rootRib,
          (rootRib.ribThicknessStartFactor + 1.0) * p.ribThickness);
      const double tipStart = projectedFaceOffset(tipRib,
          tipRib.ribThicknessStartFactor * p.ribThickness);
      const double clearBay = std::max(0.0, centerBay + tipStart - rootEnd);
      ShearWebPart web;
      web.name = namePrefix + std::to_string(i + 1);
      web.bayIndex = i + 1;
      web.thickness = thickness;
      web.outline = {{0.0, 0.0}, {clearBay, bottom1.y - bottom0.y},
                     {clearBay, top1.y - bottom0.y}, {0.0, top0.y - bottom0.y}};
      web.stationCorners = {bottom0, bottom1, top1, top0};
      wing.shearWebs.push_back(std::move(web));
    }
  };
  if (p.shearWebs && p.topSpar && p.bottomSpar) {
    std::vector<Point2> topCenters;
    std::vector<Point2> bottomCenters;
    for (const auto& rib : ribs) {
      topCenters.push_back(surfaceCenter(rib, 0.25, true, p.topSparHeight));
      bottomCenters.push_back(surfaceCenter(rib, 0.25, false, p.bottomSparHeight));
    }
    addShearWebSet(topCenters, bottomCenters, p.topSparHeight,
        p.bottomSparHeight, p.shearWebThickness, "SW");
  }
  if (p.sparShearWebs) {
    for (std::size_t top = 0; top < p.spars.size(); ++top) {
      if (p.spars[top].material != 0 || p.spars[top].verticalLocation != 0) continue;
      for (std::size_t bottom = 0; bottom < p.spars.size(); ++bottom) {
        const double topTip = p.spars[top].tipChordLocationPercent >= 0.0
            ? p.spars[top].tipChordLocationPercent
            : p.spars[top].chordLocationPercent;
        const double bottomTip = p.spars[bottom].tipChordLocationPercent >= 0.0
            ? p.spars[bottom].tipChordLocationPercent
            : p.spars[bottom].chordLocationPercent;
        if (p.spars[bottom].material != 0 || p.spars[bottom].verticalLocation != 1 ||
            std::abs(p.spars[top].chordLocationPercent -
                     p.spars[bottom].chordLocationPercent) > 1.0e-8 ||
            std::abs(topTip - bottomTip) > 1.0e-8)
          continue;
        addShearWebSet(sparCenters[top], sparCenters[bottom],
            p.spars[top].woodHeight, p.spars[bottom].woodHeight,
            p.sparShearWebThickness,
            "Spar " + std::to_string(top + 1) + "/Spar " +
                std::to_string(bottom + 1) + " shear web ");
      }
    }
  }
  return wing;
}

void addRiblets(StructuredWing& wing,
                const StructureParameters& parameters) {
  if (!parameters.riblets) return;
  if (parameters.leadingEdgeType != 3 &&
      parameters.leadingEdgeType != 4)
    throw std::invalid_argument("Riblets require a CF leading edge");
  if (parameters.ribletsPerBay < 1 || parameters.ribletsPerBay > 5)
    throw std::invalid_argument("Riblets per Bay must be from 1 through 5");
  if (parameters.ribletStartRib < 1 ||
      parameters.ribletEndRib > static_cast<int>(wing.ribs.size()) ||
      parameters.ribletStartRib >= parameters.ribletEndRib)
    throw std::invalid_argument(
        "Riblet Start Rib must be before End Rib and within the panel");

  std::optional<std::size_t> selectedSpar;
  double selectedDistance = std::numeric_limits<double>::max();
  for (std::size_t index = 0; index < parameters.spars.size(); ++index) {
    const auto& spar = parameters.spars[index];
    const double tipChord = spar.tipChordLocationPercent >= 0.0
        ? spar.tipChordLocationPercent : spar.chordLocationPercent;
    if (spar.material != 1 || spar.verticalLocation != 2 ||
        spar.chordLocationPercent < 20 ||
        spar.chordLocationPercent > 40 || tipChord < 20 || tipChord > 40)
      continue;
    const double distance = std::abs(
        0.5 * (spar.chordLocationPercent + tipChord) - 30.0);
    if (distance < selectedDistance) {
      selectedSpar = index;
      selectedDistance = distance;
    }
  }
  const bool legacySpar = !selectedSpar && parameters.spars.empty() &&
      parameters.carbonSpar != 0;
  if (!selectedSpar && !legacySpar)
    throw std::invalid_argument(
        "Riblets require a CF mid-location spar from 20% through 40% chord");

  const double rootSparFraction = legacySpar ? 0.25 :
      parameters.spars[*selectedSpar].chordLocationPercent / 100.0;
  const double tipSparFraction = legacySpar ? rootSparFraction :
      (parameters.spars[*selectedSpar].tipChordLocationPercent >= 0.0
           ? parameters.spars[*selectedSpar].tipChordLocationPercent
           : parameters.spars[*selectedSpar].chordLocationPercent) / 100.0;
  const bool sparIsRectangular = !legacySpar &&
      parameters.spars[*selectedSpar].type == 2;
  const double sparWidth = legacySpar
      ? (parameters.carbonSpar == 1
          ? parameters.cfTubeOd : parameters.cfRodOd)
      : (sparIsRectangular
          ? parameters.spars[*selectedSpar].stripWidth
          : parameters.spars[*selectedSpar].type == 0
              ? parameters.spars[*selectedSpar].tubeOd
              : parameters.spars[*selectedSpar].rodOd);
  const double sparHeight = sparIsRectangular
      ? parameters.spars[*selectedSpar].stripThickness : sparWidth;
  const double leadingDiameter = parameters.leadingEdgeType == 3
      ? parameters.leadingEdgeTubeOd : parameters.leadingEdgeRodOd;

  std::vector<RibDefinition> fullRibs;
  fullRibs.reserve(wing.ribs.size());
  for (const auto& rib : wing.ribs) fullRibs.push_back(rib.rib);
  std::vector<Point2> nominalSparCenters;
  nominalSparCenters.reserve(fullRibs.size());
  const double fullSpan = fullRibs.back().spanPosition -
      fullRibs.front().spanPosition;
  for (const auto& rib : fullRibs) {
    const double along = fullSpan > 1.0e-12
        ? (rib.spanPosition - fullRibs.front().spanPosition) / fullSpan : 0.0;
    const double sparFraction = rootSparFraction +
        (tipSparFraction - rootSparFraction) * along;
    nominalSparCenters.push_back(
        camberCenter(rib, sparFraction * rib.chord));
  }
  const auto sparCenters =
      straightMemberCenters(fullRibs, nominalSparCenters);
  const auto leadingCenters =
      straightExposedLeadingEdgeCenters(fullRibs, leadingDiameter);

  const std::size_t firstBay =
      static_cast<std::size_t>(parameters.ribletStartRib - 1);
  const std::size_t lastBoundary =
      static_cast<std::size_t>(parameters.ribletEndRib - 1);
  wing.riblets.reserve(
      wing.riblets.size() +
      (lastBoundary - firstBay) *
          static_cast<std::size_t>(parameters.ribletsPerBay));
  const auto mix = [](const double first, const double second,
                      const double t) {
    return first + (second - first) * t;
  };
  for (std::size_t bay = firstBay; bay < lastBoundary; ++bay) {
    const auto& inner = wing.ribs[bay];
    const auto& outer = wing.ribs[bay + 1];
    for (int ordinal = 1; ordinal <= parameters.ribletsPerBay; ++ordinal) {
      const double t = static_cast<double>(ordinal) /
          static_cast<double>(parameters.ribletsPerBay + 1);
      RibDefinition rib{
          mix(inner.rib.spanPosition, outer.rib.spanPosition, t),
          mix(inner.rib.chord, outer.rib.chord, t),
          mix(inner.rib.leadingEdgeOffset, outer.rib.leadingEdgeOffset, t),
          mix(inner.rib.dihedralHeight, outer.rib.dihedralHeight, t),
          mix(inner.rib.twistDegrees, outer.rib.twistDegrees, t),
          mix(inner.rib.ribPlaneAngleDegrees,
              outer.rib.ribPlaneAngleDegrees, t),
          -0.5,
          AirfoilProfile::interpolate(
              inner.rib.profile, outer.rib.profile, t)};
      const auto [upper, lower] = localSurfaces(rib);
      const Point2 sparModel{
          mix(modelPlanePoint(inner.rib, sparCenters[bay]).x,
              modelPlanePoint(outer.rib, sparCenters[bay + 1]).x, t),
          mix(modelPlanePoint(inner.rib, sparCenters[bay]).y,
              modelPlanePoint(outer.rib, sparCenters[bay + 1]).y, t)};
      const Point2 sparCenter = localPlanePoint(rib, sparModel);
      const double cutoffX = std::clamp(
          sparCenter.x, 0.001, rib.chord - 0.001);
      const auto retainedUpper = clippedSurface(upper, 0.0, cutoffX);
      const auto retainedLower = clippedSurface(lower, 0.0, cutoffX);
      const Point2 top = retainedUpper.back();
      const Point2 bottom = retainedLower.back();
      const Point2 arcCenter{
          cutoffX, 0.5 * (top.y + bottom.y)};
      const double arcRadius = 0.5 * (top.y - bottom.y);
      if (arcRadius <= sparWidth * 0.5 + 0.05)
        throw std::invalid_argument(
            "The selected CF spar leaves insufficient riblet depth in " +
            inner.name);

      std::vector<Point2> outline;
      outline.reserve(retainedUpper.size() + retainedLower.size() + 25);
      for (auto point = retainedUpper.rbegin();
           point != retainedUpper.rend(); ++point)
        outline.push_back(*point);
      auto lowerPoint = retainedLower.begin();
      if (std::hypot(outline.back().x - lowerPoint->x,
                     outline.back().y - lowerPoint->y) < 1.0e-8)
        ++lowerPoint;
      outline.insert(outline.end(), lowerPoint, retainedLower.end());
      constexpr int arcSamples = 24;
      for (int sample = 1; sample < arcSamples; ++sample) {
        const double angle = -0.5 * std::numbers::pi +
            std::numbers::pi * static_cast<double>(sample) / arcSamples;
        outline.push_back(
            {arcCenter.x + arcRadius * std::cos(angle),
             arcCenter.y + arcRadius * std::sin(angle)});
      }

      StructuredRib riblet{std::move(rib), std::move(outline), {}, {}, {}};
      riblet.outlineSegments =
          makeRibOutlineSegments(riblet.outerOutline);
      if (sparIsRectangular) {
        const double halfWidth = sparWidth * 0.5;
        const double halfHeight = sparHeight * 0.5;
        riblet.booleanCutouts.push_back(rectangle({{{
            sparCenter.x - halfWidth, sparCenter.y - halfHeight},
            {sparCenter.x + halfWidth, sparCenter.y - halfHeight},
            {sparCenter.x + halfWidth, sparCenter.y + halfHeight},
            {sparCenter.x - halfWidth, sparCenter.y + halfHeight}}}));
      } else {
        riblet.booleanHoles.push_back(circle(sparCenter, sparWidth));
      }
      const Point2 leadingModel{
          mix(modelPlanePoint(inner.rib, leadingCenters[bay]).x,
              modelPlanePoint(outer.rib, leadingCenters[bay + 1]).x, t),
          mix(modelPlanePoint(inner.rib, leadingCenters[bay]).y,
              modelPlanePoint(outer.rib, leadingCenters[bay + 1]).y, t)};
      const auto leadingCenter = localPlanePoint(riblet.rib, leadingModel);
      auto finishedOutline = exposedLeadingEdgeOutline(
          riblet.outerOutline, leadingCenter, leadingDiameter * 0.5);
      riblet.outerOutline = finishedOutline.points;
      riblet.outlineSegments = finishedOutline.segments;
      riblet.partOutline = finishedOutline.points;
      riblet.partOutlineSegments = finishedOutline.segments;
      riblet.name = inner.name +
          static_cast<char>('a' + ordinal - 1);
      wing.riblets.push_back(std::move(riblet));
    }
  }
}

std::size_t ribLighteningHoleWorkerCount(
    const std::size_t ribCount, const std::size_t maximumWorkers) {
  if (ribCount == 0) return 0;
  const unsigned logicalProcessors = std::thread::hardware_concurrency();
  std::size_t available = logicalProcessors > 2
      ? static_cast<std::size_t>(logicalProcessors - 2) : 1;
  if (maximumWorkers > 0)
    available = std::min(available, maximumWorkers);
  return std::min(ribCount, available);
}

void addRibLighteningHoles(
    StructuredWing& wing, const StructureParameters& parameters,
    const RibLighteningProgressCallback& progress,
    const std::size_t maximumWorkers) {
  if (!parameters.ribLighteningHoles) return;
  if (parameters.ribLighteningMinimumWoodMargin <= 0.0 ||
      parameters.ribLighteningMinimumHoleDistance < 0.0)
    throw std::invalid_argument(
        "Rib lightening-hole distances must be valid positive dimensions");
  if (parameters.ribLighteningStartRib < 1 ||
      parameters.ribLighteningStopRib >
          static_cast<int>(wing.ribs.size()) ||
      parameters.ribLighteningStartRib >
          parameters.ribLighteningStopRib)
    throw std::invalid_argument(
        "Rib lightening-hole range must be within the panel");

  const std::size_t first =
      static_cast<std::size_t>(parameters.ribLighteningStartRib - 1);
  const std::size_t last =
      static_cast<std::size_t>(parameters.ribLighteningStopRib);
  const std::size_t fullRibCount = last - first;
  const std::size_t count = fullRibCount + wing.riblets.size();
  const std::size_t workers =
      ribLighteningHoleWorkerCount(count, maximumWorkers);
  const std::size_t chunkSize = (count + workers - 1) / workers;
  std::vector<std::future<void>> tasks;
  std::atomic_size_t completedRibs{0};
  tasks.reserve(workers);
  for (std::size_t worker = 0; worker < workers; ++worker) {
    const std::size_t chunkFirst = worker * chunkSize;
    const std::size_t chunkLast = std::min(count, chunkFirst + chunkSize);
    if (chunkFirst >= chunkLast) break;
    tasks.push_back(std::async(std::launch::async,
        [&, chunkFirst, chunkLast] {
          for (std::size_t jobIndex = chunkFirst;
               jobIndex < chunkLast; ++jobIndex) {
            auto& rib = jobIndex < fullRibCount
                ? wing.ribs[first + jobIndex]
                : wing.riblets[jobIndex - fullRibCount];
            auto holes = ribLighteningHoleLayout(
                rib, parameters.ribLighteningMinimumWoodMargin,
                parameters.ribLighteningMinimumHoleDistance);
            rib.internalCutouts.insert(
                rib.internalCutouts.end(),
                std::make_move_iterator(holes.begin()),
                std::make_move_iterator(holes.end()));
            const std::size_t completed = ++completedRibs;
            if (progress) progress(completed, count);
          }
        }));
  }
  for (auto& task : tasks) task.get();
}

} // namespace designrc::domain
