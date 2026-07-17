#pragma once

#include "domain/AirfoilProfile.h"

#include <cstddef>
#include <vector>

namespace designrc::domain {

struct RibDefinition {
  double spanPosition{};
  double chord{};
  double leadingEdgeOffset{};
  double dihedralHeight{};
  double twistDegrees{};
  double ribPlaneAngleDegrees{}; // span-plane normal angle above horizontal
  double ribThicknessStartFactor{-0.5}; // start face in material-thickness units
  AirfoilProfile profile;
};

struct WingParameters {
  double halfSpan{700.0};
  double rootChord{240.0};
  double tipChord{150.0};
  double sweep{70.0};
  double dihedralDegrees{4.0};
  double tipTwistDegrees{0.0};
  double ribThickness{3.0};
  std::size_t ribCount{9};
};

struct WingMetrics {
  double fullSpan{};
  double planformArea{};
  double aspectRatio{};
  double taperRatio{};
};

struct PanelAssemblyAngles {
  double panelInclinationDegrees{};
  double rootRibAngleDegrees{};
  double intermediateRibAngleDegrees{};
  double tipRibAngleDegrees{};
};

[[nodiscard]] std::vector<PanelAssemblyAngles> calculatePanelAssemblyAngles(
    const std::vector<double>& panelDihedralDegrees);

[[nodiscard]] std::vector<RibDefinition> generateRibs(
    const WingParameters& parameters,
    const AirfoilProfile& root,
    const AirfoilProfile& tip);

[[nodiscard]] WingMetrics calculateWingMetrics(const WingParameters& parameters);

} // namespace designrc::domain
