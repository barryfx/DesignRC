#pragma once

#include "domain/WingDesign.h"
#include "domain/WingStructure.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>

#include <functional>
#include <string>
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

enum class PartMaterial { Wood, CarbonFiber, Aluminum, Steel, Fiberglass };

struct NamedPartShape {
  std::string name;
  TopoDS_Shape shape;
  PartMaterial material{PartMaterial::Wood};
  bool mirrorInAssembly{true};
};

struct MaterialShapeSet {
  TopoDS_Compound wood;
  TopoDS_Compound carbonFiber;
  TopoDS_Compound aluminum;
  TopoDS_Compound steel;
  TopoDS_Compound fiberglass;
  TopoDS_Compound unmirroredWood;
  TopoDS_Compound unmirroredCarbonFiber;
  TopoDS_Compound unmirroredAluminum;
  TopoDS_Compound unmirroredSteel;
  TopoDS_Compound unmirroredFiberglass;
  std::vector<NamedPartShape> parts;
};

using GeometryProgressCallback =
    std::function<void(int, const std::string&)>;

[[nodiscard]] TopoDS_Shape buildWingPreview(
    const std::vector<domain::RibDefinition>& ribs,
    double ribThickness,
    bool mirrorHalfWing = false);

[[nodiscard]] TopoDS_Shape buildStructuredWingPreview(
    const domain::StructuredWing& wing,
    double ribThickness,
    PanelBuildTimings* timings = nullptr,
    MaterialShapeSet* materialShapes = nullptr,
    const GeometryProgressCallback& progress = {},
    std::size_t maximumRibWorkers = 0);

[[nodiscard]] std::size_t ribGeometryWorkerCount(
    std::size_t ribCount, std::size_t maximumWorkers = 0);

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
