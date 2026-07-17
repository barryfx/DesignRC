#pragma once

#include "domain/WingDesign.h"
#include "domain/WingStructure.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>

#include <vector>

namespace designrc::geometry {

struct PanelBuildTimings {
  double ribsMs{};
  double profiledStockMs{};
  double controlsMs{};
  double sheetingMs{};
  double membersMs{};
  double shearWebsMs{};
  double joinersMs{};
  double displayMeshMs{};
};

struct MaterialShapeSet {
  TopoDS_Compound wood;
  TopoDS_Compound carbonFiber;
  TopoDS_Compound aluminum;
};

[[nodiscard]] TopoDS_Shape buildWingPreview(
    const std::vector<domain::RibDefinition>& ribs,
    double ribThickness,
    bool mirrorHalfWing = false);

[[nodiscard]] TopoDS_Shape buildStructuredWingPreview(
    const domain::StructuredWing& wing,
    double ribThickness,
    PanelBuildTimings* timings = nullptr,
    MaterialShapeSet* materialShapes = nullptr);

[[nodiscard]] TopoDS_Shape buildMirroredWingAssemblyPreview(
    const std::vector<domain::StructuredWing>& panels,
    const std::vector<double>& ribThicknesses);

[[nodiscard]] TopoDS_Shape assembleMirroredWingPreview(
    const std::vector<TopoDS_Shape>& panelShapes);

[[nodiscard]] TopoDS_Shape assembleHalfWingPreview(
    const std::vector<TopoDS_Shape>& panelShapes);

[[nodiscard]] MaterialShapeSet assembleMirroredMaterialPreview(
    const std::vector<MaterialShapeSet>& panelShapes);

} // namespace designrc::geometry
