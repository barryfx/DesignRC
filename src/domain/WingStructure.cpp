#include "domain/WingStructure.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace designrc::domain {
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
  }
  std::sort(coordinates.begin(), coordinates.end());
  coordinates.erase(std::unique(coordinates.begin(), coordinates.end(),
      [](double a, double b) { return std::abs(a - b) < 1.0e-8; }), coordinates.end());
  const auto depthAt = [&recesses](const double x) {
    double depth = 0.0;
    for (const auto& recess : recesses)
      if (x > recess.left + 1.0e-8 && x < recess.right - 1.0e-8)
        depth = std::max(depth, recess.depth);
    return depth;
  };
  std::vector<Point2> result;
  for (std::size_t i = 0; i < coordinates.size(); ++i) {
    const double x = coordinates[i];
    const double leftDepth = i == 0 ? depthAt(x + 1.0e-6) :
        depthAt(0.5 * (coordinates[i - 1] + x));
    const double rightDepth = i + 1 == coordinates.size() ? leftDepth :
        depthAt(0.5 * (x + coordinates[i + 1]));
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

bool circularHoleFits(const RibDefinition& rib, const Point2 center, const double diameter) {
  const auto [upper, lower] = localSurfaces(rib);
  const double radius = diameter * 0.5;
  constexpr int samples = 64;
  for (int i = 0; i < samples; ++i) {
    const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) / samples;
    const double x = center.x + radius * std::cos(angle);
    const double y = center.y + radius * std::sin(angle);
    if (x <= 0.0 || x >= rib.chord ||
        y <= interpolateY(lower, x) + 1.0e-5 ||
        y >= interpolateY(upper, x) - 1.0e-5)
      return false;
  }
  return true;
}

Point2 fittedLeadingEdgeCenter(const RibDefinition& rib, const double diameter,
                               const double additionalSetback = 0.0) {
  const double radius = diameter * 0.5;
  const double limit = std::min(rib.chord - radius, rib.chord * 0.35);
  for (double x = radius + 0.05 + additionalSetback; x <= limit; x += 0.1) {
    const auto center = camberCenter(rib, x);
    if (circularHoleFits(rib, center, diameter)) return center;
  }
  throw std::invalid_argument("The selected CF leading-edge diameter does not fit inside the airfoil");
}

std::vector<Point2> straightFittedLeadingEdgeCenters(
    const std::vector<RibDefinition>& ribs, const double diameter) {
  for (double setback = 0.0; setback <= ribs.back().chord * 0.2; setback += 0.25) {
    std::vector<Point2> nominal;
    nominal.reserve(ribs.size());
    for (const auto& rib : ribs)
      nominal.push_back(fittedLeadingEdgeCenter(rib, diameter, setback));
    auto centers = straightMemberCenters(ribs, nominal);
    bool allFit = true;
    for (std::size_t i = 0; i < ribs.size(); ++i)
      if (!circularHoleFits(ribs[i], centers[i], diameter)) { allFit = false; break; }
    if (allFit) return centers;
  }
  throw std::invalid_argument("A straight CF leading edge cannot fit through every rib");
}

void addRectMember(StructuredWing& wing, const std::string& name, const double fraction,
                   const bool top, const double width, const double height,
                   const SpanMemberKind kind = SpanMemberKind::Rectangular) {
  SpanMember member{name, kind, width, height, 0.0, {}};
  member.centers.reserve(wing.ribs.size());
  for (const auto& structured : wing.ribs)
    member.centers.push_back(surfaceCenter(structured.rib, fraction, top, height));
  wing.members.push_back(std::move(member));
}

} // namespace

StructuredWing applyWingStructure(const std::vector<RibDefinition>& ribs,
                                  const StructureParameters& p) {
  if (ribs.size() < 2) throw std::invalid_argument("Wing structure requires at least two ribs");
  StructuredWing wing;
  wing.ribs.reserve(ribs.size());
  ProfiledSpanMember leadingStock{
      p.leadingEdgeType == 1 ? "Shaped leading edge" : "Block leading edge", {}};
  ProfiledSpanMember trailingStock{
      p.trailingEdgeType == 1 ? "Shaped trailing edge" : "Sheet trailing edge", {}};
  const double trailingEdgeSlotDepth = std::max(0.0,
      std::min(p.trailingEdgeSlotDepth, p.trailingEdgeWidth));
  std::vector<ControlSurfacePart> controlParts;
  const auto addControl = [&](const bool enabled, const std::string& name,
                              const double width, const double hingeWidth,
                              const double hingeHeight, const int startRib,
                              const int stopRib) {
    if (!enabled) return;
    if (ribs.size() < 4) return;
    const int lastInternalRib = static_cast<int>(ribs.size()) - 1;
    const auto start = static_cast<std::size_t>(
        std::clamp(startRib, 2, lastInternalRib) - 1);
    const auto stop = static_cast<std::size_t>(
        std::clamp(stopRib, 2, lastInternalRib) - 1);
    if (stop <= start) return;
    controlParts.push_back({name, start, stop, width, p.controlSurfaceGap,
                            hingeWidth, hingeHeight, {}, {}});
    controlParts.back().profiles.reserve(stop - start + 1);
    controlParts.back().hingePostCenters.reserve(stop - start + 1);
  };
  addControl(p.flaps, "Flap", p.flapWidth, p.flapHingePostWidth,
             p.flapHingePostHeight, p.flapStartRib, p.flapStopRib);
  addControl(p.ailerons, "Aileron", p.aileronWidth, p.aileronHingePostWidth,
             p.aileronHingePostHeight, p.aileronStartRib, p.aileronStopRib);
  std::vector<Point2> carbonSparCenters;
  if (p.carbonSpar != 0) {
    std::vector<Point2> nominal;
    for (const auto& rib : ribs) nominal.push_back(camberCenter(rib, 0.25 * rib.chord));
    carbonSparCenters = straightMemberCenters(ribs, nominal);
  }
  std::vector<Point2> carbonLeadingEdgeCenters;
  if (p.leadingEdgeType == 3 || p.leadingEdgeType == 4) {
    const double diameter = p.leadingEdgeType == 3 ? p.leadingEdgeTubeOd : p.leadingEdgeRodOd;
    carbonLeadingEdgeCenters = straightFittedLeadingEdgeCenters(ribs, diameter);
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
  SheetingPart leTopSheet{"LE top sheeting", stopIndex(p.leTopSheetStopRib), {}};
  SheetingPart leBottomSheet{"LE bottom sheeting", stopIndex(p.leBottomSheetStopRib), {}};
  SheetingPart teTopSheet{"TE top sheeting", stopIndex(p.teTopSheetStopRib), {}};
  SheetingPart teBottomSheet{"TE bottom sheeting", stopIndex(p.teBottomSheetStopRib), {}};

  for (std::size_t ribIndex = 0; ribIndex < ribs.size(); ++ribIndex) {
    const auto& rib = ribs[ribIndex];
    const auto [upper, lower] = localSurfaces(rib);
    const bool solidLeadingEdge = p.leadingEdgeType == 1 || p.leadingEdgeType == 2;
    const bool solidTrailingEdge = p.trailingEdgeType == 1 || p.trailingEdgeType == 2;
    const double minimumX = solidLeadingEdge
        ? std::clamp(p.leadingEdgeWidth, 0.001, rib.chord - 0.001) : 0.0;
    const double maximumX = solidTrailingEdge
        ? std::clamp(rib.chord - p.trailingEdgeWidth, 0.001, rib.chord - 0.001)
        : rib.chord;
    const bool slottedSheet = p.trailingEdgeType == 2 && p.trailingEdgeSlotted;
    double retainedMaximumX = slottedSheet
        ? std::min(rib.chord, maximumX + trailingEdgeSlotDepth) : maximumX;
    const double fullSheetingMaximumX = retainedMaximumX;
    double controlSheetingMaximumX = retainedMaximumX;
    if (minimumX >= maximumX)
      throw std::invalid_argument("Leading- and trailing-edge cuts overlap");
    const auto validateCutHeight = [&](const char* edgeName, const double cutX,
                                       const double specifiedHeight) {
      const double cutHeight = interpolateY(upper, cutX) - interpolateY(lower, cutX);
      if (cutHeight + 1.0e-8 < specifiedHeight) {
        throw std::invalid_argument(std::string{edgeName} + " cut edge at rib " +
            std::to_string(ribIndex + 1) + " is " + std::to_string(cutHeight) +
            " mm, less than the specified " + edgeName + " Height of " +
            std::to_string(specifiedHeight) + " mm");
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
      if (ribIndex > control.startRibIndex && ribIndex < control.stopRibIndex) {
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
    const double topLeEnd = p.carbonSpar != 0 ? sparCenter : sparCenter - topWoodWidth * 0.5;
    const double topTeStart = p.carbonSpar != 0 ? sparCenter : sparCenter + topWoodWidth * 0.5;
    const double bottomLeEnd = p.carbonSpar != 0 ? sparCenter : sparCenter - bottomWoodWidth * 0.5;
    const double bottomTeStart = p.carbonSpar != 0 ? sparCenter : sparCenter + bottomWoodWidth * 0.5;
    const double leadingCarbonDiameter = p.leadingEdgeType == 3
        ? p.leadingEdgeTubeOd : p.leadingEdgeType == 4 ? p.leadingEdgeRodOd : 0.0;
    constexpr double carbonSheetingClearance = 0.05;
    const double sheetingMinimumX = (p.leadingEdgeType == 3 || p.leadingEdgeType == 4)
        ? carbonLeadingEdgeCenters[ribIndex].x + leadingCarbonDiameter * 0.5 +
              carbonSheetingClearance
        : minimumX;
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
    addSheet(leTopSheet, p.leTopSheet, p.leTopSheetThickness, upper,
             sheetingMinimumX, topLeEnd, true, upperRecesses);
    addSheet(teTopSheet, p.teTopSheet, p.teTopSheetThickness, upper,
             topTeStart, retainedMaximumX, true, upperRecesses);
    addSheet(leBottomSheet, p.leBottomSheet, p.leBottomSheetThickness, lower,
             sheetingMinimumX, bottomLeEnd, false, lowerRecesses);
    addSheet(teBottomSheet, p.teBottomSheet, p.teBottomSheetThickness, lower,
             bottomTeStart, retainedMaximumX, false, lowerRecesses);
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
      append(part.fullProfiles, fullSheetingMaximumX);
      append(part.controlProfiles, controlSheetingMaximumX);
    };
    addTeAlternatives(teTopSheet, p.teTopSheet, p.teTopSheetThickness,
                      upper, topTeStart, true);
    addTeAlternatives(teBottomSheet, p.teBottomSheet, p.teBottomSheetThickness,
                      lower, bottomTeStart, false);
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
    if (p.carbonSpar != 0) {
      const double diameter = p.carbonSpar == 1 ? p.cfTubeOd : p.cfRodOd;
      structured.holes.push_back(circle(carbonSparCenters[ribIndex], diameter));
    }
    if (p.leadingEdgeType == 3 || p.leadingEdgeType == 4) {
      const double diameter = p.leadingEdgeType == 3 ? p.leadingEdgeTubeOd : p.leadingEdgeRodOd;
      const auto leadingHole = circle(carbonLeadingEdgeCenters[ribIndex], diameter);
      // The sheeting boundary is kept aft of the carbon member, so the hole is
      // fully contained by the rib face and can be included in the extrusion.
      // This is substantially more reliable than a near-tangent solid cut.
      structured.holes.push_back(leadingHole);
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
          structured.booleanCutouts.push_back(rectangle(cut));
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

  if (p.leTopSheet && !leTopSheet.profiles.empty()) wing.sheeting.push_back(std::move(leTopSheet));
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
  if (p.teTopSheet && !teTopSheet.profiles.empty()) {
    markControlBays(teTopSheet);
    wing.sheeting.push_back(std::move(teTopSheet));
  }
  if (p.teBottomSheet && !teBottomSheet.profiles.empty()) {
    markControlBays(teBottomSheet);
    wing.sheeting.push_back(std::move(teBottomSheet));
  }

  if (!leadingStock.profiles.empty()) wing.profiledMembers.push_back(std::move(leadingStock));
  std::vector<std::pair<std::size_t, std::size_t>> excludedRanges;
  for (const auto& control : controlParts)
    excludedRanges.emplace_back(control.startRibIndex, control.stopRibIndex);
  std::sort(excludedRanges.begin(), excludedRanges.end());
  std::vector<std::pair<std::size_t, std::size_t>> mergedExclusions;
  for (const auto& range : excludedRanges) {
    if (mergedExclusions.empty() || range.first > mergedExclusions.back().second)
      mergedExclusions.push_back(range);
    else
      mergedExclusions.back().second = std::max(mergedExclusions.back().second, range.second);
  }
  std::vector<std::pair<std::size_t, std::size_t>> trailingRanges;
  std::size_t rangeStart = 0;
  for (const auto& excluded : mergedExclusions) {
    if (rangeStart <= excluded.first) trailingRanges.emplace_back(rangeStart, excluded.first);
    rangeStart = std::max(rangeStart, excluded.second);
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

  if (p.topSpar) addRectMember(wing, "Top spar", 0.25, true, p.topSparWidth, p.topSparHeight);
  if (p.bottomSpar) addRectMember(wing, "Bottom spar", 0.25, false, p.bottomSparWidth, p.bottomSparHeight);
  if (p.topRearSpar) addRectMember(wing, "Top 60% rear spar", 0.60, true, p.topRearSparWidth, p.topRearSparHeight);
  if (p.bottomRearSpar) addRectMember(wing, "Bottom 60% rear spar", 0.60, false, p.bottomRearSparWidth, p.bottomRearSparHeight);
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

  if (p.shearWebs && p.topSpar && p.bottomSpar) {
    for (std::size_t i = 0; i + 1 < wing.ribs.size(); ++i) {
      const bool occupiedByWoodJoiner = std::any_of(
          wing.joiners.begin(), wing.joiners.end(), [i](const JoinerPart& joiner) {
            return joiner.kind == SpanMemberKind::Rectangular &&
                   i < joiner.stopRibIndex;
          });
      if (occupiedByWoodJoiner) continue;
      auto top0 = surfaceCenter(wing.ribs[i].rib, 0.25, true, p.topSparHeight);
      auto bottom0 = surfaceCenter(wing.ribs[i].rib, 0.25, false, p.bottomSparHeight);
      auto top1 = surfaceCenter(wing.ribs[i + 1].rib, 0.25, true, p.topSparHeight);
      auto bottom1 = surfaceCenter(wing.ribs[i + 1].rib, 0.25, false, p.bottomSparHeight);
      // Webs fill the clear space between the inward-facing spar surfaces.
      top0.y -= p.topSparHeight * 0.5;
      top1.y -= p.topSparHeight * 0.5;
      bottom0.y += p.bottomSparHeight * 0.5;
      bottom1.y += p.bottomSparHeight * 0.5;
      const double bay = wing.ribs[i + 1].rib.spanPosition - wing.ribs[i].rib.spanPosition;
      ShearWebPart web;
      web.name = "SW" + std::to_string(i + 1);
      web.bayIndex = i + 1;
      web.thickness = p.shearWebThickness;
      web.outline = {{0.0, 0.0}, {bay, bottom1.y - bottom0.y},
                     {bay, top1.y - bottom0.y}, {0.0, top0.y - bottom0.y}};
      web.stationCorners = {bottom0, bottom1, top1, top0};
      wing.shearWebs.push_back(std::move(web));
    }
  }
  return wing;
}

} // namespace designrc::domain
