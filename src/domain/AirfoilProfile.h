#pragma once

#include "domain/Point2.h"

#include <cstddef>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace designrc::domain {

class AirfoilProfile {
public:
  static AirfoilProfile fromDat(std::istream& input);
  static AirfoilProfile fromDatFile(const std::filesystem::path& path);
  static AirfoilProfile nacaSymmetric(double thicknessRatio, std::size_t surfaceSamples = 61);
  static AirfoilProfile interpolate(
      const AirfoilProfile& root,
      const AirfoilProfile& tip,
      double fraction,
      std::size_t surfaceSamples = 81);

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::vector<Point2>& outline() const { return outline_; }
  [[nodiscard]] std::vector<Point2> resampled(std::size_t surfaceSamples) const;

private:
  AirfoilProfile(std::string name, std::vector<Point2> normalizedOutline);

  std::string name_;
  std::vector<Point2> outline_;
};

} // namespace designrc::domain

