#pragma once

#include "domain/Point2.h"
#include <filesystem>
#include <vector>

namespace designrc::domain {

// Millimeter X coordinates, normalized root-to-tip Y coordinates. Curves are
// flattened to 0.01 mm on import, independently of the number of physical ribs.
struct PlanformCurves {
  std::vector<Point2> leading;
  std::vector<Point2> trailing;
  double leadingSourceSpan{};
  double trailingSourceSpan{};
  [[nodiscard]] bool empty() const { return leading.empty() && trailing.empty(); }
  [[nodiscard]] double leadingX(double station) const;
  [[nodiscard]] double trailingX(double station) const;
  [[nodiscard]] double chord(double station) const;
  [[nodiscard]] double area(double span) const;
  void validateGeometry() const;
  void validate(double span) const;
};

[[nodiscard]] PlanformCurves importPlanformCurves(const std::filesystem::path& path,
                                                  double panelSpan);

} // namespace designrc::domain
