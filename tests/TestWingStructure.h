#pragma once
#include "domain/WingStructure.h"
#include <cmath>
#include <algorithm>

namespace testsupport {
// Older fixtures exercise structure/export behavior with legacy post sizes.
// Fit those posts to the new minimum, leaving explicit validation tests strict.
inline designrc::domain::StructuredWing buildStructure(
    const std::vector<designrc::domain::RibDefinition>& ribs,
    designrc::domain::StructureParameters& parameters) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    try { return designrc::domain::applyWingStructure(ribs, parameters); }
    catch (const designrc::domain::EdgeHeightError& error) {
      if (error.edgeName() != "Aileron Hinge Post" && error.edgeName() != "Flap Hinge Post") throw;
      const double height = std::max({parameters.aileronHingePostHeight, parameters.flapHingePostHeight,
          std::ceil(error.cutHeightMm() * 100.0) / 100.0});
      parameters.aileronHingePostHeight = parameters.flapHingePostHeight = height;
    }
  }
  throw std::runtime_error("Test fixture hinge posts could not be fitted");
}
inline designrc::domain::StructuredWing buildStructure(
    const std::vector<designrc::domain::RibDefinition>& ribs,
    const designrc::domain::StructureParameters& parameters) {
  auto fitted = parameters;
  return buildStructure(ribs, fitted);
}
} // namespace testsupport
