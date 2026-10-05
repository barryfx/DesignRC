#include "domain/WingDesign.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace designrc::domain {

namespace {
void validate(const WingParameters& p) {
  if (p.ribCount < 2 || p.halfSpan <= 0.0 || p.rootChord <= 0.0 || p.tipChord <= 0.0 ||
      p.ribThickness <= 0.0)
    throw std::invalid_argument("Wing dimensions, material thickness, and rib count must be positive");
}
} // namespace

double untwistedRibBottom(const RibDefinition& rib) {
  double bottom = std::numeric_limits<double>::max();
  for (const auto point : rib.profile.outline())
    bottom = std::min(bottom, point.y * rib.chord);
  return bottom;
}

Point2 ribTwistTranslation(const RibDefinition& rib) {
  if (rib.twistDegrees == 0.0) return {};
  const double angle = rib.twistDegrees * std::numbers::pi / 180.0;
  const double sine = std::sin(angle);
  const double cosine = std::cos(angle);
  const double pivot = rib.twistDegrees < 0.0 ? rib.chord : 0.0;
  Point2 translation{pivot * (1.0 - cosine), -pivot * sine};
  double bottom = std::numeric_limits<double>::max();
  for (const auto point : rib.profile.outline())
    bottom = std::min(bottom,
        rib.chord * (sine * point.x + cosine * point.y) + translation.y);
  translation.y += std::max(0.0, untwistedRibBottom(rib) - bottom);
  return translation;
}

std::vector<RibDefinition> generateRibs(
    const WingParameters& p, const AirfoilProfile& root, const AirfoilProfile& tip) {
  validate(p);
  std::vector<RibDefinition> ribs;
  ribs.reserve(p.ribCount);
  p.planform.validate(p.halfSpan);
  for (std::size_t i = 0; i < p.ribCount; ++i)
    ribs.push_back(
        ribAtStation(p, root, tip, static_cast<double>(i) / static_cast<double>(p.ribCount - 1)));
  return ribs;
}

RibDefinition ribAtStation(const WingParameters& p, const AirfoilProfile& root,
                           const AirfoilProfile& tip, double t) {
  const double span = p.halfSpan * t;
  RibDefinition rib{span,
                    p.planform.empty() ? p.rootChord + t * (p.tipChord - p.rootChord)
                                       : p.planform.chord(t),
                    p.planform.empty() ? p.sweep * t : p.planform.leadingX(t),
                    std::tan(p.dihedralDegrees * std::numbers::pi / 180.0) * span,
                    p.rootTwistDegrees + t * (p.tipTwistDegrees - p.rootTwistDegrees),
                    p.dihedralDegrees,
                    -0.5,
                    AirfoilProfile::interpolate(root, tip, t)};
  if (!p.planform.empty()) rib.planform = std::make_shared<PlanformCurves>(p.planform);
  rib.planformStation = t;
  return rib;
}

RibDefinition interpolateRib(const RibDefinition& a, const RibDefinition& b, double t) {
  const auto mix = [t](double x, double y) { return x + (y - x) * t; };
  RibDefinition rib{mix(a.spanPosition, b.spanPosition),
                    mix(a.chord, b.chord),
                    mix(a.leadingEdgeOffset, b.leadingEdgeOffset),
                    mix(a.dihedralHeight, b.dihedralHeight),
                    mix(a.twistDegrees, b.twistDegrees),
                    mix(a.ribPlaneAngleDegrees, b.ribPlaneAngleDegrees),
                    -0.5,
                    AirfoilProfile::interpolate(a.profile, b.profile, t)};
  if (a.planform && b.planform) {
    rib.planform = a.planform;
    rib.ribPlaneAngleDegrees =
        std::atan2(b.dihedralHeight - a.dihedralHeight, b.spanPosition - a.spanPosition) * 180.0 /
        std::numbers::pi;
    rib.planformStation = mix(a.planformStation, b.planformStation);
    const auto& curve = *rib.planform;
    rib.leadingEdgeOffset +=
        curve.leadingX(rib.planformStation) -
        mix(curve.leadingX(a.planformStation), curve.leadingX(b.planformStation));
    rib.chord = curve.chord(rib.planformStation);
  }
  return rib;
}

WingMetrics calculateWingMetrics(const WingParameters& p) {
  validate(p);
  const double fullSpan = p.halfSpan * 2.0;
  const double area = p.planform.empty() ? p.halfSpan * (p.rootChord + p.tipChord)
                                         : 2 * p.planform.area(p.halfSpan);
  return {fullSpan, area, fullSpan * fullSpan / area,
          p.planform.empty() ? p.tipChord / p.rootChord
                             : p.planform.chord(1) / p.planform.chord(0)};
}

std::vector<PanelAssemblyAngles> calculatePanelAssemblyAngles(
    const std::vector<double>& panelDihedralDegrees) {
  std::vector<PanelAssemblyAngles> result;
  result.reserve(panelDihedralDegrees.size());
  std::vector<double> inclinations;
  inclinations.reserve(panelDihedralDegrees.size());
  for (std::size_t i = 0; i < panelDihedralDegrees.size(); ++i) {
    inclinations.push_back(i == 0 ? panelDihedralDegrees[i] * 0.5
                                  : inclinations.back() + panelDihedralDegrees[i]);
  }
  for (std::size_t i = 0; i < inclinations.size(); ++i) {
    const double rootAngle = i == 0 ? inclinations[i]
        : 0.5 * (inclinations[i - 1] + inclinations[i]);
    const double tipAngle = i + 1 < inclinations.size()
        ? 0.5 * (inclinations[i] + inclinations[i + 1])
        : inclinations[i];
    result.push_back({inclinations[i], rootAngle, inclinations[i], tipAngle});
  }
  return result;
}

std::vector<PanelTwistRange> calculatePanelTwistRanges(
    const std::vector<double>& panelTwistDegrees) {
  std::vector<PanelTwistRange> result;
  result.reserve(panelTwistDegrees.size());
  double rootTwist = 0.0;
  for (const double panelTwist : panelTwistDegrees) {
    const double tipTwist = rootTwist + panelTwist;
    result.push_back({rootTwist, tipTwist});
    rootTwist = tipTwist;
  }
  return result;
}

} // namespace designrc::domain
