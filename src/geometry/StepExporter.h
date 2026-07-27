#pragma once

#include "geometry/OcctRibBuilder.h"

#include <filesystem>
#include <string>
#include <vector>

namespace designrc::geometry {

void exportStepAssembly(const std::vector<NamedPartShape>& parts,
                        const std::filesystem::path& path,
                        const std::string& assemblyName);

} // namespace designrc::geometry
