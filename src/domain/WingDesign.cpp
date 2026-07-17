#include "domain/WingDesign.h"

#include <cmath>
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

std::vector<RibDefinition> generateRibs(
    const WingParameters& p, const AirfoilProfile& root, const AirfoilProfile& tip) {
  validate(p);
  std::vector<RibDefinition> ribs;
  ribs.reserve(p.ribCount);
  const double dihedral = p.dihedralDegrees * std::numbers::pi / 180.0;
  for (std::size_t i = 0; i < p.ribCount; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(p.ribCount - 1);
    const double span = p.halfSpan * t;
    ribs.push_back({span,
      p.rootChord + t * (p.tipChord - p.rootChord),
      p.sweep * t,
      std::tan(dihedral) * span,
      p.tipTwistDegrees * t,
      p.dihedralDegrees,
      -0.5,
      AirfoilProfile::interpolate(root, tip, t)});
  }
  return ribs;
}

WingMetrics calculateWingMetrics(const WingParameters& p) {
  validate(p);
  const double fullSpan = p.halfSpan * 2.0;
  const double area = p.halfSpan * (p.rootChord + p.tipChord);
  return {fullSpan, area, fullSpan * fullSpan / area, p.tipChord / p.rootChord};
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

} // namespace designrc::domain
