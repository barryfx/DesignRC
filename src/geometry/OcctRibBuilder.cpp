#include "geometry/OcctRibBuilder.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GC_MakeSegment2d.hxx>
#include <Geom_Plane.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Precision.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#if OCC_VERSION_HEX >= 0x080000
#include <NCollection_HArray1.hxx>
#else
#include <TColgp_HArray1OfPnt.hxx>
#endif
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Wire.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <stdexcept>
#include <sstream>
#include <string_view>
#include <atomic>
#include <future>
#include <vector>
#include <numbers>
#include <numeric>
#include <optional>
#include <limits>
#include <cmath>
#include <chrono>
#include <thread>

namespace designrc::geometry {

#if OCC_VERSION_HEX >= 0x080000
using OcctPointArray = NCollection_HArray1<gp_Pnt>;
#else
using OcctPointArray = TColgp_HArray1OfPnt;
#endif

TopoDS_Shape buildWingPreview(
    const std::vector<domain::RibDefinition>& ribs, const double ribThickness,
    const bool mirrorHalfWing) {
  if (ribs.empty() || ribThickness <= 0.0)
    throw std::invalid_argument("Wing preview requires ribs and positive material thickness");

  BRep_Builder builder;
  TopoDS_Compound wing;
  builder.MakeCompound(wing);

  const auto addRib = [&](const domain::RibDefinition& rib, const double side) {
    // A smooth 49-point display outline keeps interactive rebuilds responsive.
    // The full profile remains on RibDefinition and is used by DXF export.
    const auto previewOutline = rib.profile.resampled(25);
    std::vector<gp_Pnt> modelPoints;
    modelPoints.reserve(previewOutline.size());
    const double twist = rib.twistDegrees * std::numbers::pi / 180.0;
    const double twistCos = std::cos(twist);
    const double twistSin = std::sin(twist);
    const double planeAngle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const double normalY = side * std::cos(planeAngle);
    const double normalZ = std::sin(planeAngle);
    const bool centerRoot = std::abs(rib.spanPosition) < 1.0e-9 &&
        std::abs(rib.ribThicknessStartFactor) < 1.0e-9;
    const double verticalY = centerRoot ? 0.0 : -side * std::sin(planeAngle);
    const double verticalZ = centerRoot ? 1.0 : std::cos(planeAngle);
    const double faceNormalY = centerRoot ? side : normalY;
    const double faceNormalZ = centerRoot ? 0.0 : normalZ;
    for (const auto& point : previewOutline) {
      const double localX = point.x * rib.chord;
      const double localZ = point.y * rib.chord;
      const double sectionX = twistCos * localX - twistSin * localZ;
      const double sectionZ = twistSin * localX + twistCos * localZ;
      const double startOffset = ribThickness * rib.ribThicknessStartFactor;
      modelPoints.emplace_back(
          rib.leadingEdgeOffset + sectionX,
          side * rib.spanPosition + verticalY * sectionZ + normalY * startOffset,
          rib.dihedralHeight + verticalZ * sectionZ + normalZ * startOffset);
    }

    const std::size_t leadingEdge = previewOutline.size() / 2;
    BRepBuilderAPI_MakePolygon polygon;
    for (std::size_t i = 0; i + 1 < modelPoints.size(); ++i) polygon.Add(modelPoints[i]);
    polygon.Close();
    if (!polygon.IsDone()) throw std::runtime_error("Unable to construct a closed rib outline");

    const gp_Pln ribPlane{modelPoints.front(), gp_Dir{0.0, faceNormalY, faceNormalZ}};
    BRepBuilderAPI_MakeFace faceBuilder{ribPlane, polygon.Wire(), true};
    if (!faceBuilder.IsDone()) throw std::runtime_error("Unable to fill the rib outline");
    BRepPrimAPI_MakePrism prism{faceBuilder.Face(),
        gp_Vec{0.0, normalY * ribThickness, normalZ * ribThickness}};
    if (!prism.IsDone()) throw std::runtime_error("Unable to extrude the rib outline");
    auto solid = prism.Shape();
    BRepTools::Clean(solid);
    if (solid.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer{solid, false}.IsValid())
      throw std::runtime_error("Rib extrusion did not produce a valid solid");
    BRepMesh_IncrementalMesh mesher{solid, 0.75, false, 0.35, true};
    if (!mesher.IsDone())
      throw std::runtime_error("Unable to create the shaded mesh for a solid rib");
    builder.Add(wing, solid);

    // OCCT can leave complex imported planar caps untriangulated even when the
    // enclosing prism is a valid solid. Tile both cap planes with simple faces
    // so shaded display always represents the closed volume.
    const auto addCapTiles = [&](const double yOffset) {
      const auto shifted = [yOffset, normalY, normalZ](gp_Pnt point) {
        point.SetY(point.Y() + normalY * yOffset);
        point.SetZ(point.Z() + normalZ * yOffset);
        return point;
      };
      for (std::size_t segment = 0; segment < leadingEdge; ++segment) {
        const gp_Pnt upperA = shifted(modelPoints[leadingEdge - segment]);
        const gp_Pnt upperB = shifted(modelPoints[leadingEdge - segment - 1]);
        const gp_Pnt lowerA = shifted(modelPoints[leadingEdge + segment]);
        const gp_Pnt lowerB = shifted(modelPoints[leadingEdge + segment + 1]);
        BRepBuilderAPI_MakePolygon tile;
        tile.Add(upperA);
        tile.Add(upperB);
        tile.Add(lowerB);
        if (segment > 0) tile.Add(lowerA);
        tile.Close();
        if (!tile.IsDone()) throw std::runtime_error("Unable to construct a rib cap tile");
        BRepBuilderAPI_MakeFace cap{
            gp_Pln{upperA, gp_Dir{0.0, faceNormalY, faceNormalZ}}, tile.Wire(), true};
        if (!cap.IsDone()) throw std::runtime_error("Unable to fill a rib cap tile");
        auto capFace = cap.Face();
        BRepMesh_IncrementalMesh capMesher{capFace, 0.75, false, 0.35, true};
        builder.Add(wing, capFace);
      }
    };
    std::size_t meshedCaps = 0;
    for (TopExp_Explorer faces{solid, TopAbs_FACE}; faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface{face};
      if (surface.GetType() != GeomAbs_Plane ||
          std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
        continue;
      TopLoc_Location location;
      if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCaps;
    }
    if (meshedCaps < 2) {
      addCapTiles(0.0);
      addCapTiles(ribThickness);
    }
  };

  for (std::size_t i = 0; i < ribs.size(); ++i) {
    addRib(ribs[i], 1.0);
    if (mirrorHalfWing && i > 0) addRib(ribs[i], -1.0);
  }
  return wing;
}

namespace {

gp_Pnt transformLocal(const domain::RibDefinition& rib, const domain::Point2 point,
                      const double yOffset = 0.0) {
  const double angle = rib.twistDegrees * std::numbers::pi / 180.0;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  const double planeAngle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
  const double sectionX = cosine * point.x - sine * point.y;
  const double sectionZ = sine * point.x + cosine * point.y;
  const bool centerRoot = std::abs(rib.spanPosition) < 1.0e-9 &&
      std::abs(rib.ribThicknessStartFactor) < 1.0e-9;
  return {rib.leadingEdgeOffset + sectionX,
          rib.spanPosition - (centerRoot ? 0.0 : std::sin(planeAngle) * sectionZ) +
              std::cos(planeAngle) * yOffset,
          rib.dihedralHeight + (centerRoot ? 1.0 : std::cos(planeAngle)) * sectionZ +
              std::sin(planeAngle) * yOffset};
}

double ribStartOffset(const domain::RibDefinition& rib, const double thickness) {
  return rib.ribThicknessStartFactor * thickness;
}

double ribEndOffset(const domain::RibDefinition& rib, const double thickness) {
  return (rib.ribThicknessStartFactor + 1.0) * thickness;
}

TopoDS_Wire makeSplineProfileWire(
    const domain::RibDefinition& rib,
    const std::vector<domain::Point2>& profile,
    const std::size_t splitIndex, const double yOffset,
    const char* description) {
  if (profile.size() < 3 || splitIndex == 0 ||
      splitIndex + 1 >= profile.size())
    throw std::runtime_error(
        std::string{description} +
        " profile cannot be divided into upper and lower contours");

  const auto makeContourEdge = [&](const std::size_t begin,
                                   const std::size_t end) {
    if (end == begin + 1)
      return BRepBuilderAPI_MakeEdge{
          transformLocal(rib, profile[begin], yOffset),
          transformLocal(rib, profile[end], yOffset)}.Edge();
    const auto points = Handle(OcctPointArray){
        new OcctPointArray{
            1, static_cast<int>(end - begin + 1)}};
    for (std::size_t point = begin; point <= end; ++point)
      points->SetValue(
          static_cast<int>(point - begin + 1),
          transformLocal(rib, profile[point], yOffset));
    GeomAPI_Interpolate interpolation{
        points, false, Precision::Confusion()};
    interpolation.Perform();
    if (!interpolation.IsDone())
      throw std::runtime_error(
          std::string{"Unable to interpolate "} + description +
          " contour");
    return BRepBuilderAPI_MakeEdge{interpolation.Curve()}.Edge();
  };

  const auto firstPoint = transformLocal(rib, profile.front(), yOffset);
  const auto lastPoint = transformLocal(rib, profile.back(), yOffset);
  BRepBuilderAPI_MakeWire wire;
  wire.Add(makeContourEdge(0, splitIndex));
  wire.Add(makeContourEdge(splitIndex, profile.size() - 1));
  wire.Add(BRepBuilderAPI_MakeEdge{lastPoint, firstPoint}.Edge());
  if (!wire.IsDone())
    throw std::runtime_error(
        std::string{"Unable to construct spline "} + description +
        " profile");
  return wire.Wire();
}

TopoDS_Shape makeRectangularSegment(const gp_Pnt& start, const gp_Pnt& end,
                                    const double width, const double height) {
  BRepBuilderAPI_MakePolygon polygon;
  polygon.Add({start.X() - width * 0.5, start.Y(), start.Z() - height * 0.5});
  polygon.Add({start.X() + width * 0.5, start.Y(), start.Z() - height * 0.5});
  polygon.Add({start.X() + width * 0.5, start.Y(), start.Z() + height * 0.5});
  polygon.Add({start.X() - width * 0.5, start.Y(), start.Z() + height * 0.5});
  polygon.Close();
  BRepBuilderAPI_MakeFace face{polygon.Wire()};
  return BRepPrimAPI_MakePrism{face.Face(), gp_Vec{start, end}}.Shape();
}

TopoDS_Shape makeTubeSegment(const gp_Pnt& start, const gp_Pnt& end,
                             const double outerDiameter, const double innerDiameter) {
  const gp_Vec vector{start, end};
  const double length = vector.Magnitude();
  const gp_Ax2 axis{start, gp_Dir{vector}};
  auto outer = BRepPrimAPI_MakeCylinder{axis, outerDiameter * 0.5, length}.Shape();
  if (innerDiameter <= 0.0) return outer;
  const auto inner = BRepPrimAPI_MakeCylinder{axis, innerDiameter * 0.5, length}.Shape();
  return BRepAlgoAPI_Cut{outer, inner}.Shape();
}

TopoDS_Shape makeRectangularSpanMember(const domain::StructuredWing& wing,
                                       const domain::SpanMember& member,
                                       const double ribThickness) {
  if (member.centers.size() != wing.ribs.size())
    throw std::runtime_error("Rectangular member centers do not match the rib stations");
  BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
  loft.CheckCompatibility(false);
  const auto addProfile = [&](const std::size_t i, const double yOffset) {
    const auto& center = member.centers[i];
    BRepBuilderAPI_MakePolygon polygon;
    polygon.Add(transformLocal(wing.ribs[i].rib,
        {center.x - member.width * 0.5, center.y - member.height * 0.5}, yOffset));
    polygon.Add(transformLocal(wing.ribs[i].rib,
        {center.x + member.width * 0.5, center.y - member.height * 0.5}, yOffset));
    polygon.Add(transformLocal(wing.ribs[i].rib,
        {center.x + member.width * 0.5, center.y + member.height * 0.5}, yOffset));
    polygon.Add(transformLocal(wing.ribs[i].rib,
        {center.x - member.width * 0.5, center.y + member.height * 0.5}, yOffset));
    polygon.Close();
    if (!polygon.IsDone())
      throw std::runtime_error("Unable to construct rectangular member profile");
    loft.AddWire(polygon.Wire());
  };
  addProfile(0, ribStartOffset(wing.ribs[0].rib, ribThickness));
  for (std::size_t i = 0; i < wing.ribs.size(); ++i) {
    addProfile(i, ribEndOffset(wing.ribs[i].rib, ribThickness));
    if (i + 1 < wing.ribs.size())
      addProfile(i + 1, ribStartOffset(wing.ribs[i + 1].rib, ribThickness));
  }
  loft.Build();
  if (!loft.IsDone())
    throw std::runtime_error("Unable to loft rectangular span member");
  return loft.Shape();
}

} // namespace

std::size_t ribGeometryWorkerCount(
    const std::size_t ribCount, const std::size_t maximumWorkers) {
  if (ribCount == 0) return 0;
  const unsigned logicalProcessors = std::thread::hardware_concurrency();
  std::size_t available = logicalProcessors > 2
      ? static_cast<std::size_t>(logicalProcessors - 2) : 1;
  if (maximumWorkers > 0)
    available = std::min(available, maximumWorkers);
  return std::min(ribCount, available);
}

TopoDS_Shape buildStructuredWingPreview(const domain::StructuredWing& structuredWing,
                                        const double ribThickness,
                                        PanelBuildTimings* timings,
                                        MaterialShapeSet* materialShapes,
                                        const GeometryProgressCallback& progress,
                                        const std::size_t maximumRibWorkers) {
  if (structuredWing.ribs.empty() || ribThickness <= 0.0)
    throw std::invalid_argument("Structured wing preview requires ribs and positive thickness");
  BRep_Builder builder;
  TopoDS_Compound result;
  builder.MakeCompound(result);
  if (materialShapes) {
    builder.MakeCompound(materialShapes->wood);
    builder.MakeCompound(materialShapes->carbonFiber);
    builder.MakeCompound(materialShapes->aluminum);
    builder.MakeCompound(materialShapes->steel);
    builder.MakeCompound(materialShapes->fiberglass);
    builder.MakeCompound(materialShapes->unmirroredWood);
    builder.MakeCompound(materialShapes->unmirroredCarbonFiber);
    builder.MakeCompound(materialShapes->unmirroredAluminum);
    builder.MakeCompound(materialShapes->unmirroredSteel);
    builder.MakeCompound(materialShapes->unmirroredFiberglass);
  }
  const auto addShape = [&](const TopoDS_Shape& shape, const PartMaterial material,
                            const bool mirrorInAssembly = true,
                            const std::string& name = std::string{}) {
    builder.Add(result, shape);
    if (!materialShapes) return;
    if (!name.empty())
      materialShapes->parts.push_back({name, shape, material, mirrorInAssembly});
    switch (material) {
      case PartMaterial::Wood: builder.Add(mirrorInAssembly
          ? materialShapes->wood : materialShapes->unmirroredWood, shape); break;
      case PartMaterial::CarbonFiber: builder.Add(mirrorInAssembly
          ? materialShapes->carbonFiber : materialShapes->unmirroredCarbonFiber, shape); break;
      case PartMaterial::Aluminum: builder.Add(mirrorInAssembly
          ? materialShapes->aluminum : materialShapes->unmirroredAluminum, shape); break;
      case PartMaterial::Steel: builder.Add(mirrorInAssembly
          ? materialShapes->steel : materialShapes->unmirroredSteel, shape); break;
      case PartMaterial::Fiberglass: builder.Add(mirrorInAssembly
          ? materialShapes->fiberglass : materialShapes->unmirroredFiberglass, shape); break;
    }
  };
  struct NamedShape {
    std::string name;
    TopoDS_Shape shape;
    Bnd_Box bounds;
  };
  std::vector<NamedShape> nonRibShapes;
  const auto isNewSparPart = [](const std::string& name) {
    return name.starts_with("Spar ");
  };
  const auto isSheetingPart = [](const std::string& name) {
    return name.find("sheeting") != std::string::npos;
  };
  const auto isShearWebPart = [](const std::string& name) {
    return name.find("shear web") != std::string::npos || name.starts_with("SW");
  };
  const auto isSpoilerPart = [](const std::string& name) {
    return name == "Spoiler" || name.starts_with("Spoiler Frame Rail") ||
        name.starts_with("Spoiler Support Rail");
  };
  const auto isTrailingEdgePart = [](const std::string& name) {
    return name.starts_with("TE") ||
        name.find("trailing edge") != std::string::npos;
  };
  const auto isSparMember = [&](const std::string& name) {
    return !isShearWebPart(name) &&
        (name.find("Spar") != std::string::npos ||
         name.find("spar") != std::string::npos);
  };
  const auto isWoodJoiner = [](const std::string& name) {
    return name.starts_with("Wood ") || name.starts_with("Joiner ") ||
        name == "Center spar wood joiner" ||
        (name.size() > 1 && name.front() == 'J');
  };
  const auto isCheckedJoiner = [&](const std::string& name) {
    return !isWoodJoiner(name) &&
        (name.starts_with("Fixed Joiner") ||
         name.starts_with("Removable Joiner") ||
         name.starts_with("Alignment Pin"));
  };
  const auto joinerDefinitionName = [](const std::string& name) {
    const auto numberedPrefix = [&name](const std::string_view prefix) {
      if (!name.starts_with(prefix)) return std::string{};
      std::size_t end = prefix.size();
      while (end < name.size() && name[end] >= '0' && name[end] <= '9') ++end;
      return name.substr(0, end);
    };
    if (auto definition = numberedPrefix("Fixed Joiner "); !definition.empty())
      return definition;
    if (auto definition = numberedPrefix("Removable Joiner "); !definition.empty())
      return definition;
    return numberedPrefix("Alignment Pin ");
  };
  const auto isJoinerCollisionTarget = [&](const std::string& name) {
    return isCheckedJoiner(name) || isSparMember(name) ||
        name == "CF tube" || name == "CF rod" ||
        name.starts_with("LE") || name.starts_with("TE") ||
        name.find("leading edge") != std::string::npos ||
        name.find("trailing edge") != std::string::npos ||
        name.starts_with("Aileron") || name.starts_with("Flap");
  };
  const auto addPartShape = [&](const std::string& name, const TopoDS_Shape& shape,
                                const PartMaterial material,
                                const bool mirrorInAssembly = true) {
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    for (const auto& other : nonRibShapes) {
      // Legacy features already validate their own layout as they are built.
      // The generalized collision pass covers every new spar/web against all
      // non-rib geometry, including other new spars and webs.
      const bool intendedWebContact =
          (isShearWebPart(name) && isSparMember(other.name)) ||
          (isShearWebPart(other.name) && isSparMember(name));
      const bool sameJoinerDefinition =
          isCheckedJoiner(name) && isCheckedJoiner(other.name) &&
          joinerDefinitionName(name) == joinerDefinitionName(other.name);
      const bool woodJoinerCollision = name != other.name &&
          isWoodJoiner(name) && isWoodJoiner(other.name);
      const bool joinerCollision = woodJoinerCollision ||
          (!sameJoinerDefinition &&
           ((isCheckedJoiner(name) && isJoinerCollisionTarget(other.name)) ||
            (isCheckedJoiner(other.name) && isJoinerCollisionTarget(name))));
      const bool newSparCollision = isNewSparPart(name) || isNewSparPart(other.name);
      const bool spoilerCollision =
          (isSpoilerPart(name) || isSpoilerPart(other.name)) &&
          !(isSpoilerPart(name) && isSpoilerPart(other.name));
      if (isSheetingPart(name) || isSheetingPart(other.name) ||
          ((isWoodJoiner(name) || isWoodJoiner(other.name)) &&
           !woodJoinerCollision && !spoilerCollision) ||
          intendedWebContact ||
          (!newSparCollision && !joinerCollision && !spoilerCollision) ||
          other.name == name || bounds.IsOut(other.bounds))
        continue;
      BRepAlgoAPI_Common common;
      try {
        TopTools_ListOfShape arguments;
        arguments.Append(shape);
        common.SetArguments(arguments);
        TopTools_ListOfShape tools;
        tools.Append(other.shape);
        common.SetTools(tools);
        common.SetRunParallel(true);
        common.Build();
      } catch (const Standard_Failure&) {
        if (spoilerCollision) {
          const auto& spoilerName = isSpoilerPart(name) ? name : other.name;
          const auto& obstructionName = isSpoilerPart(name) ? other.name : name;
          throw std::runtime_error(
              "Unable to check clearance between " + spoilerName + " and " +
              (obstructionName.empty()
                  ? std::string{"the adjoining wing part"}
                  : obstructionName) +
              ". Adjust the spoiler position or dimensions and try again.");
        }
        throw;
      }
      if (!common.IsDone())
        throw std::runtime_error(spoilerCollision
            ? "Unable to check spoiler clearance between " + name + " and " +
                other.name + ". Adjust the spoiler position or dimensions and try again."
            : "Unable to check collision between " + name + " and " + other.name);
      GProp_GProps properties;
      BRepGProp::VolumeProperties(common.Shape(), properties);
      // Boolean commons at intentionally shared faces can contain microscopic
      // tolerance slivers. Ignore sub-cubic-millimetre artifacts while still
      // rejecting any manufacturable solid overlap.
      if (properties.Mass() > 1.0e-3) {
        if (spoilerCollision) {
          const auto& spoilerName = isSpoilerPart(name) ? name : other.name;
          const auto& obstructionName = isSpoilerPart(name) ? other.name : name;
          const std::string obstruction = obstructionName.empty()
              ? "an adjoining wing part" : obstructionName;
          const std::string category = isSparMember(obstructionName) ||
                  obstructionName == "CF tube" || obstructionName == "CF rod"
              ? "spar" : isTrailingEdgePart(obstructionName)
              ? "trailing edge" : "wing part";
          throw std::invalid_argument(
              "Spoiler collision: " + spoilerName + " intersects " +
              obstruction + ". Adjust the spoiler position or dimensions so " +
              "it clears the " + category + ".");
        }
        if (joinerCollision)
          throw std::invalid_argument(
              "Joiner collision: " + name + " intersects " + other.name +
              ". Adjust the joiner positions or dimensions so they do not overlap.");
        throw std::invalid_argument("Geometric collision between " + name +
                                    " and " + other.name);
      }
    }
    nonRibShapes.push_back({name, shape, bounds});
    addShape(shape, material, mirrorInAssembly, name);
  };
  const auto materialForName = [](const std::string& name) {
    if (name.find("Fiberglass") != std::string::npos) return PartMaterial::Fiberglass;
    if (name.find("Steel") != std::string::npos) return PartMaterial::Steel;
    if (name.find("Aluminum") != std::string::npos) return PartMaterial::Aluminum;
    if (name.find("CF") != std::string::npos) return PartMaterial::CarbonFiber;
    return PartMaterial::Wood;
  };

  const auto elapsedMs = [](const std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
  };
  auto stageStart = std::chrono::steady_clock::now();
  std::vector<const domain::StructuredRib*> ribsToBuild;
  ribsToBuild.reserve(
      structuredWing.ribs.size() + structuredWing.riblets.size());
  for (const auto& rib : structuredWing.ribs)
    ribsToBuild.push_back(&rib);
  for (const auto& riblet : structuredWing.riblets)
    ribsToBuild.push_back(&riblet);
  const bool cuttingLighteningHoles = std::any_of(
      ribsToBuild.begin(), ribsToBuild.end(),
      [](const domain::StructuredRib* rib) {
        return std::any_of(
            rib->internalCutouts.begin(), rib->internalCutouts.end(),
            [](const auto& opening) { return opening.size() >= 24; });
      });
  const std::string ribStageMessage = cuttingLighteningHoles
      ? "Building Rib Solids and Cutting Lightening Holes"
      : "Building Rib Solids";
  if (progress)
    progress(0, ribStageMessage);

  struct BuiltRibShape {
    TopoDS_Shape shape;
    std::string name;
    bool mirrorInAssembly{true};
  };
  std::vector<std::vector<BuiltRibShape>> builtRibs(
      ribsToBuild.size());
  const auto buildRib = [&](const std::size_t structuredIndex) {
    const auto& structured = *ribsToBuild[structuredIndex];
    const auto outlineSegments = structured.outlineSegments.empty()
        ? domain::makeRibOutlineSegments(structured.outerOutline)
        : structured.outlineSegments;
    BRepBuilderAPI_MakeWire outer;
    for (const auto& segment : outlineSegments) {
      if (segment.spline && segment.points.size() >= 3) {
        const auto points =
            Handle(OcctPointArray){
                new OcctPointArray{
                    1, static_cast<int>(segment.points.size())}};
        for (std::size_t point = 0; point < segment.points.size(); ++point)
          points->SetValue(static_cast<int>(point + 1),
              transformLocal(structured.rib, segment.points[point],
                  ribStartOffset(structured.rib, ribThickness)));
        GeomAPI_Interpolate interpolation{
            points, false, Precision::Confusion()};
        interpolation.Perform();
        outer.Add(BRepBuilderAPI_MakeEdge{interpolation.Curve()}.Edge());
      } else {
        for (std::size_t point = 0; point + 1 < segment.points.size(); ++point)
          outer.Add(BRepBuilderAPI_MakeEdge{
              transformLocal(structured.rib, segment.points[point],
                  ribStartOffset(structured.rib, ribThickness)),
              transformLocal(structured.rib, segment.points[point + 1],
                  ribStartOffset(structured.rib, ribThickness))}.Edge());
      }
    }
    if (!outer.IsDone()) throw std::runtime_error("Unable to construct spline rib outline");
    const double planeAngle = structured.rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const gp_Vec ribNormal{0.0, std::cos(planeAngle), std::sin(planeAngle)};
    const bool centerRoot = std::abs(structured.rib.spanPosition) < 1.0e-9 &&
        std::abs(structured.rib.ribThicknessStartFactor) < 1.0e-9;
    const gp_Pln plane{transformLocal(structured.rib, structured.outerOutline.front(), ribStartOffset(structured.rib, ribThickness)),
                       centerRoot ? gp_Dir{0.0, 1.0, 0.0} : gp_Dir{ribNormal}};
    BRepBuilderAPI_MakeFace face{plane, outer.Wire(), true};
    if (!face.IsDone() || !BRepCheck_Analyzer{face.Face(), false}.IsValid())
      throw std::runtime_error(
          "Unable to fill spline outer outline for structured rib " +
          std::to_string(structuredIndex + 1) + "; segments=" +
          std::to_string(outlineSegments.size()));
    for (const auto& hole : structured.holes) {
      BRepBuilderAPI_MakePolygon polygon;
      for (const auto& point : hole)
        polygon.Add(transformLocal(structured.rib, point, ribStartOffset(structured.rib, ribThickness)));
      polygon.Close();
      auto wire = polygon.Wire();
      wire.Reverse();
      face.Add(wire);
    }
    if (!face.IsDone()) throw std::runtime_error("Unable to fill notched rib outline");
    BRepPrimAPI_MakePrism prism{face.Face(), ribNormal * ribThickness};
    if (!prism.IsDone()) throw std::runtime_error("Unable to extrude notched rib");
    auto ribSolid = prism.Shape();
    if (ribSolid.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer{ribSolid, false}.IsValid()) {
      std::ostringstream detail;
      detail << "Structured rib " << structuredIndex + 1
             << " recessed extrusion is invalid before Boolean cuts; holes="
             << structured.holes.size();
      for (std::size_t holeIndex = 0; holeIndex < structured.holes.size(); ++holeIndex) {
        const auto& hole = structured.holes[holeIndex];
        domain::Point2 center{};
        for (const auto& point : hole) { center.x += point.x; center.y += point.y; }
        center.x /= static_cast<double>(hole.size());
        center.y /= static_cast<double>(hole.size());
        double radius = 0.0;
        for (const auto& point : hole)
          radius = std::max(radius, std::hypot(point.x - center.x, point.y - center.y));
        detail << " [" << holeIndex + 1 << ":x=" << center.x << ",y=" << center.y
               << ",r=" << radius << "]";
      }
      throw std::runtime_error(detail.str());
    }
    const auto finishRib = [&](const std::vector<std::vector<domain::Point2>>&
                                   halfSpecificHoles) {
      auto finished = ribSolid;
      const auto cutCircularHoles =
          [&](const std::vector<std::vector<domain::Point2>>& holes) {
        for (const auto& hole : holes) {
          domain::Point2 center{};
          for (const auto& point : hole) {
            center.x += point.x;
            center.y += point.y;
          }
          center.x /= static_cast<double>(hole.size());
          center.y /= static_cast<double>(hole.size());
          double radius = 0.0;
          for (const auto& point : hole)
            radius = std::max(
                radius, std::hypot(point.x - center.x, point.y - center.y));
          const auto start = transformLocal(
              structured.rib, center,
              ribStartOffset(structured.rib, ribThickness) - 1.0);
          BRepPrimAPI_MakeCylinder holeTool{
              gp_Ax2{start, gp_Dir{ribNormal}}, radius, ribThickness + 2.0};
          BRepAlgoAPI_Cut cut{finished, holeTool.Shape()};
          cut.Build();
          if (!cut.IsDone())
            throw std::runtime_error("Unable to cut a circular rib opening");
          finished = cut.Shape();
          TopExp_Explorer cutSolids{finished, TopAbs_SOLID};
          if (!cutSolids.More())
            throw std::runtime_error("A circular opening removed the rib solid");
          if (!BRepCheck_Analyzer{finished, false}.IsValid()) {
            ShapeFix_Shape fixer{finished};
            fixer.Perform();
            finished = fixer.Shape();
          }
        }
      };
      cutCircularHoles(structured.booleanHoles);
      cutCircularHoles(halfSpecificHoles);

      // Make all internal openings while the rib is still one solid. The wood
      // joiner through-slot is applied last because it intentionally separates
      // a joint rib into two independently retained solids.
      for (const auto& opening : structured.internalCutouts) {
        BRepBuilderAPI_MakePolygon cutPolygon;
        for (const auto& point : opening)
          cutPolygon.Add(transformLocal(
              structured.rib, point,
              ribStartOffset(structured.rib, ribThickness) - 1.0));
        cutPolygon.Close();
        BRepBuilderAPI_MakeFace cutFace{cutPolygon.Wire()};
        BRepPrimAPI_MakePrism cutTool{
            cutFace.Face(), ribNormal * (ribThickness + 2.0)};
        BRepAlgoAPI_Cut cut{finished, cutTool.Shape()};
        cut.Build();
        if (!cut.IsDone())
          throw std::runtime_error("Unable to cut an internal rib opening");
        finished = cut.Shape();
        TopExp_Explorer cutSolids{finished, TopAbs_SOLID};
        if (!cutSolids.More())
          throw std::runtime_error("An internal opening removed the rib solid");
        if (!BRepCheck_Analyzer{finished, false}.IsValid()) {
          ShapeFix_Shape fixer{finished};
          fixer.Perform();
          finished = fixer.Shape();
        }
      }
      for (const auto& cutout : structured.booleanCutouts) {
        BRepBuilderAPI_MakePolygon cutPolygon;
        for (const auto& point : cutout)
          cutPolygon.Add(transformLocal(
              structured.rib, point,
              ribStartOffset(structured.rib, ribThickness) - 1.0));
        cutPolygon.Close();
        BRepBuilderAPI_MakeFace cutFace{cutPolygon.Wire()};
        BRepPrimAPI_MakePrism cutTool{
            cutFace.Face(), ribNormal * (ribThickness + 2.0)};
        BRepAlgoAPI_Cut cut{finished, cutTool.Shape()};
        cut.Build();
        if (!cut.IsDone())
          throw std::runtime_error("Unable to cut the wood joiner slot");
        finished = cut.Shape();
        TopExp_Explorer cutSolids{finished, TopAbs_SOLID};
        if (!cutSolids.More())
          throw std::runtime_error("Wood joiner cut removed the rib solid");
        if (!BRepCheck_Analyzer{finished, false}.IsValid()) {
          ShapeFix_Shape fixer{finished};
          fixer.Perform();
          finished = fixer.Shape();
        }
      }

      std::vector<TopoDS_Shape> resultingSolids;
      for (TopExp_Explorer solids{finished, TopAbs_SOLID}; solids.More();
           solids.Next())
        resultingSolids.push_back(solids.Current());
      if (resultingSolids.empty())
        throw std::runtime_error(
            "Structured rib " + std::to_string(structuredIndex + 1) +
            " extrusion did not produce a valid capped solid (0 result solids)");
      BRep_Builder ribBuilder;
      TopoDS_Compound ribPart;
      if (resultingSolids.size() > 1) ribBuilder.MakeCompound(ribPart);
      for (auto& solid : resultingSolids) {
        if (!BRepCheck_Analyzer{solid, false}.IsValid())
          throw std::runtime_error(
              "Structured rib " + std::to_string(structuredIndex + 1) +
              " contains an invalid solid after Boolean cuts");
        if (resultingSolids.size() > 1) ribBuilder.Add(ribPart, solid);
      }
      return resultingSolids.size() == 1
          ? resultingSolids.front() : TopoDS_Shape{ribPart};
    };

    // Some valid spline-bounded planar cap faces are rejected by OCCT's
    // mesher even though their adjoining side faces triangulate correctly.
    // Supply those faces with a polygonal triangulation derived from their
    // final boundary wires so Boolean holes and split joiner ribs are retained.
    const auto ensureRibCapTriangulation = [&](TopoDS_Shape& ribShape) {
      BRep_Builder triangulationBuilder;
      for (TopExp_Explorer faces{ribShape, TopAbs_FACE};
           faces.More(); faces.Next()) {
        const auto capFace = TopoDS::Face(faces.Current());
        const BRepAdaptor_Surface surface{capFace};
        if (surface.GetType() != GeomAbs_Plane ||
            std::abs(surface.Plane().Axis().Direction().Dot(gp_Dir{ribNormal})) <
                0.99)
          continue;
        TopLoc_Location existingLocation;
        if (!BRep_Tool::Triangulation(capFace, existingLocation).IsNull())
          continue;
        // Boolean results can leave a valid planar cap unmeshed when OCCT
        // processes the complete rib solid. Meshing an isolated copy avoids
        // that coupled-face failure while preserving every exact boundary and
        // hole. The copied face uses the same geometry/location, so its mesh
        // can be attached directly to the original cap.
        BRepBuilderAPI_Copy isolatedCopy{capFace};
        auto isolatedFace = TopoDS::Face(isolatedCopy.Shape());
        BRepTools::Clean(isolatedFace);
        BRepMesh_IncrementalMesh isolatedMesh{
            isolatedFace, 0.5, false, 0.25, false};
        TopLoc_Location isolatedLocation;
        const auto isolatedTriangulation =
            BRep_Tool::Triangulation(isolatedFace, isolatedLocation);
        if (isolatedMesh.IsDone() && !isolatedTriangulation.IsNull()) {
          triangulationBuilder.UpdateFace(capFace, isolatedTriangulation);
          continue;
        }
        struct SampledWire {
          TopoDS_Wire wire;
          std::vector<gp_Pnt> points;
        };
        const auto polygonalWire = [&](const TopoDS_Wire& source) {
          BRepBuilderAPI_MakePolygon polygon;
          std::vector<gp_Pnt> polygonPoints;
          std::optional<gp_Pnt> previous;
          const auto append = [&](const gp_Pnt& point) {
            if (!previous || previous->Distance(point) > Precision::Confusion()) {
              polygon.Add(point);
              polygonPoints.push_back(point);
              previous = point;
            }
          };
          for (BRepTools_WireExplorer edges{source, capFace};
               edges.More(); edges.Next()) {
            const auto edge = edges.Current();
            BRepAdaptor_Curve curve{edge};
            GCPnts_QuasiUniformDeflection samples{curve, 0.05};
            std::vector<gp_Pnt> edgePoints;
            if (!samples.IsDone() || samples.NbPoints() < 2) {
              edgePoints = {curve.Value(curve.FirstParameter()),
                            curve.Value(curve.LastParameter())};
            } else {
              for (int index = 1; index <= samples.NbPoints(); ++index)
                edgePoints.push_back(samples.Value(index));
            }
            const bool reverse = previous
                ? previous->Distance(edgePoints.back()) <
                      previous->Distance(edgePoints.front())
                : edge.Orientation() == TopAbs_REVERSED;
            if (reverse)
              for (auto point = edgePoints.rbegin();
                   point != edgePoints.rend(); ++point)
                append(*point);
            else
              for (const auto& point : edgePoints) append(point);
          }
          polygon.Close();
          if (!polygon.IsDone())
            throw std::runtime_error(
                "Unable to construct a polygonal structured-rib cap wire");
          return SampledWire{polygon.Wire(), std::move(polygonPoints)};
        };
        std::size_t earFailureRemaining = 0;
        double earFailureMinimumCross = 0.0;
        std::string earFailureCoordinates;
        const auto simpleTriangulation =
            [&](std::vector<gp_Pnt> nodes,
               const std::vector<gp_Pnt>& bridgeHolePoints = {}) {
          if (nodes.size() > 1 &&
              nodes.front().Distance(nodes.back()) < Precision::Confusion())
            nodes.pop_back();
          const auto axes = surface.Plane().Position();
          struct Point2d { double x{}; double y{}; };
          const auto project = [&](const gp_Pnt& point) {
            const gp_Vec offset{axes.Location(), point};
            return Point2d{offset.Dot(gp_Vec{axes.XDirection()}),
                           offset.Dot(gp_Vec{axes.YDirection()})};
          };
          std::vector<Point2d> flat;
          flat.reserve(nodes.size());
          for (const auto& point : nodes) flat.push_back(project(point));
          const auto cross = [](const Point2d a, const Point2d b,
                                const Point2d c) {
            return (b.x - a.x) * (c.y - a.y) -
                   (b.y - a.y) * (c.x - a.x);
          };
          bool removed = true;
          while (removed && nodes.size() > 3) {
            removed = false;
            for (std::size_t index = 0; index < nodes.size(); ++index) {
              const std::size_t before =
                  (index + nodes.size() - 1) % nodes.size();
              const std::size_t after = (index + 1) % nodes.size();
              if (std::abs(cross(flat[before], flat[index], flat[after])) >
                  1.0e-9)
                continue;
              bool bridgeEndpoint = false;
              for (std::size_t other = 0; other < nodes.size(); ++other) {
                if (other == index || other == before || other == after)
                  continue;
                if (nodes[index].Distance(nodes[other]) <
                    Precision::Confusion()) {
                  bridgeEndpoint = true;
                  break;
                }
              }
              if (bridgeEndpoint) continue;
              nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
              flat.erase(flat.begin() + static_cast<std::ptrdiff_t>(index));
              removed = true;
              break;
            }
          }
          if (nodes.size() < 3)
            return Handle(Poly_Triangulation){};
          double signedArea = 0.0;
          for (std::size_t index = 0; index < flat.size(); ++index) {
            const auto& next = flat[(index + 1) % flat.size()];
            signedArea += flat[index].x * next.y - next.x * flat[index].y;
          }
          double orientation = signedArea >= 0.0 ? 1.0 : -1.0;
          std::vector<int> remaining(nodes.size());
          std::iota(remaining.begin(), remaining.end(), 0);
          std::vector<std::array<int, 3>> triangles;
          triangles.reserve(nodes.size() - 2);
          while (remaining.size() > 3) {
            bool clipped = false;
            for (std::size_t position = 0; position < remaining.size(); ++position) {
              const int before = remaining[(position + remaining.size() - 1) %
                                           remaining.size()];
              const int current = remaining[position];
              const int after = remaining[(position + 1) % remaining.size()];
              if (orientation * cross(flat[before], flat[current], flat[after]) <=
                  1.0e-10)
                continue;
              bool containsPoint = false;
              for (const int candidate : remaining) {
                if (candidate == before || candidate == current ||
                    candidate == after)
                  continue;
                if (orientation * cross(flat[before], flat[current],
                                        flat[candidate]) > 1.0e-9 &&
                    orientation * cross(flat[current], flat[after],
                                        flat[candidate]) > 1.0e-9 &&
                    orientation * cross(flat[after], flat[before],
                                        flat[candidate]) > 1.0e-9) {
                  containsPoint = true;
                  break;
                }
              }
              if (containsPoint) continue;
              triangles.push_back({before, current, after});
              remaining.erase(remaining.begin() +
                              static_cast<std::ptrdiff_t>(position));
              clipped = true;
              break;
            }
            if (!clipped) {
              for (std::size_t position = 0;
                   position < remaining.size(); ++position) {
                const int before = remaining[
                    (position + remaining.size() - 1) % remaining.size()];
                const int current = remaining[position];
                const int after =
                    remaining[(position + 1) % remaining.size()];
                if (std::abs(cross(flat[before], flat[current], flat[after])) >
                    1.0e-7)
                  continue;
                remaining.erase(remaining.begin() +
                                static_cast<std::ptrdiff_t>(position));
                clipped = true;
                break;
              }
              if (!clipped) {
                // A bridged hole is represented by two occurrences of each
                // bridge endpoint. Once the surrounding ears are removed,
                // discard a repeated index rather than treating the
                // zero-width bridge as real cap area.
                for (std::size_t first = 0;
                     first < remaining.size() && !clipped; ++first) {
                  for (std::size_t second = first + 1;
                       second < remaining.size(); ++second) {
                    if (nodes[remaining[first]].Distance(
                            nodes[remaining[second]]) >= Precision::Confusion())
                      continue;
                    remaining.erase(remaining.begin() +
                                    static_cast<std::ptrdiff_t>(second));
                    clipped = true;
                    break;
                  }
                }
              }
              if (!clipped) {
                // After all hole arcs have been consumed, one copy of the
                // hole-side bridge endpoint can remain in the weak polygon.
                // It bounds no cap area and may be dropped safely.
                for (std::size_t position = 0;
                     position < remaining.size() && !clipped; ++position) {
                  for (const auto& bridgePoint : bridgeHolePoints) {
                    if (nodes[remaining[position]].Distance(bridgePoint) >=
                        Precision::Confusion())
                      continue;
                    remaining.erase(remaining.begin() +
                                    static_cast<std::ptrdiff_t>(position));
                    clipped = true;
                    break;
                  }
                }
              }
              if (!clipped) {
                double remainingArea = 0.0;
                for (std::size_t position = 0;
                     position < remaining.size(); ++position) {
                  const auto& current = flat[remaining[position]];
                  const auto& next = flat[remaining[
                      (position + 1) % remaining.size()]];
                  remainingArea += current.x * next.y - next.x * current.y;
                }
                const double remainingOrientation =
                    remainingArea >= 0.0 ? 1.0 : -1.0;
                if (remainingOrientation != orientation) {
                  orientation = remainingOrientation;
                  clipped = true;
                }
              }
              if (!clipped) {
                double minimumCross =
                    std::numeric_limits<double>::infinity();
                std::size_t minimumPosition = 0;
                for (std::size_t position = 0;
                     position < remaining.size(); ++position) {
                  const int before = remaining[
                      (position + remaining.size() - 1) % remaining.size()];
                  const int current = remaining[position];
                  const int after =
                      remaining[(position + 1) % remaining.size()];
                  const double value =
                      std::abs(cross(flat[before], flat[current], flat[after]));
                  if (value < minimumCross) {
                    minimumCross = value;
                    minimumPosition = position;
                  }
                }
                // A sampled spline can leave a tiny kink after most ears have
                // been clipped. Removing a display sliver of at most 5 mm^2
                // lets the remaining cap triangulate without changing the
                // underlying solid or exported geometry.
                if (minimumCross <= 10.0) {
                  remaining.erase(remaining.begin() +
                      static_cast<std::ptrdiff_t>(minimumPosition));
                  clipped = true;
                } else {
                  earFailureRemaining = remaining.size();
                  earFailureMinimumCross = minimumCross;
                  std::ostringstream coordinates;
                  for (const int node : remaining)
                    coordinates << " [" << flat[node].x << ','
                                << flat[node].y << ']';
                  earFailureCoordinates = coordinates.str();
                  return Handle(Poly_Triangulation){};
                }
              }
            }
          }
          triangles.push_back(
              {remaining[0], remaining[1], remaining[2]});
          Handle(Poly_Triangulation) triangulation =
              new Poly_Triangulation(
                  static_cast<int>(nodes.size()),
                  static_cast<int>(triangles.size()), false);
          for (std::size_t index = 0; index < nodes.size(); ++index)
            triangulation->SetNode(static_cast<int>(index + 1), nodes[index]);
          for (std::size_t index = 0; index < triangles.size(); ++index)
            triangulation->SetTriangle(static_cast<int>(index + 1),
                Poly_Triangle{triangles[index][0] + 1,
                              triangles[index][1] + 1,
                              triangles[index][2] + 1});
          triangulation->Deflection(0.25);
          return triangulation;
        };
        const auto triangulationWithHoles = [&simpleTriangulation, &surface](
            std::vector<gp_Pnt> outer,
            std::vector<std::vector<gp_Pnt>> holes) {
          const auto axes = surface.Plane().Position();
          struct FlatPoint { double x{}; double y{}; };
          const auto project = [&](const gp_Pnt& point) {
            const gp_Vec offset{axes.Location(), point};
            return FlatPoint{offset.Dot(gp_Vec{axes.XDirection()}),
                             offset.Dot(gp_Vec{axes.YDirection()})};
          };
          const auto removeClosingDuplicate = [](std::vector<gp_Pnt>& points) {
            if (points.size() > 1 &&
                points.front().Distance(points.back()) < Precision::Confusion())
              points.pop_back();
          };
          removeClosingDuplicate(outer);
          for (auto& hole : holes) removeClosingDuplicate(hole);
          const auto signedArea = [&](const std::vector<gp_Pnt>& points) {
            double area = 0.0;
            for (std::size_t index = 0; index < points.size(); ++index) {
              const auto current = project(points[index]);
              const auto next = project(points[(index + 1) % points.size()]);
              area += current.x * next.y - next.x * current.y;
            }
            return area * 0.5;
          };
          const auto orientation = [](const FlatPoint a, const FlatPoint b,
                                      const FlatPoint c) {
            return (b.x - a.x) * (c.y - a.y) -
                   (b.y - a.y) * (c.x - a.x);
          };
          const auto properIntersection = [&](const FlatPoint a,
                                              const FlatPoint b,
                                              const FlatPoint c,
                                              const FlatPoint d) {
            constexpr double tolerance = 1.0e-9;
            const double abC = orientation(a, b, c);
            const double abD = orientation(a, b, d);
            const double cdA = orientation(c, d, a);
            const double cdB = orientation(c, d, b);
            return ((abC > tolerance && abD < -tolerance) ||
                    (abC < -tolerance && abD > tolerance)) &&
                   ((cdA > tolerance && cdB < -tolerance) ||
                    (cdA < -tolerance && cdB > tolerance));
          };
          const auto pointInside = [&](const FlatPoint point,
                                       const std::vector<gp_Pnt>& polygon) {
            bool inside = false;
            for (std::size_t index = 0, previous = polygon.size() - 1;
                 index < polygon.size(); previous = index++) {
              const auto a = project(polygon[index]);
              const auto b = project(polygon[previous]);
              if ((a.y > point.y) == (b.y > point.y)) continue;
              const double crossingX = (b.x - a.x) * (point.y - a.y) /
                  (b.y - a.y) + a.x;
              if (point.x < crossingX) inside = !inside;
            }
            return inside;
          };
          if (outer.size() < 3) return Handle(Poly_Triangulation){};
          const std::vector<gp_Pnt> originalOuter = outer;
          const double outerArea = signedArea(outer);
          std::vector<gp_Pnt> bridgeHolePoints;
          for (auto& hole : holes) {
            if (hole.size() < 3) continue;
            if ((signedArea(hole) >= 0.0) == (outerArea >= 0.0))
              std::reverse(hole.begin(), hole.end());
            bridgeHolePoints.insert(
                bridgeHolePoints.end(), hole.begin(), hole.end());
            std::size_t holeVertex = 0;
            for (std::size_t index = 1; index < hole.size(); ++index) {
              const auto candidate = project(hole[index]);
              const auto selected = project(hole[holeVertex]);
              if (candidate.x > selected.x ||
                  (std::abs(candidate.x - selected.x) < 1.0e-9 &&
                   candidate.y < selected.y))
                holeVertex = index;
            }
            const auto holePoint = project(hole[holeVertex]);
            std::size_t bridgeVertex = outer.size();
            double bridgeDistance = std::numeric_limits<double>::infinity();
            for (std::size_t candidateIndex = 0;
                 candidateIndex < outer.size(); ++candidateIndex) {
              const auto candidate = project(outer[candidateIndex]);
              bool blocked = false;
              for (std::size_t edge = 0; edge < outer.size(); ++edge) {
                const std::size_t next = (edge + 1) % outer.size();
                if (edge == candidateIndex || next == candidateIndex) continue;
                if (properIntersection(holePoint, candidate,
                                       project(outer[edge]),
                                       project(outer[next]))) {
                  blocked = true;
                  break;
                }
              }
              if (blocked) continue;
              for (std::size_t edge = 0; edge < hole.size(); ++edge) {
                const std::size_t next = (edge + 1) % hole.size();
                if (edge == holeVertex || next == holeVertex) continue;
                if (properIntersection(holePoint, candidate,
                                       project(hole[edge]),
                                       project(hole[next]))) {
                  blocked = true;
                  break;
                }
              }
              if (blocked) continue;
              for (int sample = 1; sample < 10 && !blocked; ++sample) {
                const double t = static_cast<double>(sample) / 10.0;
                const FlatPoint point{
                    holePoint.x + (candidate.x - holePoint.x) * t,
                    holePoint.y + (candidate.y - holePoint.y) * t};
                if (!pointInside(point, originalOuter)) {
                  blocked = true;
                  break;
                }
                for (const auto& otherHole : holes) {
                  if (pointInside(point, otherHole)) {
                    blocked = true;
                    break;
                  }
                }
              }
              if (blocked) continue;
              const double distance = std::hypot(
                  candidate.x - holePoint.x, candidate.y - holePoint.y);
              if (distance < bridgeDistance) {
                bridgeDistance = distance;
                bridgeVertex = candidateIndex;
              }
            }
            if (bridgeVertex == outer.size())
              return Handle(Poly_Triangulation){};
            std::vector<gp_Pnt> merged;
            merged.reserve(outer.size() + hole.size() + 2);
            merged.insert(merged.end(), outer.begin(),
                          outer.begin() + static_cast<std::ptrdiff_t>(bridgeVertex + 1));
            merged.push_back(hole[holeVertex]);
            for (std::size_t offset = 1; offset < hole.size(); ++offset)
              merged.push_back(hole[(holeVertex + offset) % hole.size()]);
            merged.push_back(hole[holeVertex]);
            merged.push_back(outer[bridgeVertex]);
            merged.insert(merged.end(),
                          outer.begin() + static_cast<std::ptrdiff_t>(bridgeVertex + 1),
                          outer.end());
            outer = std::move(merged);
          }
          return simpleTriangulation(
              std::move(outer), bridgeHolePoints);
        };
        const auto outerWire = BRepTools::OuterWire(capFace);
        const auto sampledOuter = polygonalWire(outerWire);
        std::size_t innerWireCount = 0;
        std::vector<std::vector<gp_Pnt>> sampledInnerWires;
        for (TopExp_Explorer wires{capFace, TopAbs_WIRE};
             wires.More(); wires.Next()) {
          const auto wire = TopoDS::Wire(wires.Current());
          if (!wire.IsSame(outerWire)) {
            sampledInnerWires.push_back(polygonalWire(wire).points);
            ++innerWireCount;
          }
        }
        const auto planeAxes = surface.Plane().Position();
        Handle(Geom_Plane) fallbackSurface = new Geom_Plane(surface.Plane());
        const auto parametricWire = [&](std::vector<gp_Pnt> points) {
          if (points.size() > 1 &&
              points.front().Distance(points.back()) < Precision::Confusion())
            points.pop_back();
          BRepBuilderAPI_MakeWire wireBuilder;
          for (std::size_t index = 0; index < points.size(); ++index) {
            const auto parameterPoint = [&](const gp_Pnt& point) {
              const gp_Vec offset{planeAxes.Location(), point};
              return gp_Pnt2d{
                  offset.Dot(gp_Vec{planeAxes.XDirection()}),
                  offset.Dot(gp_Vec{planeAxes.YDirection()})};
            };
            const auto first = parameterPoint(points[index]);
            const auto second = parameterPoint(
                points[(index + 1) % points.size()]);
            if (first.Distance(second) < Precision::PConfusion()) continue;
            const auto pcurve = GC_MakeSegment2d{first, second}.Value();
            BRepBuilderAPI_MakeEdge edgeBuilder{
                points[index], points[(index + 1) % points.size()]};
            if (!edgeBuilder.IsDone())
              throw std::runtime_error(
                  "Unable to construct a parametric rib-cap edge");
            auto edge = edgeBuilder.Edge();
            BRep_Builder edgeBuilderWithPcurve;
            edgeBuilderWithPcurve.UpdateEdge(
                edge, pcurve, fallbackSurface, TopLoc_Location{},
                Precision::Confusion());
            wireBuilder.Add(edge);
          }
          if (!wireBuilder.IsDone())
            throw std::runtime_error(
                "Unable to construct a parametric rib-cap wire");
          return wireBuilder.Wire();
        };
        const auto parametricOuter = parametricWire(sampledOuter.points);
        BRepBuilderAPI_MakeFace outerOnly{
            fallbackSurface, parametricOuter, true};
        const bool outerOnlyValid = outerOnly.IsDone() &&
            BRepCheck_Analyzer{outerOnly.Face(), false}.IsValid();
        std::vector<TopoDS_Wire> parametricInnerWires;
        for (const auto& inner : sampledInnerWires)
          parametricInnerWires.push_back(parametricWire(inner));
        const auto makeFallbackFace = [&](const TopAbs_Orientation orientation) {
          BRepBuilderAPI_MakeFace faceBuilder{
              fallbackSurface, parametricOuter, true};
          for (auto innerWire : parametricInnerWires) {
            innerWire.Orientation(orientation);
            faceBuilder.Add(innerWire);
          }
          return faceBuilder.Face();
        };
        auto fallbackFace = makeFallbackFace(TopAbs_FORWARD);
        if (!BRepCheck_Analyzer{fallbackFace, false}.IsValid())
          fallbackFace = makeFallbackFace(TopAbs_REVERSED);
        ShapeFix_Face fallbackFix{fallbackFace};
        fallbackFix.FixWireTool()->FixAddPCurveMode() = 1;
        fallbackFix.FixOrientationMode() = 1;
        fallbackFix.Perform();
        fallbackFace = fallbackFix.Face();
        BRepMesh_IncrementalMesh fallbackMesh{
            fallbackFace, 0.75, false, 0.35, false};
        TopLoc_Location fallbackLocation;
        auto triangulation =
            BRep_Tool::Triangulation(fallbackFace, fallbackLocation);
        if (triangulation.IsNull() && innerWireCount == 0)
          triangulation = simpleTriangulation(sampledOuter.points);
        if (triangulation.IsNull() && innerWireCount > 0)
          triangulation = triangulationWithHoles(
              sampledOuter.points, sampledInnerWires);
        if (!fallbackMesh.IsDone() || triangulation.IsNull()) {
          GProp_GProps capProperties;
          BRepGProp::SurfaceProperties(capFace, capProperties);
          std::ostringstream detail;
          detail << "Unable to triangulate structured rib cap "
                 << structuredIndex + 1 << " (area="
                 << capProperties.Mass() << " mm^2, fallbackValid="
                 << BRepCheck_Analyzer{fallbackFace, false}.IsValid()
                 << ", outerOnlyValid=" << outerOnlyValid
                 << ", capOrientation=" << static_cast<int>(capFace.Orientation())
                 << ", outerOrientation=" << static_cast<int>(outerWire.Orientation())
                 << ", innerWires=" << innerWireCount
                 << ", sampledNodes=" << sampledOuter.points.size()
                 << ", earRemaining=" << earFailureRemaining
                 << ", minCross=" << earFailureMinimumCross
                 << ", remaining=" << earFailureCoordinates << ")";
          throw std::runtime_error(detail.str());
        }
        triangulationBuilder.UpdateFace(capFace, triangulation);
      }
    };

    auto positiveRibShape =
        finishRib(structured.positiveHalfBooleanHoles);
    BRepTools::Clean(positiveRibShape);
    BRepMesh_IncrementalMesh positiveRibMesh{
        positiveRibShape, 0.75, false, 0.35, true};
    if (!positiveRibMesh.IsDone())
      throw std::runtime_error(
          "Unable to create the shaded mesh for structured rib " +
          std::to_string(structuredIndex + 1));
    ensureRibCapTriangulation(positiveRibShape);
    const std::string ribName = structured.name.empty()
        ? "Rib " + std::to_string(structuredIndex + 1) : structured.name;
    const bool hasHalfSpecificHoles =
        structured.uniqueHalfPartVariants ||
        !structured.positiveHalfBooleanHoles.empty() ||
        !structured.negativeHalfBooleanHoles.empty();
    if (!hasHalfSpecificHoles) {
      builtRibs[structuredIndex].push_back(
          {positiveRibShape, ribName, true});
    } else {
      const std::string positiveName = structured.positiveHalfName.empty()
          ? ribName + " Right" : structured.positiveHalfName;
      const std::string negativeName = structured.negativeHalfName.empty()
          ? ribName + " Left" : structured.negativeHalfName;
      builtRibs[structuredIndex].push_back(
          {positiveRibShape, positiveName, false});
      gp_Trsf mirror;
      mirror.SetMirror(
          gp_Ax2{gp_Pnt{0.0, 0.0, 0.0}, gp_Dir{0.0, 1.0, 0.0}});
      auto negativeRibShape =
          finishRib(structured.negativeHalfBooleanHoles);
      BRepTools::Clean(negativeRibShape);
      BRepMesh_IncrementalMesh negativeRibMesh{
          negativeRibShape, 0.75, false, 0.35, true};
      if (!negativeRibMesh.IsDone())
        throw std::runtime_error(
            "Unable to create the shaded mesh for structured rib " +
            std::to_string(structuredIndex + 1) + " left variant");
      ensureRibCapTriangulation(negativeRibShape);
      builtRibs[structuredIndex].push_back(
          {BRepBuilderAPI_Transform{
               negativeRibShape, mirror, true, true}.Shape(),
           negativeName, false});
    }
  };
  const std::size_t ribWorkerCount = ribGeometryWorkerCount(
      ribsToBuild.size(), maximumRibWorkers);
  std::atomic_size_t nextRib{0};
  std::atomic_size_t completedRibs{0};
  std::vector<std::future<void>> ribWorkers;
  ribWorkers.reserve(ribWorkerCount);
  for (std::size_t worker = 0; worker < ribWorkerCount; ++worker)
    ribWorkers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t ribIndex = nextRib.fetch_add(1);
        if (ribIndex >= ribsToBuild.size()) return;
        buildRib(ribIndex);
        const std::size_t completed = ++completedRibs;
        if (progress)
          progress(2 + static_cast<int>(
              36 * completed / ribsToBuild.size()),
              ribStageMessage + " (" + std::to_string(completed) + "/" +
                  std::to_string(ribsToBuild.size()) + " complete)");
      }
    }));
  for (auto& worker : ribWorkers) worker.get();
  for (const auto& ribParts : builtRibs)
    for (const auto& ribPart : ribParts)
      addShape(ribPart.shape, PartMaterial::Wood,
               ribPart.mirrorInAssembly, ribPart.name);
  if (timings) timings->ribsMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(40, "Building leading and trailing edge stock");
  for (const auto& member : structuredWing.profiledMembers) {
    if (member.profiles.size() != structuredWing.ribs.size())
      throw std::runtime_error("Edge stock profiles do not match the rib stations");
    auto ranges = member.activeRanges;
    if (ranges.empty()) ranges.emplace_back(0, member.profiles.size() - 1);
    const bool splineTrailingEdge =
        member.name.starts_with("TE") ||
        member.name.find("trailing edge") != std::string::npos;
    for (const auto [first, last] : ranges) {
      BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
      loft.CheckCompatibility(false);
      const auto addProfile = [&](const std::size_t i, const double yOffset) {
        if (splineTrailingEdge) {
          const auto& profile = member.profiles[i];
          const auto trailing = std::max_element(
              profile.begin(), profile.end(),
              [](const domain::Point2& left, const domain::Point2& right) {
                return left.x < right.x;
              });
          const std::size_t trailingIndex = static_cast<std::size_t>(
              std::distance(profile.begin(), trailing));
          loft.AddWire(makeSplineProfileWire(
              structuredWing.ribs[i].rib, profile, trailingIndex,
              yOffset, "trailing-edge"));
          return;
        }
        const auto& profile = member.profiles[i];
        const auto leading = std::min_element(
            profile.begin(), profile.end(),
            [](const domain::Point2& left, const domain::Point2& right) {
              return left.x < right.x;
            });
        const std::size_t leadingIndex = static_cast<std::size_t>(
            std::distance(profile.begin(), leading));
        loft.AddWire(makeSplineProfileWire(
            structuredWing.ribs[i].rib, profile, leadingIndex,
            yOffset, "leading-edge"));
      };
      // LE/TE stock is straight over each uninterrupted range. Using profiles
      // at every rib split the ruled loft into thousands of small faces and
      // made both loft construction and final triangulation expensive. The
      // outside faces of the boundary ribs define the same continuous stock
      // with one straight ruled span between corresponding profile vertices.
      addProfile(first,
          ribStartOffset(structuredWing.ribs[first].rib, ribThickness));
      addProfile(last,
          ribEndOffset(structuredWing.ribs[last].rib, ribThickness));
      loft.Build();
      if (!loft.IsDone()) throw std::runtime_error("Unable to loft the panel edge stock");
      addPartShape(member.name, loft.Shape(), PartMaterial::Wood);
    }
  }
  if (timings) timings->profiledStockMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(50, "Building controls, spoilers, and rails");
  const domain::ControlSurfacePart* sharedFlap = nullptr;
  const domain::ControlSurfacePart* sharedAileron = nullptr;
  for (const auto& flap : structuredWing.controlSurfaces) {
    if (flap.name != "Flap") continue;
    for (const auto& aileron : structuredWing.controlSurfaces) {
      if (aileron.name == "Aileron" &&
          flap.stopRibIndex == aileron.startRibIndex) {
        sharedFlap = &flap;
        sharedAileron = &aileron;
      }
    }
  }
  for (const auto& control : structuredWing.controlSurfaces) {
    BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
    loft.CheckCompatibility(false);
    const auto addControlProfile = [&](const std::size_t localIndex, const double yOffset) {
      const std::size_t ribIndex = control.startRibIndex + localIndex;
      const auto& profile = control.profiles[localIndex];
      const auto trailing = std::max_element(
          profile.begin(), profile.end(),
          [](const domain::Point2& left, const domain::Point2& right) {
            return left.x < right.x;
          });
      const std::size_t trailingIndex = static_cast<std::size_t>(
          std::distance(profile.begin(), trailing));
      loft.AddWire(makeSplineProfileWire(
          structuredWing.ribs[ribIndex].rib, profile, trailingIndex,
          yOffset, "control-surface"));
    };
    addControlProfile(0,
        ribEndOffset(structuredWing.ribs[control.startRibIndex].rib,
                     ribThickness) + control.gap);
    const auto& stopRib = structuredWing.ribs[control.stopRibIndex].rib;
    addControlProfile(control.profiles.size() - 1,
        control.extendThroughStopRib
            ? ribEndOffset(stopRib, ribThickness)
            : ribStartOffset(stopRib, ribThickness) - control.gap);
    loft.Build();
    if (!loft.IsDone()) throw std::runtime_error("Unable to loft the control surface");
    addPartShape(control.name, loft.Shape(), PartMaterial::Wood);

    if (&control == sharedFlap || &control == sharedAileron) continue;
    const auto hingeStart = transformLocal(
        structuredWing.ribs[control.startRibIndex].rib,
        control.hingePostCenters.front(),
        ribEndOffset(structuredWing.ribs[control.startRibIndex].rib, ribThickness));
    const auto hingeEnd = transformLocal(
        structuredWing.ribs[control.stopRibIndex].rib,
        control.hingePostCenters.back(),
        control.extendThroughStopRib
            ? ribEndOffset(structuredWing.ribs[control.stopRibIndex].rib,
                           ribThickness)
            : ribStartOffset(structuredWing.ribs[control.stopRibIndex].rib,
                             ribThickness));
    addPartShape(control.name + " hinge post", makeRectangularSegment(
        hingeStart, hingeEnd, control.hingePostWidth, control.hingePostHeight),
        PartMaterial::Wood);
  }
  if (sharedFlap != nullptr && sharedAileron != nullptr) {
    const auto hingeStart = transformLocal(
        structuredWing.ribs[sharedFlap->startRibIndex].rib,
        sharedFlap->hingePostCenters.front(),
        ribEndOffset(structuredWing.ribs[sharedFlap->startRibIndex].rib,
                     ribThickness));
    const auto hingeEnd = transformLocal(
        structuredWing.ribs[sharedAileron->stopRibIndex].rib,
        sharedAileron->hingePostCenters.back(),
        sharedAileron->extendThroughStopRib
            ? ribEndOffset(
                  structuredWing.ribs[sharedAileron->stopRibIndex].rib,
                  ribThickness)
            : ribStartOffset(
                  structuredWing.ribs[sharedAileron->stopRibIndex].rib,
                  ribThickness));
    addPartShape("Flap/Aileron hinge post", makeRectangularSegment(
        hingeStart, hingeEnd, sharedFlap->hingePostWidth,
        sharedFlap->hingePostHeight), PartMaterial::Wood);
  }
  if (timings) timings->controlsMs = elapsedMs(stageStart);

  const auto buildMemberShape = [&](const domain::SpanMember& member) {
    const bool circular = member.kind == domain::SpanMemberKind::Tube ||
        member.kind == domain::SpanMemberKind::Rod;
    const bool endpointDefinedSpar = member.name.starts_with("Spar ");
    if (!circular && !endpointDefinedSpar)
      return makeRectangularSpanMember(structuredWing, member, ribThickness);
    const auto start = transformLocal(
        structuredWing.ribs.front().rib, member.centers.front());
    const auto end = transformLocal(
        structuredWing.ribs.back().rib, member.centers.back());
    gp_Pnt extendedStart = start;
    gp_Pnt extendedEnd = end;
    const gp_Vec axis{start, end};
    const gp_Vec extension = axis * (ribThickness * 0.5 / std::abs(axis.Y()));
    extendedStart.Translate(-extension);
    extendedEnd.Translate(extension);
    if (!circular)
      return makeRectangularSegment(
          extendedStart, extendedEnd, member.width, member.height);
    return makeTubeSegment(extendedStart, extendedEnd, member.width,
        member.kind == domain::SpanMemberKind::Tube ? member.innerDiameter : 0.0);
  };
  struct SpoilerShape { std::string name; TopoDS_Shape shape; bool mirror{true}; };
  std::vector<SpoilerShape> spoilerShapes;
  const auto loftSpoilerProfiles = [&](const domain::SpoilerPart& part,
      const std::vector<std::array<domain::Point2, 4>>& profiles,
      const double startExtra, const double endExtra) {
    BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
    loft.CheckCompatibility(false);
    const auto addProfile = [&](const std::size_t local, const double offset) {
      BRepBuilderAPI_MakePolygon polygon;
      const std::size_t ribIndex = part.startRibIndex + local;
      for (const auto& point : profiles[local])
        polygon.Add(transformLocal(structuredWing.ribs[ribIndex].rib, point, offset));
      polygon.Close();
      loft.AddWire(polygon.Wire());
    };
    addProfile(0, part.spansCenter ? 0.0 :
        ribEndOffset(structuredWing.ribs[part.startRibIndex].rib,
                     ribThickness) + startExtra);
    addProfile(profiles.size() - 1,
        ribStartOffset(structuredWing.ribs[part.endRibIndex].rib,
                       ribThickness) - endExtra);
    loft.Build();
    if (!loft.IsDone()) throw std::runtime_error("Unable to loft spoiler assembly part");
    return loft.Shape();
  };
  for (const auto& spoiler : structuredWing.spoilers) {
    const auto cutSpoilerLighteningHoles = [&](
        TopoDS_Shape shape,
        const std::vector<std::array<domain::Point2, 4>>& profiles,
        const double endGap) {
      if (spoiler.lighteningHoleOutlines.empty()) return shape;
      if (profiles.size() < 2 || spoiler.dxfOutline.size() < 2)
        throw std::runtime_error(
            "Unable to locate spoiler lightening holes");
      gp_Trsf mirror;
      mirror.SetMirror(
          gp_Ax2{gp_Pnt{0.0, 0.0, 0.0}, gp_Dir{0.0, 1.0, 0.0}});
      const std::size_t lastLocal = profiles.size() - 1;
      const auto topEdgePoint = [&](const std::size_t local,
                                    const double offset,
                                    const double chordFraction,
                                    const bool mirrored) {
        const auto& profile = profiles[local];
        const domain::Point2 localPoint{
            profile[3].x +
                chordFraction * (profile[2].x - profile[3].x),
            profile[3].y +
                chordFraction * (profile[2].y - profile[3].y)};
        auto modelPoint = transformLocal(
            structuredWing.ribs[spoiler.startRibIndex + local].rib,
            localPoint, offset);
        if (mirrored) modelPoint.Transform(mirror);
        return modelPoint;
      };
      const double rootOffset = spoiler.spansCenter ? 0.0 :
          ribEndOffset(
              structuredWing.ribs[spoiler.startRibIndex].rib,
              ribThickness) + endGap;
      const double endOffset = ribStartOffset(
          structuredWing.ribs[spoiler.endRibIndex].rib,
          ribThickness) - endGap;
      const double exportedSpan =
          spoiler.dxfOutline[1].x - spoiler.dxfOutline[0].x;
      const double halfExportedSpan = exportedSpan * 0.5;
      for (const auto& hole : spoiler.lighteningHoleOutlines) {
        if (hole.size() < 3) continue;
        double minimumX = hole.front().x;
        double maximumX = hole.front().x;
        double minimumY = hole.front().y;
        double maximumY = hole.front().y;
        for (const auto point : hole) {
          minimumX = std::min(minimumX, point.x);
          maximumX = std::max(maximumX, point.x);
          minimumY = std::min(minimumY, point.y);
          maximumY = std::max(maximumY, point.y);
        }
        const double centerX = 0.5 * (minimumX + maximumX);
        const double centerY = 0.5 * (minimumY + maximumY);
        const double radius = 0.25 *
            ((maximumX - minimumX) + (maximumY - minimumY));
        const double chordFraction = std::clamp(
            centerY / std::max(1.0e-8, spoiler.width), 0.0, 1.0);
        bool mirroredSegment = false;
        double along = exportedSpan > 1.0e-8
            ? centerX / exportedSpan : 0.0;
        if (spoiler.spansCenter) {
          mirroredSegment = centerX < halfExportedSpan;
          along = halfExportedSpan > 1.0e-8
              ? std::abs(centerX - halfExportedSpan) / halfExportedSpan
              : 0.0;
        }
        along = std::clamp(along, 0.0, 1.0);
        const auto rootCenter = topEdgePoint(
            0, rootOffset, chordFraction, false);
        const auto endCenter = topEdgePoint(
            lastLocal, endOffset, chordFraction, mirroredSegment);
        gp_Pnt center{
            rootCenter.X() + along * (endCenter.X() - rootCenter.X()),
            rootCenter.Y() + along * (endCenter.Y() - rootCenter.Y()),
            rootCenter.Z() + along * (endCenter.Z() - rootCenter.Z())};
        const auto rootLeft = topEdgePoint(
            0, rootOffset, 0.0, false);
        const auto rootRight = topEdgePoint(
            0, rootOffset, 1.0, false);
        const auto endLeft = topEdgePoint(
            lastLocal, endOffset, 0.0, mirroredSegment);
        const auto endRight = topEdgePoint(
            lastLocal, endOffset, 1.0, mirroredSegment);
        const gp_Vec rootChord{rootLeft, rootRight};
        const gp_Vec endChord{endLeft, endRight};
        const gp_Vec chordDirection{
            rootChord.X() + along * (endChord.X() - rootChord.X()),
            rootChord.Y() + along * (endChord.Y() - rootChord.Y()),
            rootChord.Z() + along * (endChord.Z() - rootChord.Z())};
        gp_Vec spanDirection{rootCenter, endCenter};
        gp_Vec normal = spanDirection.Crossed(chordDirection);
        if (normal.Magnitude() <= Precision::Confusion())
          throw std::runtime_error(
              "Unable to determine a spoiler lightening-hole axis");
        normal.Normalize();
        const double cutterHalfLength = spoiler.thickness + 2.0;
        gp_Pnt cutterOrigin = center;
        cutterOrigin.Translate(normal * -cutterHalfLength);
        BRepPrimAPI_MakeCylinder cutter{
            gp_Ax2{cutterOrigin, gp_Dir{normal}}, radius,
            2.0 * cutterHalfLength};
        const auto cutterShape = cutter.Shape();
        if (cutterShape.IsNull())
          throw std::runtime_error(
              "Unable to construct a spoiler lightening-hole cutter");
        BRepAlgoAPI_Cut cut{shape, cutterShape};
        cut.SetRunParallel(true);
        cut.Build();
        if (!cut.IsDone())
          throw std::runtime_error(
              "Unable to cut a spoiler lightening hole");
        shape = cut.Shape();
        TopExp_Explorer solids{shape, TopAbs_SOLID};
        if (!solids.More())
          throw std::runtime_error(
              "A lightening hole removed the spoiler solid");
        const auto singleSolid = solids.Current();
        solids.Next();
        if (!solids.More()) shape = singleSolid;
      }
      BRepTools::Clean(shape);
      return shape;
    };
    const auto addLongPart = [&](const std::string& name,
        const std::vector<std::array<domain::Point2, 4>>& profiles,
        const double endGap) {
      if (!spoiler.spansCenter) {
        auto half = loftSpoilerProfiles(spoiler, profiles, endGap, endGap);
        if (name == "Spoiler")
          half = cutSpoilerLighteningHoles(half, profiles, endGap);
        spoilerShapes.push_back({name, half, true});
        return;
      }
      gp_Trsf mirror;
      mirror.SetMirror(gp_Ax2{gp_Pnt{0.0, 0.0, 0.0}, gp_Dir{0.0, 1.0, 0.0}});
      const auto profileWire = [&](const std::size_t local,
                                   const double offset,
                                   const bool mirrored) {
        BRepBuilderAPI_MakePolygon polygon;
        const std::size_t ribIndex = spoiler.startRibIndex + local;
        for (const auto& point : profiles[local]) {
          auto modelPoint = transformLocal(
              structuredWing.ribs[ribIndex].rib, point, offset);
          if (mirrored) modelPoint.Transform(mirror);
          polygon.Add(modelPoint);
        }
        polygon.Close();
        if (!polygon.IsDone())
          throw std::runtime_error("Unable to construct center-spanning spoiler profile");
        return polygon.Wire();
      };
      const auto profileOffset = [&](const std::size_t local) {
        if (local == 0) return 0.0;
        if (local + 1 == profiles.size())
          return ribStartOffset(
              structuredWing.ribs[spoiler.endRibIndex].rib, ribThickness) -
              endGap;
        return 0.0;
      };
      BRepOffsetAPI_ThruSections fullLoft{
          true, true, Precision::Confusion()};
      fullLoft.CheckCompatibility(false);
      const std::size_t endProfile = profiles.size() - 1;
      fullLoft.AddWire(
          profileWire(endProfile, profileOffset(endProfile), true));
      fullLoft.AddWire(profileWire(0, 0.0, false));
      fullLoft.AddWire(
          profileWire(endProfile, profileOffset(endProfile), false));
      fullLoft.Build();
      if (!fullLoft.IsDone())
        throw std::runtime_error("Unable to loft center-spanning spoiler assembly part");
      auto fullShape = fullLoft.Shape();
      if (name == "Spoiler")
        fullShape =
            cutSpoilerLighteningHoles(fullShape, profiles, endGap);
      spoilerShapes.push_back({name, fullShape, false});
    };
    addLongPart("Spoiler Frame Rail 1", spoiler.forwardRailProfiles, 0.0);
    addLongPart("Spoiler", spoiler.spoilerProfiles, spoiler.gap);
    addLongPart("Spoiler Frame Rail 2", spoiler.aftRailProfiles, 0.0);
    for (std::size_t end = 0; end < spoiler.supportProfiles.size(); ++end) {
      if (spoiler.spansCenter && end == 0) continue;
      const std::size_t ribIndex = end == 0 ? spoiler.startRibIndex : spoiler.endRibIndex;
      const auto& fullProfile = spoiler.supportProfiles[end];
      std::array<domain::Point2, 4> support = fullProfile;
      support[2].y -= spoiler.thickness;
      support[3].y -= spoiler.thickness;
      BRepBuilderAPI_MakePolygon polygon;
      const double offset = end == 0
          ? ribEndOffset(structuredWing.ribs[ribIndex].rib, ribThickness)
          : ribStartOffset(structuredWing.ribs[ribIndex].rib, ribThickness) - spoiler.frameRailWidth;
      for (const auto& point : support)
        polygon.Add(transformLocal(structuredWing.ribs[ribIndex].rib, point, offset));
      polygon.Close();
      BRepBuilderAPI_MakeFace face{polygon.Wire()};
      const double plane = structuredWing.ribs[ribIndex].rib.ribPlaneAngleDegrees *
          std::numbers::pi / 180.0;
      const gp_Vec direction{0.0, std::cos(plane) * spoiler.frameRailWidth,
                            std::sin(plane) * spoiler.frameRailWidth};
      spoilerShapes.push_back({"Spoiler Support Rail " + std::to_string(end + 1),
          BRepPrimAPI_MakePrism{face.Face(), direction}.Shape(),
          !(spoiler.spansCenter && end == 0)});
    }
  }
  struct SheetingCutter {
    int verticalLocation{};
    std::string name;
    TopoDS_Shape shape;
  };
  std::vector<SheetingCutter> sheetingCutters;
  for (const auto& member : structuredWing.members) {
    if (member.cutsSheeting)
      sheetingCutters.push_back(
          {member.verticalLocation, member.name, buildMemberShape(member)});
  }
  for (const auto& spoiler : structuredWing.spoilers) {
    std::vector<std::array<domain::Point2, 4>> assemblyProfiles;
    assemblyProfiles.reserve(spoiler.forwardRailProfiles.size());
    for (std::size_t i = 0; i < spoiler.forwardRailProfiles.size(); ++i) {
      auto profile = std::array<domain::Point2, 4>{
          spoiler.forwardRailProfiles[i][0], spoiler.aftRailProfiles[i][1],
          spoiler.aftRailProfiles[i][2], spoiler.forwardRailProfiles[i][3]};
      // Avoid a coplanar Boolean against the sheeting's outer face. The small
      // overcut is wholly outside the finished spoiler assembly.
      profile[0].y -= 0.2; profile[1].y -= 0.2;
      profile[2].y += 0.2; profile[3].y += 0.2;
      assemblyProfiles.push_back(profile);
    }
    sheetingCutters.push_back({0, "Spoiler assembly",
        loftSpoilerProfiles(spoiler, assemblyProfiles, 0.0, 0.0)});
  }
  const auto cutSheeting = [&](const std::string& name, TopoDS_Shape shape) {
    const int verticalLocation = name.find("top sheeting") != std::string::npos ? 0 :
        name.find("bottom sheeting") != std::string::npos ? 1 : 2;
    if (verticalLocation == 2) return shape;
    for (const auto& cutter : sheetingCutters) {
      if (cutter.verticalLocation != verticalLocation) continue;
      BRepAlgoAPI_Cut cut{shape, cutter.shape};
      cut.SetRunParallel(true);
      cut.Build();
      if (!cut.IsDone())
        throw std::runtime_error("Unable to cut " + name + " around " + cutter.name);
      shape = cut.Shape();
    }
    return shape;
  };

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(65, "Lofting wing sheeting");
  for (const auto& sheet : structuredWing.sheeting) {
    if (sheet.profiles.size() != sheet.stopRibIndex + 1)
      throw std::runtime_error("Sheeting profiles do not match their rib stations");
    const auto addProfile = [&](BRepOffsetAPI_ThruSections& loft,
                                const std::vector<domain::Point2>& profile,
                                const std::size_t i, const double yOffset) {
      if (profile.size() < 6 || profile.size() % 2 != 0)
        throw std::runtime_error(
            "Sheeting profile cannot be divided into outer and inner contours");
      const std::size_t contourSize = profile.size() / 2;
      const auto splineEdge = [&](const std::size_t begin,
                                  const std::size_t end) {
        const auto points =
            Handle(OcctPointArray){
                new OcctPointArray{
                    1, static_cast<int>(end - begin + 1)}};
        for (std::size_t point = begin; point <= end; ++point)
          points->SetValue(
              static_cast<int>(point - begin + 1),
              transformLocal(
                  structuredWing.ribs[i].rib, profile[point], yOffset));
        GeomAPI_Interpolate interpolation{
            points, false, Precision::Confusion()};
        interpolation.Perform();
        return BRepBuilderAPI_MakeEdge{interpolation.Curve()}.Edge();
      };
      const auto outerEnd = transformLocal(
          structuredWing.ribs[i].rib, profile[contourSize - 1], yOffset);
      const auto innerStart = transformLocal(
          structuredWing.ribs[i].rib, profile[contourSize], yOffset);
      const auto innerEnd = transformLocal(
          structuredWing.ribs[i].rib, profile.back(), yOffset);
      const auto outerStart = transformLocal(
          structuredWing.ribs[i].rib, profile.front(), yOffset);
       BRepBuilderAPI_MakeWire wire;
       wire.Add(splineEdge(0, contourSize - 1));
       if (outerEnd.Distance(innerStart) > Precision::Confusion())
         wire.Add(BRepBuilderAPI_MakeEdge{outerEnd, innerStart}.Edge());
       wire.Add(splineEdge(contourSize, profile.size() - 1));
       if (innerEnd.Distance(outerStart) > Precision::Confusion())
         wire.Add(BRepBuilderAPI_MakeEdge{innerEnd, outerStart}.Edge());
      if (!wire.IsDone())
        throw std::runtime_error(
            "Unable to construct a spline sheeting profile");
      loft.AddWire(wire.Wire());
    };
    const auto addSegment = [&](const std::vector<domain::Point2>& firstProfile,
                                const std::size_t firstRib, const double firstOffset,
                                const std::vector<domain::Point2>& secondProfile,
                                const std::size_t secondRib, const double secondOffset) {
      BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
      loft.CheckCompatibility(false);
      addProfile(loft, firstProfile, firstRib, firstOffset);
      addProfile(loft, secondProfile, secondRib, secondOffset);
      loft.Build();
      if (!loft.IsDone()) throw std::runtime_error("Unable to loft a wing sheeting segment");
      addPartShape(sheet.name, cutSheeting(sheet.name, loft.Shape()), PartMaterial::Wood);
    };
    if (!sheet.controlBays.empty()) {
      if (sheet.controlBays.size() != sheet.stopRibIndex ||
          sheet.fullProfiles.size() != sheet.profiles.size() ||
          sheet.controlProfiles.size() != sheet.profiles.size())
        throw std::runtime_error("Control-surface sheeting profiles do not match their bays");
      for (std::size_t i = 0; i <= sheet.stopRibIndex; ++i)
        addSegment(sheet.profiles[i], i,
                   ribStartOffset(structuredWing.ribs[i].rib, ribThickness),
                   sheet.profiles[i], i,
                   ribEndOffset(structuredWing.ribs[i].rib, ribThickness));
      for (std::size_t bay = 0; bay < sheet.stopRibIndex; ++bay) {
        const auto& bayProfiles = sheet.controlBays[bay]
            ? sheet.controlProfiles : sheet.fullProfiles;
        addSegment(bayProfiles[bay], bay,
                   ribEndOffset(structuredWing.ribs[bay].rib, ribThickness),
                   bayProfiles[bay + 1], bay + 1,
                   ribStartOffset(structuredWing.ribs[bay + 1].rib, ribThickness));
      }
    } else {
      BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
      loft.CheckCompatibility(false);
      addProfile(loft, sheet.profiles[0], 0,
          ribStartOffset(structuredWing.ribs[0].rib, ribThickness));
      for (std::size_t i = 0; i <= sheet.stopRibIndex; ++i) {
        addProfile(loft, sheet.profiles[i], i,
            ribEndOffset(structuredWing.ribs[i].rib, ribThickness));
        if (i < sheet.stopRibIndex)
          addProfile(loft, sheet.profiles[i + 1], i + 1,
              ribStartOffset(structuredWing.ribs[i + 1].rib, ribThickness));
      }
      loft.Build();
      if (!loft.IsDone()) throw std::runtime_error("Unable to loft wing sheeting");
      addPartShape(sheet.name, cutSheeting(sheet.name, loft.Shape()), PartMaterial::Wood);
    }
  }
  if (timings) timings->sheetingMs = elapsedMs(stageStart);

  for (const auto& spoiler : spoilerShapes)
    addPartShape(spoiler.name, spoiler.shape, PartMaterial::Wood, spoiler.mirror);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(75, "Building spars and span members");
  for (const auto& member : structuredWing.members) {
    const bool carbonFiber = member.carbonFiber ||
        member.kind == domain::SpanMemberKind::Tube ||
        member.kind == domain::SpanMemberKind::Rod;
    addPartShape(member.name, buildMemberShape(member),
        carbonFiber ? PartMaterial::CarbonFiber : PartMaterial::Wood);
  }
  if (timings) timings->membersMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(82, "Building shear webs");
  for (const auto& web : structuredWing.shearWebs) {
    const std::size_t i = web.bayIndex - 1;
    const auto& rootRib = structuredWing.ribs[i].rib;
    const auto& tipRib = structuredWing.ribs[i + 1].rib;
    // The web occupies only the clear bay between the facing rib surfaces.
    const double rootFace = ribEndOffset(rootRib, ribThickness);
    const double tipFace = ribStartOffset(tipRib, ribThickness);
    const gp_Pnt bottom0 = transformLocal(rootRib, web.stationCorners[0], rootFace);
    const gp_Pnt bottom1 = transformLocal(tipRib, web.stationCorners[1], tipFace);
    const gp_Pnt top1 = transformLocal(tipRib, web.stationCorners[2], tipFace);
    const gp_Pnt top0 = transformLocal(rootRib, web.stationCorners[3], rootFace);
    const auto addTriangle = [&](const gp_Pnt& a, const gp_Pnt& b, const gp_Pnt& c) {
      BRepBuilderAPI_MakePolygon polygon;
      polygon.Add(a); polygon.Add(b); polygon.Add(c); polygon.Close();
      BRepBuilderAPI_MakeFace face{polygon.Wire()};
      // Extrude equal half-thicknesses forward and aft from the spar center
      // plane. Keeping the source face on the centerline makes the symmetry
      // explicit and avoids accumulating a one-sided offset.
      const double halfThickness = web.thickness * 0.5;
      addPartShape(web.name, BRepPrimAPI_MakePrism{
          face.Face(), gp_Vec{halfThickness, 0.0, 0.0}}.Shape(), PartMaterial::Wood);
      addPartShape(web.name, BRepPrimAPI_MakePrism{
          face.Face(), gp_Vec{-halfThickness, 0.0, 0.0}}.Shape(), PartMaterial::Wood);
    };
    addTriangle(bottom0, bottom1, top1);
    addTriangle(bottom0, top1, top0);
  }
  if (timings) timings->shearWebsMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(87, "Building joiners and checking collisions");
  for (const auto& joiner : structuredWing.joiners) {
    if (joiner.kind == domain::SpanMemberKind::Rectangular) {
      if (!joiner.innerRectangularProfiles.empty()) {
        BRepOffsetAPI_ThruSections fullLoft{true, true, Precision::Confusion()};
        fullLoft.CheckCompatibility(false);
        const auto addGlobalProfile = [&](const std::array<domain::Point3, 4>& profile) {
          BRepBuilderAPI_MakePolygon polygon;
          for (const auto& point : profile) polygon.Add({point.x, point.y, point.z});
          polygon.Close();
          if (!polygon.IsDone())
            throw std::runtime_error("Unable to construct the full wood joiner profile");
          fullLoft.AddWire(polygon.Wire());
        };
        for (const auto& profile : joiner.innerRectangularProfiles)
          addGlobalProfile(profile);
        std::array<domain::Point3, 4> outerSecond{};
        const auto& secondRib = structuredWing.ribs[joiner.stopRibIndex].rib;
        const double secondOffset = ribEndOffset(secondRib, ribThickness);
        for (std::size_t corner = 0; corner < outerSecond.size(); ++corner) {
          const auto point = transformLocal(secondRib,
              joiner.rectangularProfiles[joiner.stopRibIndex][corner], secondOffset);
          outerSecond[corner] = {point.X(), point.Y(), point.Z()};
        }
        addGlobalProfile(outerSecond);
        fullLoft.Build();
        if (!fullLoft.IsDone())
          throw std::runtime_error("Unable to loft the full wood joiner");
        addPartShape(joiner.name, fullLoft.Shape(), PartMaterial::Wood,
            joiner.mirrorInAssembly);
        continue;
      }
      BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
      loft.CheckCompatibility(false);
      const auto addProfile = [&](const std::size_t i, const double yOffset) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& point : joiner.rectangularProfiles[i])
          polygon.Add(transformLocal(structuredWing.ribs[i].rib, point, yOffset));
        polygon.Close();
        if (!polygon.IsDone()) throw std::runtime_error("Unable to construct wood joiner profile");
        loft.AddWire(polygon.Wire());
      };
      addProfile(0, ribStartOffset(structuredWing.ribs[0].rib, ribThickness));
      for (std::size_t i = 0; i < joiner.stopRibIndex; ++i)
        addProfile(i, ribEndOffset(structuredWing.ribs[i].rib, ribThickness));
      addProfile(joiner.stopRibIndex,
          ribStartOffset(structuredWing.ribs[joiner.stopRibIndex].rib, ribThickness));
      loft.Build();
      if (!loft.IsDone()) throw std::runtime_error("Unable to loft center spar wood joiner");
      const auto outerHalf = loft.Shape();
      addPartShape(joiner.name, outerHalf, PartMaterial::Wood,
          joiner.mirrorInAssembly);
      if (joiner.spansJoint) {
        const auto rootPoint = transformLocal(structuredWing.ribs.front().rib,
            joiner.rectangularProfiles.front()[0]);
        const double angle = joiner.mirrorPlaneAngleDegrees *
            std::numbers::pi / 180.0;
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2{rootPoint, gp_Dir{0.0, std::cos(angle), std::sin(angle)}});
        addPartShape(joiner.name, BRepBuilderAPI_Transform{outerHalf, mirror, true}.Shape(),
            PartMaterial::Wood);
      }
      continue;
    }
    auto start = joiner.hasExplicitEndpoints
        ? gp_Pnt{joiner.innerEndpoint.x, joiner.innerEndpoint.y, joiner.innerEndpoint.z}
        : transformLocal(structuredWing.ribs.front().rib, joiner.centers.front());
    auto end = joiner.hasExplicitEndpoints
        ? gp_Pnt{joiner.outerEndpoint.x, joiner.outerEndpoint.y, joiner.outerEndpoint.z}
        : transformLocal(structuredWing.ribs[joiner.stopRibIndex].rib, joiner.centers.back());
    if (joiner.hasExplicitEndpoints) {
      addPartShape(joiner.name, makeTubeSegment(start, end, joiner.outerDiameter,
          joiner.kind == domain::SpanMemberKind::Tube ? joiner.innerDiameter : 0.0),
          materialForName(joiner.name), joiner.mirrorInAssembly);
      continue;
    }
    const gp_Vec axis{start, end};
    const gp_Vec extension = axis * (ribThickness * 0.5 / std::abs(axis.Y()));
    end.Translate(extension);
    if (joiner.spansJoint) {
      const gp_Pnt root = start;
      start.Translate(gp_Vec{end, root});
    } else {
      start.Translate(-extension);
    }
    addPartShape(joiner.name, makeTubeSegment(start, end, joiner.outerDiameter,
        joiner.kind == domain::SpanMemberKind::Tube ? joiner.innerDiameter : 0.0),
        materialForName(joiner.name));
  }

  const auto isWiringCollisionTarget = [&](const std::string& name) {
    return isSparMember(name) || name == "CF tube" || name == "CF rod" ||
        isWoodJoiner(name) || isCheckedJoiner(name) ||
        name.find("joiner") != std::string::npos ||
        name.find("Joiner") != std::string::npos ||
        name.starts_with("Alignment Pin");
  };
  for (const auto& opening : structuredWing.wiringHoles) {
    if (opening.ribIndex >= structuredWing.ribs.size() || opening.outline.size() < 3)
      throw std::runtime_error("Wiring Hole references an invalid rib");
    const auto& rib = structuredWing.ribs[opening.ribIndex].rib;
    const double planeAngle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const gp_Vec ribNormal{0.0, std::cos(planeAngle), std::sin(planeAngle)};
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& point : opening.outline)
      polygon.Add(transformLocal(rib, point, ribStartOffset(rib, ribThickness) - 1.0));
    polygon.Close();
    BRepBuilderAPI_MakeFace face{polygon.Wire()};
    BRepPrimAPI_MakePrism prism{face.Face(), ribNormal * (ribThickness + 2.0)};
    const auto cutter = prism.Shape();
    Bnd_Box cutterBounds;
    BRepBndLib::Add(cutter, cutterBounds);
    for (const auto& part : nonRibShapes) {
      if (!isWiringCollisionTarget(part.name) || cutterBounds.IsOut(part.bounds)) continue;
      BRepAlgoAPI_Common common{cutter, part.shape};
      common.SetRunParallel(true);
      common.Build();
      if (!common.IsDone())
        throw std::runtime_error("Unable to check collision between " + opening.name +
            " and " + part.name);
      GProp_GProps properties;
      BRepGProp::VolumeProperties(common.Shape(), properties);
      if (properties.Mass() > 1.0e-3)
        throw std::invalid_argument("Geometric collision between " + opening.name +
            " and " + part.name);
    }
  }
  if (timings) timings->joinersMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(95, "Meshing completed panel geometry");
  // AIS automatic triangulation is disabled in the viewport. Mesh the complete
  // compound once on the worker. This includes ribs, lofted sheeting, spars,
  // joiners, controls, and edge stock in one parallel meshing operation.
  BRepMesh_IncrementalMesh displayMesh{result, 0.75, false, 0.35, true};
  if (!displayMesh.IsDone())
    throw std::runtime_error("Unable to mesh the complete panel for display");
  if (timings) timings->displayMeshMs = elapsedMs(stageStart);
  if (progress)
    progress(100, "Panel geometry complete");
  return result;
}

TopoDS_Shape buildMirroredWingAssemblyPreview(
    const std::vector<domain::StructuredWing>& panels,
    const std::vector<double>& ribThicknesses) {
  if (panels.empty() || panels.size() != ribThicknesses.size())
    throw std::invalid_argument("Wing assembly requires matching panels and thicknesses");
  std::vector<TopoDS_Shape> panelShapes;
  panelShapes.reserve(panels.size());
  for (std::size_t i = 0; i < panels.size(); ++i)
    panelShapes.push_back(buildStructuredWingPreview(panels[i], ribThicknesses[i]));
  return assembleMirroredWingPreview(panelShapes);
}

TopoDS_Shape assembleMirroredWingPreview(const std::vector<TopoDS_Shape>& panelShapes) {
  if (panelShapes.empty())
    throw std::invalid_argument("Wing assembly requires at least one panel shape");

  BRep_Builder builder;
  TopoDS_Compound assembly;
  builder.MakeCompound(assembly);
  gp_Trsf mirror;
  mirror.SetMirror(gp_Ax2{gp_Pnt{0.0, 0.0, 0.0}, gp_Dir{0.0, 1.0, 0.0}});
  for (const auto& panelShape : panelShapes) {
    if (panelShape.IsNull())
      throw std::invalid_argument("Wing assembly contains a null panel shape");
    builder.Add(assembly, panelShape);
    // Copy both geometry and its completed triangulation. Without the fourth
    // argument OCCT creates mirrored faces with no display mesh, which either
    // looks hollow or requires a second expensive meshing pass.
    builder.Add(assembly, BRepBuilderAPI_Transform{panelShape, mirror, true, true}.Shape());
  }
  return assembly;
}

TopoDS_Shape assembleHalfWingPreview(const std::vector<TopoDS_Shape>& panelShapes) {
  if (panelShapes.empty())
    throw std::invalid_argument("Wing assembly requires at least one panel shape");
  BRep_Builder rightBuilder;
  TopoDS_Compound rightWing;
  rightBuilder.MakeCompound(rightWing);
  for (const auto& panelShape : panelShapes) {
    if (panelShape.IsNull()) throw std::invalid_argument("Wing assembly contains a null panel shape");
    rightBuilder.Add(rightWing, panelShape);
  }
  return rightWing;
}

MaterialShapeSet assembleMirroredMaterialPreview(
    const std::vector<MaterialShapeSet>& panelShapes) {
  if (panelShapes.empty())
    throw std::invalid_argument("Material wing assembly requires at least one panel");
  BRep_Builder builder;
  MaterialShapeSet assembly;
  builder.MakeCompound(assembly.wood);
  builder.MakeCompound(assembly.carbonFiber);
  builder.MakeCompound(assembly.aluminum);
  builder.MakeCompound(assembly.steel);
  builder.MakeCompound(assembly.fiberglass);
  builder.MakeCompound(assembly.unmirroredWood);
  builder.MakeCompound(assembly.unmirroredCarbonFiber);
  builder.MakeCompound(assembly.unmirroredAluminum);
  builder.MakeCompound(assembly.unmirroredSteel);
  builder.MakeCompound(assembly.unmirroredFiberglass);
  gp_Trsf mirror;
  mirror.SetMirror(gp_Ax2{gp_Pnt{0.0, 0.0, 0.0}, gp_Dir{0.0, 1.0, 0.0}});
  const auto addMirrored = [&](TopoDS_Compound& target, const TopoDS_Compound& panel) {
    if (panel.IsNull()) return;
    builder.Add(target, panel);
    builder.Add(target, BRepBuilderAPI_Transform{panel, mirror, true, true}.Shape());
  };
  for (std::size_t panelIndex = 0; panelIndex < panelShapes.size(); ++panelIndex) {
    const auto& panel = panelShapes[panelIndex];
    addMirrored(assembly.wood, panel.wood);
    addMirrored(assembly.carbonFiber, panel.carbonFiber);
    addMirrored(assembly.aluminum, panel.aluminum);
    addMirrored(assembly.steel, panel.steel);
    addMirrored(assembly.fiberglass, panel.fiberglass);
    if (!panel.unmirroredWood.IsNull()) builder.Add(assembly.wood, panel.unmirroredWood);
    if (!panel.unmirroredCarbonFiber.IsNull())
      builder.Add(assembly.carbonFiber, panel.unmirroredCarbonFiber);
    if (!panel.unmirroredAluminum.IsNull())
      builder.Add(assembly.aluminum, panel.unmirroredAluminum);
    if (!panel.unmirroredSteel.IsNull())
      builder.Add(assembly.steel, panel.unmirroredSteel);
    if (!panel.unmirroredFiberglass.IsNull())
      builder.Add(assembly.fiberglass, panel.unmirroredFiberglass);
    for (const auto& part : panel.parts) {
      if (part.mirrorInAssembly) {
        assembly.parts.push_back({
            "Right Panel " + std::to_string(panelIndex + 1) + " - " + part.name,
            part.shape, part.material, false});
        assembly.parts.push_back({
            "Left Panel " + std::to_string(panelIndex + 1) + " - " + part.name,
            BRepBuilderAPI_Transform{part.shape, mirror, true, true}.Shape(),
            part.material, false});
      } else if (part.name.ends_with(" Right")) {
        assembly.parts.push_back({
            "Right Panel " + std::to_string(panelIndex + 1) + " - " +
                part.name,
            part.shape, part.material, false});
      } else if (part.name.ends_with(" Left")) {
        assembly.parts.push_back({
            "Left Panel " + std::to_string(panelIndex + 1) + " - " +
                part.name,
            part.shape, part.material, false});
      } else {
        assembly.parts.push_back({"Center - " + part.name,
            part.shape, part.material, false});
      }
    }
  }
  return assembly;
}

} // namespace designrc::geometry
