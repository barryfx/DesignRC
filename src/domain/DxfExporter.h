#pragma once

#include "domain/WingDesign.h"
#include "domain/WingStructure.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace designrc::domain {

struct PartDrawingPath {
  std::vector<Point2> points;
  std::string layer;
  bool spline{false};
  bool closed{true};
};

struct PartLabelPlacement {
  Point2 position;
  double height{};
};

struct PartDrawing {
  std::string label;
  std::vector<PartDrawingPath> paths;
  std::vector<Point2> labelOutline;
  std::vector<std::vector<Point2>> labelExclusions;
  bool rotateForComposite{false};
  std::optional<PartLabelPlacement> preferredLabelPlacement;
};

std::optional<PartLabelPlacement> partLabelPlacement(const PartDrawing& part);

PartDrawing makeStructuredRibPartDrawing(
    const StructuredRib& rib, const std::string& label);
PartDrawing makeShearWebPartDrawing(
    const ShearWebPart& web, const std::string& label);
PartDrawing makeSheetStockPartDrawing(
    const SheetStockPart& stock, const std::string& label);
PartDrawing makeWoodJoinerPartDrawing(
    const JoinerPart& joiner, const std::string& label);
PartDrawing makeSpoilerPartDrawing(
    const SpoilerPart& spoiler, const std::string& label);
PartDrawing makeDihedralAnglePartDrawing(
    double dihedralDegrees, const std::string& label);

std::vector<PartDrawing> arrangePartDrawings(
    const std::vector<PartDrawing>& parts, double gap = 5.0);
void exportPartsDxf(const std::vector<PartDrawing>& parts,
                    const std::filesystem::path& path);
void exportPartsSvg(const std::vector<PartDrawing>& parts,
                    const std::filesystem::path& path);

void exportRibDxf(
    const RibDefinition& rib,
    const std::filesystem::path& path,
    const std::string& label);

void exportStructuredRibDxf(
    const StructuredRib& rib,
    const std::filesystem::path& path,
    const std::string& label);

void exportShearWebDxf(
    const ShearWebPart& web,
    const std::filesystem::path& path,
    const std::string& label);

void exportSheetStockDxf(
    const SheetStockPart& stock,
    const std::filesystem::path& path,
    const std::string& label);

void exportWoodJoinerDxf(
    const JoinerPart& joiner,
    const std::filesystem::path& path,
    const std::string& label);
void exportSpoilerDxf(
    const SpoilerPart& spoiler, const std::filesystem::path& path,
    const std::string& label);

void exportDihedralAngleDxf(
    double dihedralDegrees,
    const std::filesystem::path& path,
    const std::string& label);

void exportRibSvg(
    const RibDefinition& rib,
    const std::filesystem::path& path,
    const std::string& label);

void exportStructuredRibSvg(
    const StructuredRib& rib,
    const std::filesystem::path& path,
    const std::string& label);

void exportShearWebSvg(
    const ShearWebPart& web,
    const std::filesystem::path& path,
    const std::string& label);

void exportSheetStockSvg(
    const SheetStockPart& stock,
    const std::filesystem::path& path,
    const std::string& label);

void exportWoodJoinerSvg(
    const JoinerPart& joiner,
    const std::filesystem::path& path,
    const std::string& label);
void exportSpoilerSvg(
    const SpoilerPart& spoiler, const std::filesystem::path& path,
    const std::string& label);

void exportDihedralAngleSvg(
    double dihedralDegrees,
    const std::filesystem::path& path,
    const std::string& label);

} // namespace designrc::domain
