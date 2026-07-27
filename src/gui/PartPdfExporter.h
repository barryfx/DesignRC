#pragma once

#include "domain/DxfExporter.h"

#include <filesystem>
#include <vector>

namespace designrc::gui {

void exportPartPdf(const domain::PartDrawing& part,
                   const std::filesystem::path& path);
void exportPartsPdf(const std::vector<domain::PartDrawing>& parts,
                    const std::filesystem::path& path);

} // namespace designrc::gui
