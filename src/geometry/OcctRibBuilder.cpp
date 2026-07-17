#include "geometry/OcctRibBuilder.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <stdexcept>
#include <sstream>
#include <vector>
#include <numbers>
#include <cmath>
#include <chrono>

namespace designrc::geometry {

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

} // namespace

TopoDS_Shape buildStructuredWingPreview(const domain::StructuredWing& structuredWing,
                                        const double ribThickness,
                                        PanelBuildTimings* timings,
                                        MaterialShapeSet* materialShapes) {
  if (structuredWing.ribs.empty() || ribThickness <= 0.0)
    throw std::invalid_argument("Structured wing preview requires ribs and positive thickness");
  BRep_Builder builder;
  TopoDS_Compound result;
  builder.MakeCompound(result);
  if (materialShapes) {
    builder.MakeCompound(materialShapes->wood);
    builder.MakeCompound(materialShapes->carbonFiber);
    builder.MakeCompound(materialShapes->aluminum);
  }
  enum class PreviewMaterial { Wood, CarbonFiber, Aluminum };
  const auto addShape = [&](const TopoDS_Shape& shape, const PreviewMaterial material) {
    builder.Add(result, shape);
    if (!materialShapes) return;
    switch (material) {
      case PreviewMaterial::Wood: builder.Add(materialShapes->wood, shape); break;
      case PreviewMaterial::CarbonFiber: builder.Add(materialShapes->carbonFiber, shape); break;
      case PreviewMaterial::Aluminum: builder.Add(materialShapes->aluminum, shape); break;
    }
  };
  const auto materialForName = [](const std::string& name) {
    if (name.find("Aluminum") != std::string::npos) return PreviewMaterial::Aluminum;
    if (name.find("CF") != std::string::npos) return PreviewMaterial::CarbonFiber;
    return PreviewMaterial::Wood;
  };

  const auto elapsedMs = [](const std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
  };
  auto stageStart = std::chrono::steady_clock::now();

  for (std::size_t structuredIndex = 0; structuredIndex < structuredWing.ribs.size(); ++structuredIndex) {
    const auto& structured = structuredWing.ribs[structuredIndex];
    BRepBuilderAPI_MakePolygon outer;
    for (const auto& point : structured.outerOutline)
      outer.Add(transformLocal(structured.rib, point, ribStartOffset(structured.rib, ribThickness)));
    outer.Close();
    if (!outer.IsDone()) throw std::runtime_error("Unable to construct notched rib outline");
    const double planeAngle = structured.rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
    const gp_Vec ribNormal{0.0, std::cos(planeAngle), std::sin(planeAngle)};
    const bool centerRoot = std::abs(structured.rib.spanPosition) < 1.0e-9 &&
        std::abs(structured.rib.ribThicknessStartFactor) < 1.0e-9;
    const gp_Pln plane{transformLocal(structured.rib, structured.outerOutline.front(), ribStartOffset(structured.rib, ribThickness)),
                       centerRoot ? gp_Dir{0.0, 1.0, 0.0} : gp_Dir{ribNormal}};
    BRepBuilderAPI_MakeFace face{plane, outer.Wire(), true};
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
    for (const auto& hole : structured.booleanHoles) {
      domain::Point2 center{};
      for (const auto& point : hole) { center.x += point.x; center.y += point.y; }
      center.x /= static_cast<double>(hole.size());
      center.y /= static_cast<double>(hole.size());
      double radius = 0.0;
      for (const auto& point : hole)
        radius = std::max(radius, std::hypot(point.x - center.x, point.y - center.y));
      const auto start = transformLocal(structured.rib, center,
          ribStartOffset(structured.rib, ribThickness) - 1.0);
      BRepPrimAPI_MakeCylinder holeTool{
          gp_Ax2{start, gp_Dir{ribNormal}}, radius, ribThickness + 2.0};
      BRepAlgoAPI_Cut cut{ribSolid, holeTool.Shape()};
      cut.Build();
      if (!cut.IsDone()) throw std::runtime_error("Unable to cut a circular rib opening");
      ribSolid = cut.Shape();
      TopExp_Explorer cutSolids{ribSolid, TopAbs_SOLID};
      if (!cutSolids.More()) throw std::runtime_error("A circular opening removed the rib solid");
      if (!BRepCheck_Analyzer{ribSolid, false}.IsValid()) {
        ShapeFix_Shape fixer{ribSolid};
        fixer.Perform();
        ribSolid = fixer.Shape();
      }
    }
    // Make all internal openings while the rib is still one solid. The wood
    // joiner through-slot is applied last because it intentionally separates
    // a joint rib into two independently retained solids.
    for (const auto& cutout : structured.booleanCutouts) {
      BRepBuilderAPI_MakePolygon cutPolygon;
      for (const auto& point : cutout)
        cutPolygon.Add(transformLocal(structured.rib, point,
            ribStartOffset(structured.rib, ribThickness) - 1.0));
      cutPolygon.Close();
      BRepBuilderAPI_MakeFace cutFace{cutPolygon.Wire()};
      BRepPrimAPI_MakePrism cutTool{cutFace.Face(), ribNormal * (ribThickness + 2.0)};
      BRepAlgoAPI_Cut cut{ribSolid, cutTool.Shape()};
      cut.Build();
      if (!cut.IsDone()) throw std::runtime_error("Unable to cut the wood joiner slot");
      ribSolid = cut.Shape();
      TopExp_Explorer cutSolids{ribSolid, TopAbs_SOLID};
      if (!cutSolids.More()) throw std::runtime_error("Wood joiner cut removed the rib solid");
      if (!BRepCheck_Analyzer{ribSolid, false}.IsValid()) {
        ShapeFix_Shape fixer{ribSolid};
        fixer.Perform();
        ribSolid = fixer.Shape();
      }
    }
    std::vector<TopoDS_Shape> resultingSolids;
    for (TopExp_Explorer solids{ribSolid, TopAbs_SOLID}; solids.More(); solids.Next())
      resultingSolids.push_back(solids.Current());
    if (resultingSolids.empty()) {
      throw std::runtime_error("Structured rib " + std::to_string(structuredIndex + 1) +
          " extrusion did not produce a valid capped solid (0 result solids)");
    }
    for (auto& solid : resultingSolids) {
      if (!BRepCheck_Analyzer{solid, false}.IsValid())
        throw std::runtime_error("Structured rib " + std::to_string(structuredIndex + 1) +
            " contains an invalid solid after Boolean cuts");
      addShape(solid, PreviewMaterial::Wood);
    }
  }
  if (timings) timings->ribsMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  for (const auto& member : structuredWing.profiledMembers) {
    if (member.profiles.size() != structuredWing.ribs.size())
      throw std::runtime_error("Edge stock profiles do not match the rib stations");
    auto ranges = member.activeRanges;
    if (ranges.empty()) ranges.emplace_back(0, member.profiles.size() - 1);
    for (const auto [first, last] : ranges) {
      BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
      loft.CheckCompatibility(false);
      const auto addProfile = [&](const std::size_t i, const double yOffset) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& point : member.profiles[i])
          polygon.Add(transformLocal(structuredWing.ribs[i].rib, point, yOffset));
        polygon.Close();
        if (!polygon.IsDone()) throw std::runtime_error("Unable to construct edge stock profile");
        loft.AddWire(polygon.Wire());
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
      addShape(loft.Shape(), PreviewMaterial::Wood);
    }
  }
  if (timings) timings->profiledStockMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  for (const auto& control : structuredWing.controlSurfaces) {
    BRepOffsetAPI_ThruSections loft{true, true, Precision::Confusion()};
    loft.CheckCompatibility(false);
    const auto addControlProfile = [&](const std::size_t localIndex, const double yOffset) {
      const std::size_t ribIndex = control.startRibIndex + localIndex;
      BRepBuilderAPI_MakePolygon polygon;
      for (const auto& point : control.profiles[localIndex])
        polygon.Add(transformLocal(structuredWing.ribs[ribIndex].rib, point, yOffset));
      polygon.Close();
      if (!polygon.IsDone()) throw std::runtime_error("Unable to construct control-surface profile");
      loft.AddWire(polygon.Wire());
    };
    addControlProfile(0, ribThickness * 0.5 + control.gap);
    for (std::size_t local = 1; local + 1 < control.profiles.size(); ++local) {
      addControlProfile(local, -ribThickness * 0.5);
      addControlProfile(local, ribThickness * 0.5);
    }
    addControlProfile(control.profiles.size() - 1, -ribThickness * 0.5 - control.gap);
    loft.Build();
    if (!loft.IsDone()) throw std::runtime_error("Unable to loft the control surface");
    addShape(loft.Shape(), PreviewMaterial::Wood);

    const auto hingeStart = transformLocal(
        structuredWing.ribs[control.startRibIndex].rib,
        control.hingePostCenters.front(), ribThickness * 0.5);
    const auto hingeEnd = transformLocal(
        structuredWing.ribs[control.stopRibIndex].rib,
        control.hingePostCenters.back(), -ribThickness * 0.5);
    addShape(makeRectangularSegment(
        hingeStart, hingeEnd, control.hingePostWidth, control.hingePostHeight),
        PreviewMaterial::Wood);
  }
  if (timings) timings->controlsMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  for (const auto& sheet : structuredWing.sheeting) {
    if (sheet.profiles.size() != sheet.stopRibIndex + 1)
      throw std::runtime_error("Sheeting profiles do not match their rib stations");
    const auto addProfile = [&](BRepOffsetAPI_ThruSections& loft,
                                const std::vector<domain::Point2>& profile,
                                const std::size_t i, const double yOffset) {
      BRepBuilderAPI_MakePolygon polygon;
      for (const auto& point : profile)
        polygon.Add(transformLocal(structuredWing.ribs[i].rib, point, yOffset));
      polygon.Close();
      if (!polygon.IsDone()) throw std::runtime_error("Unable to construct a sheeting profile");
      loft.AddWire(polygon.Wire());
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
      addShape(loft.Shape(), PreviewMaterial::Wood);
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
      addShape(loft.Shape(), PreviewMaterial::Wood);
    }
  }
  if (timings) timings->sheetingMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  for (const auto& member : structuredWing.members) {
    if (member.kind == domain::SpanMemberKind::Tube ||
        member.kind == domain::SpanMemberKind::Rod) {
      const auto start = transformLocal(structuredWing.ribs.front().rib, member.centers.front());
      const auto end = transformLocal(structuredWing.ribs.back().rib, member.centers.back());
      gp_Pnt extendedStart = start;
      gp_Pnt extendedEnd = end;
      const gp_Vec axis{start, end};
      const gp_Vec extension = axis * (ribThickness * 0.5 / std::abs(axis.Y()));
      extendedStart.Translate(-extension);
      extendedEnd.Translate(extension);
      addShape(makeTubeSegment(extendedStart, extendedEnd, member.width,
          member.kind == domain::SpanMemberKind::Tube ? member.innerDiameter : 0.0),
          PreviewMaterial::CarbonFiber);
      continue;
    }
    // Wood spars and turbulators are continuous straight stock within a panel.
    // A separate prism in every rib bay duplicated six faces per bay and made
    // final triangulation needlessly expensive. The boundary centers define
    // the same panel-length member with one rectangular prism.
    const auto start = transformLocal(
        structuredWing.ribs.front().rib, member.centers.front());
    const auto end = transformLocal(
        structuredWing.ribs.back().rib, member.centers.back());
    addShape(makeRectangularSegment(start, end, member.width, member.height),
        PreviewMaterial::Wood);
  }
  if (timings) timings->membersMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  for (const auto& web : structuredWing.shearWebs) {
    const std::size_t i = web.bayIndex - 1;
    const gp_Pnt bottom0 = transformLocal(structuredWing.ribs[i].rib, web.stationCorners[0]);
    const gp_Pnt bottom1 = transformLocal(structuredWing.ribs[i + 1].rib, web.stationCorners[1]);
    const gp_Pnt top1 = transformLocal(structuredWing.ribs[i + 1].rib, web.stationCorners[2]);
    const gp_Pnt top0 = transformLocal(structuredWing.ribs[i].rib, web.stationCorners[3]);
    const auto addTriangle = [&](const gp_Pnt& a, const gp_Pnt& b, const gp_Pnt& c) {
      BRepBuilderAPI_MakePolygon polygon;
      polygon.Add(a); polygon.Add(b); polygon.Add(c); polygon.Close();
      BRepBuilderAPI_MakeFace face{polygon.Wire()};
      addShape(BRepPrimAPI_MakePrism{
          face.Face(), gp_Vec{web.thickness, 0.0, 0.0}}.Shape(), PreviewMaterial::Wood);
    };
    addTriangle(bottom0, bottom1, top1);
    addTriangle(bottom0, top1, top0);
  }
  if (timings) timings->shearWebsMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  for (const auto& joiner : structuredWing.joiners) {
    if (joiner.kind == domain::SpanMemberKind::Rectangular) {
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
      addShape(outerHalf, PreviewMaterial::Wood);
      if (!joiner.innerRectangularProfiles.empty()) {
        BRepOffsetAPI_ThruSections innerLoft{true, true, Precision::Confusion()};
        innerLoft.CheckCompatibility(false);
        for (const auto& profile : joiner.innerRectangularProfiles) {
          BRepBuilderAPI_MakePolygon polygon;
          for (const auto& point : profile) polygon.Add({point.x, point.y, point.z});
          polygon.Close();
          if (!polygon.IsDone())
            throw std::runtime_error("Unable to construct the inner wood joiner profile");
          innerLoft.AddWire(polygon.Wire());
        }
        innerLoft.Build();
        if (!innerLoft.IsDone())
          throw std::runtime_error("Unable to loft the inner wood joiner half");
        addShape(innerLoft.Shape(), PreviewMaterial::Wood);
      } else if (joiner.spansJoint) {
        const auto rootPoint = transformLocal(structuredWing.ribs.front().rib,
            joiner.rectangularProfiles.front()[0]);
        const double angle = joiner.mirrorPlaneAngleDegrees *
            std::numbers::pi / 180.0;
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2{rootPoint, gp_Dir{0.0, std::cos(angle), std::sin(angle)}});
        addShape(BRepBuilderAPI_Transform{outerHalf, mirror, true}.Shape(),
            PreviewMaterial::Wood);
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
      addShape(makeTubeSegment(start, end, joiner.outerDiameter,
          joiner.kind == domain::SpanMemberKind::Tube ? joiner.innerDiameter : 0.0),
          materialForName(joiner.name));
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
    addShape(makeTubeSegment(start, end, joiner.outerDiameter,
        joiner.kind == domain::SpanMemberKind::Tube ? joiner.innerDiameter : 0.0),
        materialForName(joiner.name));
  }
  if (timings) timings->joinersMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  // AIS automatic triangulation is disabled in the viewport. Mesh the complete
  // compound once on the worker. This includes ribs, lofted sheeting, spars,
  // joiners, controls, and edge stock in one parallel meshing operation.
  BRepMesh_IncrementalMesh displayMesh{result, 0.75, false, 0.35, true};
  if (!displayMesh.IsDone())
    throw std::runtime_error("Unable to mesh the complete panel for display");
  if (timings) timings->displayMeshMs = elapsedMs(stageStart);
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
  gp_Trsf mirror;
  mirror.SetMirror(gp_Ax2{gp_Pnt{0.0, 0.0, 0.0}, gp_Dir{0.0, 1.0, 0.0}});
  const auto addMirrored = [&](TopoDS_Compound& target, const TopoDS_Compound& panel) {
    if (panel.IsNull()) return;
    builder.Add(target, panel);
    builder.Add(target, BRepBuilderAPI_Transform{panel, mirror, true, true}.Shape());
  };
  for (const auto& panel : panelShapes) {
    addMirrored(assembly.wood, panel.wood);
    addMirrored(assembly.carbonFiber, panel.carbonFiber);
    addMirrored(assembly.aluminum, panel.aluminum);
  }
  return assembly;
}

} // namespace designrc::geometry
