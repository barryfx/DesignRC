#pragma once

#include "domain/WingDesign.h"
#include "domain/WingStructure.h"

#include <filesystem>
#include <string>

namespace designrc::domain {

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

void exportDihedralAngleDxf(
    double dihedralDegrees,
    const std::filesystem::path& path,
    const std::string& label);

} // namespace designrc::domain
