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
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <GeomFill_Sweep.hxx>
#include <GeomFill_NSections.hxx>
#include <GeomFill_Profiler.hxx>
#include <GeomFill_Fixed.hxx>
#include <GeomFill_CurveAndTrihedron.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
#include <BSplCLib.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
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
#include <GCE2d_MakeSegment.hxx>
#include <Geom_Plane.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Precision.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#if OCC_VERSION_HEX >= 0x080000
#include <NCollection_HArray1.hxx>
#else
#include <TColgp_HArray1OfPnt.hxx>
#include <TColStd_HArray1OfReal.hxx>
#endif
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Wire.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Circ.hxx>
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
#include <cstdlib>
#include <cstdio>
#include <thread>

namespace designrc::geometry {

#if OCC_VERSION_HEX >= 0x080000
using OcctPointArray = NCollection_HArray1<gp_Pnt>;
using OcctParameterArray = NCollection_HArray1<double>;
#else
using OcctPointArray = TColgp_HArray1OfPnt;
using OcctParameterArray = TColStd_HArray1OfReal;
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
    const auto translation = domain::ribTwistTranslation(rib);
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
      const double sectionX = twistCos * localX - twistSin * localZ + translation.x;
      const double sectionZ = twistSin * localX + twistCos * localZ + translation.y;
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

Handle(Geom_BSplineCurve) sectionCurve(const Handle(OcctPointArray)& points, bool imported) {
  // Common transverse parameters keep knot vectors compatible across swept
  // profiles. Chord-length knots differ slightly at every station and their
  // union makes the section law needlessly huge.
  const auto parameters = Handle(OcctParameterArray){new OcctParameterArray{points->Lower(), points->Upper()}};
  double distance = 0;
  for (int i = points->Lower(); i <= points->Upper(); ++i) {
    if (i > points->Lower()) distance += points->Value(i).Distance(points->Value(i - 1));
    parameters->SetValue(i, imported ? static_cast<double>(i - points->Lower()) : distance);
  }
  GeomAPI_Interpolate interpolation{points, parameters, false, Precision::Confusion()};
  interpolation.Perform();
  if (!interpolation.IsDone()) throw std::runtime_error("Unable to interpolate stock section");
  return interpolation.Curve();
}

// Interpolate profile control points with a common C1 cubic law. Keeping
// non-rational profiles non-rational permits an exact guide-translation sweep.
Handle(Geom_BSplineSurface) sectionSurface(
    const NCollection_Sequence<Handle(Geom_Curve)>& curves,
    const NCollection_Sequence<double>& parameters) {
  GeomFill_Profiler profiles;
  bool rational = false;
  for (int i = 1; i <= curves.Length(); ++i) {
    const auto curve = Handle(Geom_BSplineCurve)::DownCast(curves(i));
    rational |= curve.IsNull() || curve->IsRational();
    profiles.AddCurve(curves(i));
  }
  if (rational) {
    GeomFill_NSections law{curves, parameters, 0, 1, parameters.First(), parameters.Last()};
    return law.BSplineSurface();
  }
  profiles.Perform(Precision::PConfusion());
  const int count = curves.Length(), poles = profiles.NbPoles();
  NCollection_Array2<gp_Pnt> grid{1, poles, 1, 3 * (count - 1) + 1};
  NCollection_Array1<double> uKnots{1, profiles.NbKnots()}, vKnots{1, count};
  NCollection_Array1<int> uMults{1, profiles.NbKnots()}, vMults{1, count};
  profiles.KnotsAndMults(uKnots, uMults);
  for (int i = 1; i <= count; ++i) {
    vKnots(i) = parameters(i);
    vMults(i) = i == 1 || i == count ? 4 : 3;
  }
  for (int u = 1; u <= poles; ++u) {
    std::vector<gp_Pnt> points;
    for (int i = 1; i <= count; ++i)
      points.push_back(Handle(Geom_BSplineCurve)::DownCast(profiles.Curve(i))->Pole(u));
    std::vector<gp_Vec> tangents;
    for (int i = 0; i < count; ++i) {
      const int a = std::max(0, i - 1), b = std::min(count - 1, i + 1);
      tangents.push_back(gp_Vec{points[a], points[b]} / (parameters(b + 1) - parameters(a + 1)));
    }
    grid(u, 1) = points.front();
    for (int i = 0; i + 1 < count; ++i) {
      const double step = (parameters(i + 2) - parameters(i + 1)) / 3;
      grid(u, 3 * i + 2) = points[i].Translated(tangents[i] * step);
      grid(u, 3 * i + 3) = points[i + 1].Translated(tangents[i + 1] * -step);
      grid(u, 3 * i + 4) = points[i + 1];
    }
  }
  return new Geom_BSplineSurface{grid, uKnots, vKnots, uMults, vMults, profiles.Degree(), 3};
}

// Separate the path from the section's shape/orientation. Dense stations define
// a smooth guide, but only sections needed to describe shape or twist changes
// are supplied to OCCT's pipe builder. Never fit a surface through every path
// sample: that made ordinary imported wood stock take minutes to generate.
class SpanwiseSurface {
public:
  explicit SpanwiseSurface(bool imported, std::function<bool()> cancelled = {},
                           PanelBuildTimings* timings = nullptr)
      : imported_(imported), cancelled_(std::move(cancelled)),
        loft_(true, true, Precision::Confusion()), timings_(timings) {
    loft_.CheckCompatibility(false);
  }
  void CheckCompatibility(bool value) { loft_.CheckCompatibility(value); }
  void AddWire(const TopoDS_Wire& wire, const domain::RibDefinition* = nullptr) {
    wires_.push_back(wire);
  }
  void Build() {
    checkpoint();
    bool endpointsOnly = imported_ && wires_.size() > 2;
    const auto samples = [](const TopoDS_Wire& wire) {
      std::vector<gp_Pnt> points;
      for (BRepTools_WireExplorer edges{wire}; edges.More(); edges.Next()) {
        BRepAdaptor_Curve curve{edges.Current()};
        for (int i = 0; i <= 8; ++i) {
          const double t = edges.Current().Orientation() == TopAbs_REVERSED ? 1 - i / 8.0 : i / 8.0;
          points.push_back(curve.Value(curve.FirstParameter() +
                                       t * (curve.LastParameter() - curve.FirstParameter())));
        }
      }
      return points;
    };
    if (endpointsOnly) {
      const auto first = samples(wires_.front()), last = samples(wires_.back());
      endpointsOnly = !first.empty() && first.size() == last.size();
      for (std::size_t i = 1; endpointsOnly && i + 1 < wires_.size(); ++i) {
        const auto middle = samples(wires_[i]);
        if (middle.size() != first.size()) {
          endpointsOnly = false;
          break;
        }
        const gp_Vec axis{first.front(), last.front()};
        if (axis.SquareMagnitude() < 1e-12) {
          endpointsOnly = false;
          break;
        }
        const double t = gp_Vec{first.front(), middle.front()}.Dot(axis) / axis.SquareMagnitude();
        if (t <= 0 || t >= 1) {
          endpointsOnly = false;
          break;
        }
        for (std::size_t j = 0; j < first.size(); ++j)
          if (first[j].Translated(gp_Vec{first[j], last[j]} * t).Distance(middle[j]) > 1e-5) {
            endpointsOnly = false;
            break;
          }
      }
    }
    Handle(Message_ProgressIndicator) indicator = new SweepProgress{cancelled_};
    if (!imported_ || endpointsOnly || wires_.size() == 2) {
      for (std::size_t i = 0; i < wires_.size(); ++i)
        if (!endpointsOnly || i == 0 || i + 1 == wires_.size()) loft_.AddWire(wires_[i]);
      loft_.Build(indicator->Start());
      checkpoint();
      if (!loft_.IsDone()) return;
      shape_ = loft_.Shape();
    } else {
      std::vector<std::vector<gp_Pnt>> sections;
      std::vector<gp_Pnt> centers;
      std::vector<double> station{0};
      for (const auto& wire : wires_) {
        checkpoint();
        auto points = samples(wire);
        if (points.empty() || (!sections.empty() && points.size() != sections.front().size()))
          throw std::runtime_error("Curved stock sweep has incompatible section contours");
        gp_XYZ sum{0, 0, 0};
        for (const auto& point : points) sum += point.XYZ();
        gp_Pnt center{sum / static_cast<double>(points.size())};
        if (points.size() == 9) {
          BRepTools_WireExplorer edge{wire};
          BRepAdaptor_Curve curve{edge.Current()};
          if (curve.GetType() == GeomAbs_Circle && curve.IsClosed())
            center = curve.Circle().Location(); // Same guide for both walls of a CF tube.
        }
        if (!centers.empty()) station.push_back(station.back() + center.Distance(centers.back()));
        centers.push_back(center);
        sections.push_back(std::move(points));
      }
      // Adapt section density to changes relative to the guide, not to the
      // guide's curvature. Rotated profiles encode the same twist as the ribs.
      std::vector<std::size_t> selected{0};
      const auto select = [&](auto&& self, std::size_t first, std::size_t last) -> void {
        checkpoint();
        double error = 0.001; // mm, below the importer's path tolerance
        std::size_t split = first;
        for (std::size_t i = first + 1; i < last; ++i) {
          const double t = (station[i] - station[first]) / (station[last] - station[first]);
          for (std::size_t j = 0; j < sections[i].size(); ++j) {
            const gp_XYZ expected = (sections[first][j].XYZ() - centers[first].XYZ()) * (1 - t) +
                                    (sections[last][j].XYZ() - centers[last].XYZ()) * t;
            const double deviation =
                (sections[i][j].XYZ() - centers[i].XYZ() - expected).Modulus();
            if (deviation > error) { error = deviation; split = i; }
          }
        }
        if (split != first) { self(self, first, split); self(self, split, last); }
        else selected.push_back(last);
      };
      select(select, 0, wires_.size() - 1);
      if (std::getenv("DESIGNRC_SWEEP_DIAGNOSTICS"))
        std::fprintf(stderr, "Sweep: %zu guide stations, %zu selected profiles, %zu contour samples\n",
                     wires_.size(), selected.size(), sections.front().size());
      if (selected.size() > 64)
        throw std::runtime_error("Curved stock section changes are too complex for a smooth sweep");
      const auto guidePoints = Handle(OcctPointArray){new OcctPointArray{1, static_cast<int>(centers.size())}};
      for (std::size_t i = 0; i < centers.size(); ++i)
        guidePoints->SetValue(static_cast<int>(i + 1), centers[i]);
      GeomAPI_PointsToBSpline guide{guidePoints->Array1(), 3, 5, GeomAbs_C2, 0.005};
      if (!guide.IsDone()) throw std::runtime_error("Unable to build curved stock sweep guide");
      // End profiles must stay exactly on the panel end planes.
      guide.Curve()->SetPole(1, centers.front());
      guide.Curve()->SetPole(guide.Curve()->NbPoles(), centers.back());
      if (std::getenv("DESIGNRC_SWEEP_DIAGNOSTICS"))
        std::fprintf(stderr, "Guide: %d poles, %d knots\n", guide.Curve()->NbPoles(), guide.Curve()->NbKnots());
      std::vector<double> locationsOnGuide;
      for (std::size_t j = 0; j < wires_.size(); ++j) {
        GeomAPI_ProjectPointOnCurve projection{centers[j], guide.Curve()};
        if (projection.NbPoints() == 0) throw std::runtime_error("Unable to locate stock section on its guide");
        const double parameter = j == 0 ? guide.Curve()->FirstParameter()
                                  : j + 1 == wires_.size() ? guide.Curve()->LastParameter()
                                                           : projection.LowerDistanceParameter();
        if (!locationsOnGuide.empty() && parameter <= locationsOnGuide.back())
          throw std::runtime_error("Curved stock guide doubles back at a section");
        locationsOnGuide.push_back(parameter);
      }
      bool circular = sections.front().size() == 9;
      gp_Circ rootCircle;
      for (std::size_t i = 0; circular && i < wires_.size(); ++i) {
        BRepTools_WireExplorer edge{wires_[i]};
        BRepAdaptor_Curve curve{edge.Current()};
        circular = curve.GetType() == GeomAbs_Circle && curve.IsClosed();
        if (circular) {
          const auto circle = curve.Circle();
          if (i == 0) rootCircle = circle;
          else circular = std::abs(circle.Radius() - rootCircle.Radius()) < 1e-7 &&
              circle.Axis().Direction().IsParallel(rootCircle.Axis().Direction(), 1e-7);
        }
      }
      bool swept = false;
      try {
        if (circular) {
          // A circular section is invariant under panel twist. The single-
          // profile pipe builder also constructs its periodic seam and caps.
          const auto spine = BRepBuilderAPI_MakeWire{BRepBuilderAPI_MakeEdge{guide.Curve()}.Edge()}.Wire();
          BRepOffsetAPI_MakePipeShell pipe{spine};
          pipe.SetMode(rootCircle.Position());
          pipe.SetTolerance(0.005, 0.005, 0.01);
          pipe.SetMaxSegments(std::max(200, guide.Curve()->NbKnots() * 8));
          pipe.Add(wires_.front(), false, false);
          pipe.Build(indicator->Start());
          checkpoint();
          if (pipe.IsDone() && pipe.MakeSolid()) {
            shape_ = pipe.Shape();
            swept = BRepCheck_Analyzer{shape_, false}.IsValid();
          }
        } else {
          // The guide supplies translation; the section law supplies shape
          // and rotation in world axes. Avoid automatic profile rematching.
          Handle(GeomFill_LocationLaw) location = new GeomFill_CurveAndTrihedron{
              new GeomFill_Fixed{gp_Vec{0, 0, 1}, gp_Vec{1, 0, 0}}};
          location->SetCurve(new GeomAdaptor_Curve{guide.Curve()});
          std::vector<std::vector<Handle(Geom_BSplineCurve)>> contours;
          for (std::size_t i = 0; i < wires_.size(); ++i) {
            std::vector<Handle(Geom_BSplineCurve)> contour;
            for (BRepTools_WireExplorer edge{wires_[i]}; edge.More(); edge.Next()) {
              TopLoc_Location placement;
              double first, last;
              auto curve = BRep_Tool::Curve(edge.Current(), placement, first, last);
              auto local = GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve{curve, first, last});
              local->Transform(placement.Transformation());
              if (edge.Current().Orientation() == TopAbs_REVERSED) local->Reverse();
              local->Translate(gp_Vec{guide.Curve()->Value(locationsOnGuide[i]), gp_Pnt{0, 0, 0}});
              auto knots = local->Knots();
              BSplCLib::Reparametrize(0, 1, knots);
              local->SetKnots(knots);
              contour.push_back(local);
            }
            contours.push_back(std::move(contour));
          }
          std::vector<Handle(Geom_BSplineSurface)> laws;
          for (;;) {
            laws.clear();
            double worst = 0.005;
            std::size_t split = wires_.size();
            for (std::size_t edge = 0; edge < contours.front().size(); ++edge) {
              checkpoint();
              NCollection_Sequence<Handle(Geom_Curve)> curves;
              NCollection_Sequence<double> parameters;
              for (const auto i : selected) {
                curves.Append(contours[i][edge]);
                parameters.Append(locationsOnGuide[i]);
              }
              auto law = sectionSurface(curves, parameters);
              for (std::size_t i = 0; i < contours.size(); ++i) {
                if (std::binary_search(selected.begin(), selected.end(), i)) continue;
                for (int j = 0; j <= 8; ++j) {
                  const double u = j / 8.0;
                  const double error = law->Value(u, locationsOnGuide[i]).Distance(
                      contours[i][edge]->Value(u));
                  if (error > worst) { worst = error; split = i; }
                }
              }
              laws.push_back(law);
            }
            if (split == wires_.size()) break;
            if (selected.size() >= 64)
              throw std::runtime_error("Curved stock section changes are too complex for a smooth sweep");
            selected.insert(std::lower_bound(selected.begin(), selected.end(), split), split);
          }
          if (std::getenv("DESIGNRC_SWEEP_DIAGNOSTICS"))
            std::fprintf(stderr, "Validated sweep laws: %zu profiles\n", selected.size());
          BRepBuilderAPI_Sewing sewing{0.02};
          for (const auto& section : laws) {
            checkpoint();
            Handle(Geom_Surface) sweptSurface;
            const auto sectionSurface = section;
            if (!sectionSurface->IsURational() && !sectionSurface->IsVRational()) {
              // With a fixed world frame the sweep is exactly S(u,v)+G(v).
              // Compose compatible B-spline coefficients instead of refitting
              // this sum: the general sweep approximator can report success
              // while missing a steep root-rib orientation transition by mm.
              auto surface = Handle(Geom_BSplineSurface)::DownCast(sectionSurface->Copy());
              auto path = Handle(Geom_BSplineCurve)::DownCast(guide.Curve()->Copy());
              const int degree = std::max(surface->VDegree(), path->Degree());
              surface->IncreaseDegree(surface->UDegree(), degree);
              path->IncreaseDegree(degree);
              surface->InsertVKnots(path->Knots(), path->Multiplicities(), Precision::PConfusion(), false);
              path->InsertKnots(surface->VKnots(), surface->VMultiplicities(), Precision::PConfusion(), false);
              if (surface->NbVPoles() != path->NbPoles())
                throw std::runtime_error("Unable to match curved stock sweep parameters");
              for (int v = 1; v <= surface->NbVPoles(); ++v)
                for (int u = 1; u <= surface->NbUPoles(); ++u)
                  surface->SetPole(u, v, gp_Pnt{surface->Pole(u, v).XYZ() + path->Pole(v).XYZ()});
              sweptSurface = surface;
            } else {
              GeomFill_Sweep sweep{location, false};
              sweep.SetTolerance(0.005, 0.005, 1e-5, 0.01);
              NCollection_Sequence<Handle(Geom_Curve)> referenceCurves;
              NCollection_Sequence<gp_Trsf> transforms;
              NCollection_Sequence<double> parameters;
              for (const double v : {locationsOnGuide.front(), locationsOnGuide.back()}) {
                referenceCurves.Append(section->VIso(v));
                transforms.Append(gp_Trsf{});
                parameters.Append(v);
              }
              Handle(GeomFill_SectionLaw) rationalLaw = new GeomFill_NSections{
                  referenceCurves, transforms, parameters, 0, 1,
                  locationsOnGuide.front(), locationsOnGuide.back(), section};
              sweep.Build(rationalLaw, GeomFill_Location, GeomAbs_C1, 10,
                          std::max(200, guide.Curve()->NbKnots() * 8));
              checkpoint();
              if (!sweep.IsDone() || sweep.ErrorOnSurface() > 0.005)
                throw std::runtime_error("Unable to sweep curved stock contour within tolerance");
              sweptSurface = sweep.Surface();
            }
            sewing.Add(BRepBuilderAPI_MakeFace{sweptSurface, Precision::Confusion()}.Face());
          }
          sewing.Add(BRepBuilderAPI_MakeFace{wires_.front()}.Face());
          sewing.Add(BRepBuilderAPI_MakeFace{wires_.back()}.Face());
          sewing.Perform(indicator->Start());
          checkpoint();
          const auto shell = sewing.SewedShape();
          if (shell.ShapeType() == TopAbs_SHELL && sewing.NbFreeEdges() == 0) {
            auto solid = BRepBuilderAPI_MakeSolid{TopoDS::Shell(shell)}.Solid();
            BRepLib::OrientClosedSolid(solid);
            shape_ = solid;
            swept = BRepCheck_Analyzer{shape_, false}.IsValid();
          }
          if (std::getenv("DESIGNRC_SWEEP_DIAGNOSTICS"))
            std::fprintf(stderr, "Sweep solid valid: %d, free edges: %d\n", swept, sewing.NbFreeEdges());
        }
      } catch (const Standard_Failure& failure) {
        if (std::getenv("DESIGNRC_SWEEP_DIAGNOSTICS"))
          std::fprintf(stderr, "Sweep failure: %s\n", failure.GetMessageString());
        checkpoint();
      }
      if (swept) {
        if (timings_) {
          ++timings_->guideSweeps;
          timings_->maximumSweepProfiles = std::max(timings_->maximumSweepProfiles, selected.size());
        }
      } else {
        // OCCT cannot sweep every section/guide combination (notably abrupt
        // changes in a piecewise outline). Retain a bounded fallback using
        // global contour error, including path curvature as well as twist.
        // Never return to fitting all of the imported sampling stations.
        std::vector<std::size_t> reduced{0};
        const auto reduce = [&](auto&& self, std::size_t first, std::size_t last) -> void {
          checkpoint();
          double error = 0.005;
          std::size_t split = first;
          for (std::size_t i = first + 1; i < last; ++i) {
            const double t = (station[i] - station[first]) / (station[last] - station[first]);
            for (std::size_t j = 0; j < sections[i].size(); ++j) {
              const double deviation = (sections[i][j].XYZ() - sections[first][j].XYZ() * (1 - t) -
                                        sections[last][j].XYZ() * t).Modulus();
              if (deviation > error) { error = deviation; split = i; }
            }
          }
          if (split != first) { self(self, first, split); self(self, split, last); }
          else reduced.push_back(last);
        };
        reduce(reduce, 0, wires_.size() - 1);
        if (reduced.size() > 64)
          throw std::runtime_error("Curved stock cannot be swept within the geometry tolerance; simplify the curve");
        if (std::getenv("DESIGNRC_SWEEP_DIAGNOSTICS"))
          std::fprintf(stderr, "Reduced loft fallback: %zu profiles\n", reduced.size());
        BRepOffsetAPI_ThruSections fallback{true, false, 0.001};
        fallback.CheckCompatibility(false);
        fallback.SetMaxDegree(3);
        fallback.SetContinuity(GeomAbs_C1);
        for (const auto i : reduced) fallback.AddWire(wires_[i]);
        fallback.Build(indicator->Start());
        checkpoint();
        if (!fallback.IsDone()) throw std::runtime_error("Unable to build reduced curved-stock surface");
        shape_ = fallback.Shape();
        if (timings_) ++timings_->reducedLoftFallbacks;
      }
    }
    if (imported_) {
      if (!BRepCheck_Analyzer{shape_, false}.IsValid()) {
        ShapeFix_Shape fix{shape_};
        fix.SetPrecision(1e-4);
        fix.Perform();
        shape_ = fix.Shape();
      }
      if (!BRepCheck_Analyzer{shape_, false}.IsValid())
        throw std::runtime_error("Curved stock sweep produced an invalid solid");
    }
    done_ = true;
  }
  bool IsDone() const { return done_; }
  const TopoDS_Shape& Shape() { return shape_; }

private:
  class SweepProgress final : public Message_ProgressIndicator {
  public:
    explicit SweepProgress(std::function<bool()> cancelled) : cancelled_(std::move(cancelled)) {}
    bool UserBreak() override { return cancelled_ && cancelled_(); }
    void Show(const Message_ProgressScope&, const bool) override {}
  private:
    std::function<bool()> cancelled_;
  };
  void checkpoint() const { if (cancelled_ && cancelled_()) throw GeometryCancelled{}; }
  bool imported_;
  bool done_{false};
  std::function<bool()> cancelled_;
  std::vector<TopoDS_Wire> wires_;
  BRepOffsetAPI_ThruSections loft_;
  TopoDS_Shape shape_;
  PanelBuildTimings* timings_;
};

void addSpanSection(SpanwiseSurface& surface, const TopoDS_Wire& wire,
                    const domain::RibDefinition& rib) { surface.AddWire(wire, &rib); }
void addSpanSection(BRepOffsetAPI_ThruSections& surface, const TopoDS_Wire& wire,
                    const domain::RibDefinition&) { surface.AddWire(wire); }

gp_Pnt transformLocal(const domain::RibDefinition& rib, const domain::Point2 point,
                      const double yOffset = 0.0) {
  const double angle = rib.twistDegrees * std::numbers::pi / 180.0;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  const double planeAngle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
  const auto translation = domain::ribTwistTranslation(rib);
  const double sectionX = cosine * point.x - sine * point.y + translation.x;
  const double sectionZ = sine * point.x + cosine * point.y + translation.y;
  const bool centerRoot = std::abs(rib.spanPosition) < 1.0e-9 &&
      std::abs(rib.ribThicknessStartFactor) < 1.0e-9;
  return {rib.leadingEdgeOffset + sectionX,
          rib.spanPosition - (centerRoot ? 0.0 : std::sin(planeAngle) * sectionZ) +
              std::cos(planeAngle) * yOffset,
          rib.dihedralHeight + (centerRoot ? 1.0 : std::cos(planeAngle)) * sectionZ +
              std::sin(planeAngle) * yOffset};
}

double ribStartOffset(const domain::RibDefinition& rib, const double thickness) {
  if (rib.virtualStation) return 0;
  return rib.ribThicknessStartFactor * thickness;
}

double ribEndOffset(const domain::RibDefinition& rib, const double thickness) {
  if (rib.virtualStation) return 0;
  return (rib.ribThicknessStartFactor + 1.0) * thickness;
}

TopoDS_Wire makeSplineProfileWire(
    const domain::RibDefinition& rib,
    const std::vector<domain::Point2>& profile,
    std::size_t splitIndex, const double yOffset,
    const char* description, const bool diamondNose = false) {
  if (rib.planform && splitIndex < profile.size())
    for (std::size_t i = 1; i < splitIndex; ++i)
      if (std::abs(profile[i].x - profile[splitIndex].x) < 1e-8) {
        splitIndex = i;
        break;
      }
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
    return BRepBuilderAPI_MakeEdge{sectionCurve(points, static_cast<bool>(rib.planform))}.Edge();
  };

  const auto firstPoint = transformLocal(rib, profile.front(), yOffset);
  const auto end = profile.size() - (diamondNose ? 2 : 1);
  const auto lastPoint = transformLocal(rib, profile[end], yOffset);
  BRepBuilderAPI_MakeWire wire;
  wire.Add(makeContourEdge(0, splitIndex));
  wire.Add(makeContourEdge(splitIndex, end));
  if (diamondNose) {
    const auto apex = transformLocal(rib, profile.back(), yOffset);
    wire.Add(BRepBuilderAPI_MakeEdge{lastPoint, apex}.Edge());
    wire.Add(BRepBuilderAPI_MakeEdge{apex, firstPoint}.Edge());
  } else {
    wire.Add(BRepBuilderAPI_MakeEdge{lastPoint, firstPoint}.Edge());
  }
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
                                       const double ribThickness,
                                       const std::function<bool()>& isCancelled,
                                       PanelBuildTimings* timings) {
  if (member.centers.size() != wing.ribs.size())
    throw std::runtime_error("Rectangular member centers do not match the rib stations");
  SpanwiseSurface loft{static_cast<bool>(wing.ribs.front().rib.planform), isCancelled, timings};
  loft.CheckCompatibility(false);
  const auto addProfile = [&](const std::size_t i, const double yOffset) {
    const auto& center = member.centers[i];
    BRepBuilderAPI_MakePolygon polygon;
    polygon.Add(transformLocal(wing.ribs[i].rib,
                               {center.x - member.width * 0.5, center.y - member.height * 0.5},
                               yOffset));
    polygon.Add(transformLocal(wing.ribs[i].rib,
                               {center.x + member.width * 0.5, center.y - member.height * 0.5},
                               yOffset));
    polygon.Add(transformLocal(wing.ribs[i].rib,
                               {center.x + member.width * 0.5, center.y + member.height * 0.5},
                               yOffset));
    polygon.Add(transformLocal(wing.ribs[i].rib,
                               {center.x - member.width * 0.5, center.y + member.height * 0.5},
                               yOffset));
    polygon.Close();
    if (!polygon.IsDone())
      throw std::runtime_error("Unable to construct rectangular member profile");
    loft.AddWire(polygon.Wire(), &wing.ribs[i].rib);
  };
  addProfile(0, ribStartOffset(wing.ribs[0].rib, ribThickness));
  if (wing.ribs.front().rib.planform) {
    for (std::size_t i = 1; i + 1 < wing.ribs.size(); ++i)
      addProfile(i, 0);
    addProfile(wing.ribs.size() - 1, ribEndOffset(wing.ribs.back().rib, ribThickness));
  } else {
    for (std::size_t i = 0; i < wing.ribs.size(); ++i) {
      if (!wing.ribs[i].rib.virtualStation)
        addProfile(i, ribEndOffset(wing.ribs[i].rib, ribThickness));
      if (i + 1 < wing.ribs.size())
        addProfile(i + 1, ribStartOffset(wing.ribs[i + 1].rib, ribThickness));
    }
  }
  loft.Build();
  if (!loft.IsDone()) throw std::runtime_error("Unable to loft rectangular span member");
  return loft.Shape();
}

} // namespace

std::size_t ribGeometryWorkerCount(const std::size_t ribCount, const std::size_t maximumWorkers) {
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
                                        const std::size_t maximumRibWorkers,
                                        const std::function<bool()>& isCancelled) {
  const auto checkpoint = [&] {
    if (isCancelled && isCancelled()) throw GeometryCancelled{};
  };
  checkpoint();
  class CancellationProgress final : public Message_ProgressIndicator {
  public:
    explicit CancellationProgress(std::function<bool()> cancelled)
        : cancelled_{std::move(cancelled)} {}
  protected:
    bool UserBreak() override { return cancelled_ && cancelled_(); }
    void Show(const Message_ProgressScope&, const bool) override {}
  private:
    std::function<bool()> cancelled_;
  };
  const auto cutOpening = [&](const TopoDS_Shape& stock, const TopoDS_Shape& tool) {
    checkpoint();
    Handle(Message_ProgressIndicator) indicator = new CancellationProgress{isCancelled};
    // The two-shape constructor performs the cut immediately. Supply its
    // progress range there, rather than running an uncancellable first cut.
    BRepAlgoAPI_Cut cut{stock, tool, indicator->Start()};
    checkpoint();
    if (!cut.IsDone()) throw std::runtime_error("Unable to cut a rib opening");
    return cut.Shape();
  };
  const auto meshRib = [&](const TopoDS_Shape& shape) {
    checkpoint();
    IMeshTools_Parameters parameters;
    parameters.Deflection = 0.75;
    parameters.Angle = 0.35;
    parameters.Relative = false;
    parameters.InParallel = true;
    Handle(Message_ProgressIndicator) indicator = new CancellationProgress{isCancelled};
    BRepMesh_IncrementalMesh mesh{shape, parameters, indicator->Start()};
    checkpoint();
    if (!mesh.IsDone()) throw std::runtime_error("Unable to create a shaded rib mesh");
  };
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
      const bool curveCollision = structuredWing.ribs.front().rib.planform != nullptr &&
                                  !(isSpoilerPart(name) && isSpoilerPart(other.name));
      const bool newSparCollision = isNewSparPart(name) || isNewSparPart(other.name);
      const bool spoilerCollision =
          (isSpoilerPart(name) || isSpoilerPart(other.name)) &&
          !(isSpoilerPart(name) && isSpoilerPart(other.name));
      if (isSheetingPart(name) || isSheetingPart(other.name) ||
          ((isWoodJoiner(name) || isWoodJoiner(other.name)) && !woodJoinerCollision &&
           !spoilerCollision) ||
          intendedWebContact ||
          (!newSparCollision && !joinerCollision && !spoilerCollision && !curveCollision) ||
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
    checkpoint();
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
          checkpoint();
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
          finished = cutOpening(finished, holeTool.Shape());
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
        checkpoint();
        BRepBuilderAPI_MakePolygon cutPolygon;
        for (const auto& point : opening)
          cutPolygon.Add(transformLocal(
              structured.rib, point,
              ribStartOffset(structured.rib, ribThickness) - 1.0));
        cutPolygon.Close();
        BRepBuilderAPI_MakeFace cutFace{cutPolygon.Wire()};
        BRepPrimAPI_MakePrism cutTool{
            cutFace.Face(), ribNormal * (ribThickness + 2.0)};
        finished = cutOpening(finished, cutTool.Shape());
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
        checkpoint();
        BRepBuilderAPI_MakePolygon cutPolygon;
        for (const auto& point : cutout)
          cutPolygon.Add(transformLocal(
              structured.rib, point,
              ribStartOffset(structured.rib, ribThickness) - 1.0));
        cutPolygon.Close();
        BRepBuilderAPI_MakeFace cutFace{cutPolygon.Wire()};
        BRepPrimAPI_MakePrism cutTool{
            cutFace.Face(), ribNormal * (ribThickness + 2.0)};
        finished = cutOpening(finished, cutTool.Shape());
        TopExp_Explorer cutSolids{finished, TopAbs_SOLID};
        if (!cutSolids.More())
          throw std::runtime_error("Wood joiner cut removed the rib solid");
        if (!BRepCheck_Analyzer{finished, false}.IsValid()) {
          ShapeFix_Shape fixer{finished};
          fixer.Perform();
          finished = fixer.Shape();
        }
      }

      for (const auto& plane : {structured.buildPlane, structured.topBuildPlane}) {
        if (!plane) continue;
        const auto& support = *plane;
        gp_Trsf align;
        align.SetRotation(gp_Ax1{gp_Pnt{0, 0, 0}, gp_Dir{1, 0, 0}},
            std::atan2(support.spanNormal, support.verticalNormal));
        const auto minimumHeight = [&] {
          Bnd_Box bounds;
          BRepBndLib::AddOptimal(BRepBuilderAPI_Transform{finished, align}.Shape(),
                                bounds, false, false);
          double x0, y0, z0, x1, y1, z1;
          bounds.Get(x0, y0, z0, x1, y1, z1);
          return z0;
        };
        if (minimumHeight() < support.offset - 1.0e-7) {
          const gp_Dir normal{0.0, support.spanNormal, support.verticalNormal};
          const gp_Pnt origin{0.0, support.spanNormal * support.offset,
                              support.verticalNormal * support.offset};
          const auto cutter = BRepPrimAPI_MakeHalfSpace{
              BRepBuilderAPI_MakeFace{gp_Pln{origin, normal}}.Face(),
              origin.Translated(-gp_Vec{normal})}.Solid();
          BRepAlgoAPI_Cut cut{finished, cutter};
          if (!cut.IsDone()) throw std::runtime_error("Unable to trim rib to build plane");
          finished = cut.Shape();
          if (finished.IsNull() || minimumHeight() < support.offset - 1.0e-6)
            throw std::runtime_error("Finished rib extends below its build plane");
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
            const auto pcurve = GCE2d_MakeSegment{first, second}.Value();
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
    checkpoint();
    BRepTools::Clean(positiveRibShape);
    meshRib(positiveRibShape);
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
      checkpoint();
      BRepTools::Clean(negativeRibShape);
      meshRib(negativeRibShape);
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
        checkpoint();
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
  checkpoint();
  for (const auto& ribParts : builtRibs)
    for (const auto& ribPart : ribParts)
      addShape(ribPart.shape, PartMaterial::Wood,
               ribPart.mirrorInAssembly, ribPart.name);
  if (timings) timings->ribsMs = elapsedMs(stageStart);

  stageStart = std::chrono::steady_clock::now();
  if (progress)
    progress(40, "Building leading and trailing edge stock");
  const auto& spanWing = structuredWing.surfaceWing ? *structuredWing.surfaceWing : structuredWing;
  for (const auto& member : spanWing.profiledMembers) {
    if (member.profiles.size() != spanWing.ribs.size())
      throw std::runtime_error("Edge stock profiles do not match the rib stations");
    auto ranges = member.activeRanges;
    if (ranges.empty()) ranges.emplace_back(0, member.profiles.size() - 1);
    const bool splineTrailingEdge =
        member.name.starts_with("TE") ||
        member.name.find("trailing edge") != std::string::npos;
    for (const auto [first, last] : ranges) {
      SpanwiseSurface loft{static_cast<bool>(spanWing.ribs.front().rib.planform), isCancelled, timings};
      loft.CheckCompatibility(false);
      const auto addProfile = [&](const std::size_t i, const double yOffset) {
        if (!member.profileSegments.empty()) {
          BRepBuilderAPI_MakeWire wire;
          for (const auto& segment : member.profileSegments.at(i)) {
            if (segment.spline && segment.points.size() > 2) {
              const auto points = Handle(OcctPointArray){new OcctPointArray{1, static_cast<int>(segment.points.size())}};
              for (std::size_t j = 0; j < segment.points.size(); ++j)
                points->SetValue(static_cast<int>(j + 1),
                                 transformLocal(spanWing.ribs[i].rib, segment.points[j], yOffset));
              wire.Add(BRepBuilderAPI_MakeEdge{
                  sectionCurve(points, static_cast<bool>(spanWing.ribs[i].rib.planform))}.Edge());
            } else {
              for (std::size_t j = 1; j < segment.points.size(); ++j)
                wire.Add(BRepBuilderAPI_MakeEdge{
                    transformLocal(spanWing.ribs[i].rib, segment.points[j - 1], yOffset),
                    transformLocal(spanWing.ribs[i].rib, segment.points[j], yOffset)}
                             .Edge());
            }
          }
          if (!wire.IsDone()) throw std::runtime_error("Unable to construct leading-edge section");
          loft.AddWire(wire.Wire(), &spanWing.ribs[i].rib);
          return;
        }
        if (splineTrailingEdge) {
          const auto& profile = member.profiles[i];
          const auto trailing = std::max_element(
              profile.begin(), profile.end(),
              [](const domain::Point2& left, const domain::Point2& right) {
                return left.x < right.x;
              });
          const std::size_t trailingIndex = static_cast<std::size_t>(
              std::distance(profile.begin(), trailing));
          loft.AddWire(makeSplineProfileWire(spanWing.ribs[i].rib, profile, trailingIndex, yOffset,
                                             "trailing-edge"), &spanWing.ribs[i].rib);
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
        loft.AddWire(makeSplineProfileWire(spanWing.ribs[i].rib, profile, leadingIndex, yOffset,
                                           "leading-edge", member.diamondNose), &spanWing.ribs[i].rib);
      };
      // Imported stock retains intermediate shape constraints; SpanwiseSurface
      // removes them only when the endpoint loft reproduces those sections.
      addProfile(first, ribStartOffset(spanWing.ribs[first].rib, ribThickness));
      if (spanWing.ribs.front().rib.planform || member.diamondNose ||
          !member.profileSegments.empty())
        for (std::size_t i = first + 1; i < last; ++i) addProfile(i, 0.0);
      addProfile(last, ribEndOffset(spanWing.ribs[last].rib, ribThickness));
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
  for (const auto& flap : spanWing.controlSurfaces) {
    if (flap.name != "Flap") continue;
    for (const auto& aileron : spanWing.controlSurfaces) {
      if (aileron.name == "Aileron" &&
          flap.stopRibIndex == aileron.startRibIndex) {
        sharedFlap = &flap;
        sharedAileron = &aileron;
      }
    }
  }
  for (const auto& control : spanWing.controlSurfaces) {
    SpanwiseSurface loft{static_cast<bool>(spanWing.ribs.front().rib.planform), isCancelled, timings};
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
      loft.AddWire(makeSplineProfileWire(spanWing.ribs[ribIndex].rib, profile, trailingIndex,
                                         yOffset, "control-surface"), &spanWing.ribs[ribIndex].rib);
    };
    addControlProfile(0, ribEndOffset(spanWing.ribs[control.startRibIndex].rib, ribThickness) +
                             control.gap);
    if (spanWing.ribs.front().rib.planform)
      for (std::size_t i = 1; i + 1 < control.profiles.size(); ++i)
        addControlProfile(i, 0);
    const auto& stopRib = spanWing.ribs[control.stopRibIndex].rib;
    addControlProfile(control.profiles.size() - 1,
        control.extendThroughStopRib
            ? ribEndOffset(stopRib, ribThickness)
            : ribStartOffset(stopRib, ribThickness) - control.gap);
    loft.Build();
    if (!loft.IsDone()) throw std::runtime_error("Unable to loft the control surface");
    addPartShape(control.name, loft.Shape(), PartMaterial::Wood);

    if (!spanWing.ribs.front().rib.planform &&
        (&control == sharedFlap || &control == sharedAileron))
      continue;
    const auto hingeStart =
        transformLocal(spanWing.ribs[control.startRibIndex].rib, control.hingePostCenters.front(),
                       ribEndOffset(spanWing.ribs[control.startRibIndex].rib, ribThickness));
    const auto hingeEnd =
        transformLocal(spanWing.ribs[control.stopRibIndex].rib, control.hingePostCenters.back(),
                       control.extendThroughStopRib
                           ? ribEndOffset(spanWing.ribs[control.stopRibIndex].rib, ribThickness)
                           : ribStartOffset(spanWing.ribs[control.stopRibIndex].rib, ribThickness));
    addPartShape(control.name + " hinge post", makeRectangularSegment(
        hingeStart, hingeEnd, control.hingePostWidth, control.hingePostHeight),
        PartMaterial::Wood);
  }
  if (!spanWing.ribs.front().rib.planform && sharedFlap != nullptr && sharedAileron != nullptr) {
    const auto hingeStart = transformLocal(
        spanWing.ribs[sharedFlap->startRibIndex].rib, sharedFlap->hingePostCenters.front(),
        ribEndOffset(spanWing.ribs[sharedFlap->startRibIndex].rib, ribThickness));
    const auto hingeEnd = transformLocal(
        spanWing.ribs[sharedAileron->stopRibIndex].rib, sharedAileron->hingePostCenters.back(),
        sharedAileron->extendThroughStopRib
            ? ribEndOffset(spanWing.ribs[sharedAileron->stopRibIndex].rib, ribThickness)
            : ribStartOffset(spanWing.ribs[sharedAileron->stopRibIndex].rib, ribThickness));
    addPartShape("Flap/Aileron hinge post", makeRectangularSegment(
        hingeStart, hingeEnd, sharedFlap->hingePostWidth,
        sharedFlap->hingePostHeight), PartMaterial::Wood);
  }
  if (timings) timings->controlsMs = elapsedMs(stageStart);

  const auto buildMemberShape = [&](const domain::SpanMember& member) {
    const bool circular =
        member.kind == domain::SpanMemberKind::Tube || member.kind == domain::SpanMemberKind::Rod;
    const bool endpointDefinedSpar = member.name.starts_with("Spar ");
    if (circular && spanWing.ribs.front().rib.planform &&
        (member.name.starts_with("LE") || member.name.find("leading edge") != std::string::npos)) {
      const auto tubeSolid = [&](double diameter) {
        SpanwiseSurface loft{true, isCancelled, timings};
        loft.CheckCompatibility(false);
        for (std::size_t i = 0; i < spanWing.ribs.size(); ++i) {
          const auto& rib = spanWing.ribs[i].rib;
          const double offset = i == 0                          ? ribStartOffset(rib, ribThickness)
                                : i + 1 == spanWing.ribs.size() ? ribEndOffset(rib, ribThickness)
                                                                : 0;
          const double angle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180;
          const gp_Ax2 plane{transformLocal(rib, member.centers[i], offset),
                             gp_Dir{0, std::cos(angle), std::sin(angle)}, gp_Dir{1, 0, 0}};
          loft.AddWire(BRepBuilderAPI_MakeWire{
              BRepBuilderAPI_MakeEdge{gp_Circ{plane, diameter * 0.5}}.Edge()}
                           .Wire(), &rib);
        }
        loft.Build();
        if (!loft.IsDone()) throw std::runtime_error("Unable to build curved carbon LE");
        return loft.Shape();
      };
      auto shape = tubeSolid(member.width);
      if (member.kind == domain::SpanMemberKind::Tube && member.innerDiameter > 0) {
        BRepAlgoAPI_Cut cut{shape, tubeSolid(member.innerDiameter)};
        if (!cut.IsDone()) throw std::runtime_error("Unable to hollow curved CF tube LE");
        shape = cut.Shape();
        if (!BRepCheck_Analyzer{shape, false}.IsValid()) {
          ShapeFix_Shape fix{shape};
          fix.SetPrecision(0.001);
          fix.Perform();
          shape = fix.Shape();
        }
        if (!BRepCheck_Analyzer{shape, false}.IsValid())
          throw std::runtime_error("Unable to create a valid hollow curved CF tube LE");
      }
      return shape;
    }
    if (!circular && !endpointDefinedSpar)
      return makeRectangularSpanMember(spanWing, member, ribThickness, isCancelled, timings);
    const auto start = transformLocal(spanWing.ribs.front().rib, member.centers.front());
    const auto end = transformLocal(spanWing.ribs.back().rib, member.centers.back());
    gp_Pnt extendedStart = start;
    gp_Pnt extendedEnd = end;
    const gp_Vec axis{start, end};
    const auto boundaryPlane = [&](const domain::RibDefinition& rib,
                                   const double offset) {
      const bool centerRoot = std::abs(rib.spanPosition) < 1.0e-9 &&
          std::abs(rib.ribThicknessStartFactor) < 1.0e-9;
      const double angle = centerRoot ? 0.0 :
          rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
      return gp_Pln{transformLocal(rib, {0.0, 0.0}, offset),
                    gp_Dir{0.0, std::cos(angle), std::sin(angle)}};
    };
    const auto& rootRib = spanWing.ribs.front().rib;
    const auto& tipRib = spanWing.ribs.back().rib;
    const auto rootPlane = boundaryPlane(rootRib, ribStartOffset(rootRib, ribThickness));
    const auto tipPlane = boundaryPlane(tipRib, ribEndOffset(tipRib, ribThickness));
    const gp_Vec direction = axis.Normalized();
    const double projection = std::min(
        direction.Dot(gp_Vec{rootPlane.Axis().Direction()}),
        direction.Dot(gp_Vec{tipPlane.Axis().Direction()}));
    if (projection <= Precision::Confusion())
      throw std::runtime_error("Spar axis does not cross the panel end rib planes");
    // Overbuild enough stock for the entire cross-section to reach both end
    // planes, then miter it to the actual outer rib faces.
    const gp_Vec extension = direction *
        ((ribThickness + member.width + member.height) / projection);
    extendedStart.Translate(-extension);
    extendedEnd.Translate(extension);
    auto shape = !circular ? makeRectangularSegment(
        extendedStart, extendedEnd, member.width, member.height) :
        makeTubeSegment(extendedStart, extendedEnd, member.width,
        member.kind == domain::SpanMemberKind::Tube ? member.innerDiameter : 0.0);
    const auto trimEnd = [&](const gp_Pln& plane, const double outsideSign) {
      const auto outside = plane.Location().Translated(
          gp_Vec{plane.Axis().Direction()} * outsideSign);
      const auto cutter = BRepPrimAPI_MakeHalfSpace{
          BRepBuilderAPI_MakeFace{plane}.Face(), outside}.Solid();
      BRepAlgoAPI_Cut cut{shape, cutter};
      if (!cut.IsDone())
        throw std::runtime_error("Unable to trim " + member.name + " to the end rib face");
      shape = cut.Shape();
    };
    trimEnd(rootPlane, -1.0);
    trimEnd(tipPlane, 1.0);
    TopExp_Explorer solids{shape, TopAbs_SOLID};
    if (!solids.More()) throw std::runtime_error("End rib trimming removed " + member.name);
    const auto solid = solids.Current();
    solids.Next();
    return solids.More() ? shape : solid;
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
        polygon.Add(transformLocal(spanWing.ribs[ribIndex].rib, point, offset));
      polygon.Close();
      loft.AddWire(polygon.Wire());
    };
    addProfile(0, part.spansCenter
                      ? 0.0
                      : ribEndOffset(spanWing.ribs[part.startRibIndex].rib, ribThickness) +
                            startExtra);
    addProfile(profiles.size() - 1,
               ribStartOffset(spanWing.ribs[part.endRibIndex].rib, ribThickness) - endExtra);
    loft.Build();
    if (!loft.IsDone()) throw std::runtime_error("Unable to loft spoiler assembly part");
    return loft.Shape();
  };
  for (const auto& spoiler : spanWing.spoilers) {
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
        auto modelPoint =
            transformLocal(spanWing.ribs[spoiler.startRibIndex + local].rib, localPoint, offset);
        if (mirrored) modelPoint.Transform(mirror);
        return modelPoint;
      };
      const double rootOffset =
          spoiler.spansCenter
              ? 0.0
              : ribEndOffset(spanWing.ribs[spoiler.startRibIndex].rib, ribThickness) + endGap;
      const double endOffset =
          ribStartOffset(spanWing.ribs[spoiler.endRibIndex].rib, ribThickness) - endGap;
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
          auto modelPoint = transformLocal(spanWing.ribs[ribIndex].rib, point, offset);
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
          return ribStartOffset(spanWing.ribs[spoiler.endRibIndex].rib, ribThickness) - endGap;
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
      const double offset = end == 0 ? ribEndOffset(spanWing.ribs[ribIndex].rib, ribThickness)
                                     : ribStartOffset(spanWing.ribs[ribIndex].rib, ribThickness) -
                                           spoiler.frameRailWidth;
      for (const auto& point : support)
        polygon.Add(transformLocal(spanWing.ribs[ribIndex].rib, point, offset));
      polygon.Close();
      BRepBuilderAPI_MakeFace face{polygon.Wire()};
      const double plane =
          spanWing.ribs[ribIndex].rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
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
  for (const auto& member : spanWing.members) {
    if (member.cutsSheeting)
      sheetingCutters.push_back(
          {member.verticalLocation, member.name, buildMemberShape(member)});
  }
  for (const auto& spoiler : spanWing.spoilers) {
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
  for (const auto& sheet : spanWing.sheeting) {
    if (sheet.profiles.size() != sheet.stopRibIndex + 1)
      throw std::runtime_error("Sheeting profiles do not match their rib stations");
    const auto addProfile = [&](auto& loft,
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
          points->SetValue(static_cast<int>(point - begin + 1),
                           transformLocal(spanWing.ribs[i].rib, profile[point], yOffset));
        return BRepBuilderAPI_MakeEdge{
            sectionCurve(points, static_cast<bool>(spanWing.ribs[i].rib.planform))}.Edge();
      };
      const auto outerEnd = transformLocal(spanWing.ribs[i].rib, profile[contourSize - 1], yOffset);
      const auto innerStart = transformLocal(spanWing.ribs[i].rib, profile[contourSize], yOffset);
      const auto innerEnd = transformLocal(spanWing.ribs[i].rib, profile.back(), yOffset);
      const auto outerStart = transformLocal(spanWing.ribs[i].rib, profile.front(), yOffset);
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
      addSpanSection(loft, wire.Wire(), spanWing.ribs[i].rib);
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
        if (!spanWing.ribs[i].rib.virtualStation)
          addSegment(sheet.profiles[i], i, ribStartOffset(spanWing.ribs[i].rib, ribThickness),
                     sheet.profiles[i], i, ribEndOffset(spanWing.ribs[i].rib, ribThickness));
      for (std::size_t bay = 0; bay < sheet.stopRibIndex; ++bay) {
        const auto& bayProfiles =
            sheet.controlBays[bay] ? sheet.controlProfiles : sheet.fullProfiles;
        if (spanWing.ribs.front().rib.planform) {
          std::size_t end = bay + 1;
          while (end < sheet.stopRibIndex && spanWing.ribs[end].rib.virtualStation &&
                 sheet.controlBays[end] == sheet.controlBays[bay])
            ++end;
          SpanwiseSurface loft{true, isCancelled, timings};
          addProfile(loft, bayProfiles[bay], bay,
                     ribEndOffset(spanWing.ribs[bay].rib, ribThickness));
          for (std::size_t i = bay + 1; i < end; ++i)
            addProfile(loft, bayProfiles[i], i, 0);
          addProfile(loft, bayProfiles[end], end,
                     ribStartOffset(spanWing.ribs[end].rib, ribThickness));
          loft.Build();
          if (!loft.IsDone())
            throw std::runtime_error("Unable to loft curved control-bay sheeting");
          addPartShape(sheet.name, cutSheeting(sheet.name, loft.Shape()), PartMaterial::Wood);
          bay = end - 1;
          continue;
        }
        addSegment(bayProfiles[bay], bay, ribEndOffset(spanWing.ribs[bay].rib, ribThickness),
                   bayProfiles[bay + 1], bay + 1,
                   ribStartOffset(spanWing.ribs[bay + 1].rib, ribThickness));
      }
    } else {
      SpanwiseSurface loft{static_cast<bool>(spanWing.ribs.front().rib.planform), isCancelled, timings};
      loft.CheckCompatibility(false);
      addProfile(loft, sheet.profiles[0], 0, ribStartOffset(spanWing.ribs[0].rib, ribThickness));
      if (spanWing.ribs.front().rib.planform) {
        for (std::size_t i = 1; i < sheet.stopRibIndex; ++i)
          addProfile(loft, sheet.profiles[i], i, 0);
        addProfile(loft, sheet.profiles[sheet.stopRibIndex], sheet.stopRibIndex,
                   ribEndOffset(spanWing.ribs[sheet.stopRibIndex].rib, ribThickness));
      } else {
        for (std::size_t i = 0; i <= sheet.stopRibIndex; ++i) {
          if (!spanWing.ribs[i].rib.virtualStation)
            addProfile(loft, sheet.profiles[i], i,
                       ribEndOffset(spanWing.ribs[i].rib, ribThickness));
          if (i < sheet.stopRibIndex)
            addProfile(loft, sheet.profiles[i + 1], i + 1,
                       ribStartOffset(spanWing.ribs[i + 1].rib, ribThickness));
        }
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
  for (const auto& member : spanWing.members) {
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

  // Caps follow the rib surface, extruded along its normal. Trim their full
  // width against the completed structure: a swept spar or a spoiler rail
  // can enter a cap beside the rib even when the center profile is clear.
  const auto capStageStart = std::chrono::steady_clock::now();
  std::vector<std::vector<std::size_t>> capsByRib(structuredWing.ribs.size());
  for (std::size_t i = 0; i < structuredWing.ribCaps.size(); ++i)
    capsByRib.at(structuredWing.ribCaps[i].ribIndex).push_back(i);
  std::vector<TopoDS_Shape> capShapes(structuredWing.ribCaps.size());
  std::atomic_size_t completedCapRibs{0};
  const auto buildCap = [&](const std::size_t capIndex) {
    const auto& cap = structuredWing.ribCaps[capIndex];
    const auto& rib = structuredWing.ribs.at(cap.ribIndex).rib;
    const double startOffset = cap.ribIndex == 0
        ? std::max(cap.startOffset, ribStartOffset(rib, ribThickness)) : cap.startOffset;
    const double endOffset = cap.ribIndex + 1 == structuredWing.ribs.size()
        ? std::min(cap.endOffset, ribEndOffset(rib, ribThickness)) : cap.endOffset;
    if (cap.profile.size() < 4 || cap.profile.size() % 2 != 0 || endOffset <= startOffset)
      throw std::runtime_error("Invalid rib cap profile: " + cap.name);
    const auto edge = [&](const std::size_t begin, const std::size_t end) {
      if (end == begin + 1)
        return BRepBuilderAPI_MakeEdge{
            transformLocal(rib, cap.profile[begin], startOffset),
            transformLocal(rib, cap.profile[end], startOffset)}.Edge();
      const auto points = Handle(OcctPointArray){new OcctPointArray{1, static_cast<int>(end - begin + 1)}};
      for (std::size_t i = begin; i <= end; ++i)
        points->SetValue(static_cast<int>(i - begin + 1),
            transformLocal(rib, cap.profile[i], startOffset));
      GeomAPI_Interpolate interpolation{points, false, Precision::Confusion()};
      interpolation.Perform();
      if (!interpolation.IsDone()) throw std::runtime_error("Unable to interpolate " + cap.name);
      return BRepBuilderAPI_MakeEdge{interpolation.Curve()}.Edge();
    };
    const auto half = cap.profile.size() / 2;
    BRepBuilderAPI_MakeWire wire;
    wire.Add(edge(0, half - 1));
    wire.Add(edge(half - 1, half));
    wire.Add(edge(half, cap.profile.size() - 1));
    wire.Add(BRepBuilderAPI_MakeEdge{
        transformLocal(rib, cap.profile.back(), startOffset),
        transformLocal(rib, cap.profile.front(), startOffset)}.Edge());
    if (!wire.IsDone()) throw std::runtime_error("Unable to construct " + cap.name);
    BRepBuilderAPI_MakeFace face{wire.Wire()};
    if (!face.IsDone()) throw std::runtime_error("Unable to fill " + cap.name);
    const gp_Vec extrusion{transformLocal(rib, cap.profile.front(), startOffset),
                           transformLocal(rib, cap.profile.front(), endOffset)};
    TopoDS_Shape shape = BRepPrimAPI_MakePrism{face.Face(), extrusion}.Shape();
    const auto& ribName = structuredWing.ribs[cap.ribIndex].name;
    const std::string ribLabel = ribName.empty() ? "Rib " + std::to_string(cap.ribIndex + 1) :
        ribName.starts_with("R") && !ribName.starts_with("Rib") ? "Rib " + ribName.substr(1) : ribName;
    if (progress)
      progress(90 + static_cast<int>(3 * completedCapRibs.load() / structuredWing.ribs.size()),
          "Checking rib cap collisions for " + ribLabel +
          (cap.top ? " (Top; " : " (Bottom; ") +
          std::to_string(completedCapRibs.load()) + "/" +
          std::to_string(structuredWing.ribs.size()) + " ribs complete)");
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    TopTools_ListOfShape cutters;
    for (const auto& other : nonRibShapes) {
      if (bounds.IsOut(other.bounds)) continue;
      cutters.Append(other.shape);
    }
    if (!cutters.IsEmpty()) {
      TopTools_ListOfShape arguments;
      arguments.Append(shape);
      BRepAlgoAPI_Cut cut;
      cut.SetArguments(arguments);
      cut.SetTools(cutters);
      // Different rib workers share read-only obstacle shapes. OCCT must
      // copy any topology it needs to alter instead of mutating those inputs.
      cut.SetNonDestructive(true);
      cut.SetRunParallel(false);
      cut.Build();
      if (!cut.IsDone()) throw std::runtime_error("Unable to trim " + cap.name + " around the wing structure");
      shape = cut.Shape();
    }
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    if (properties.Mass() > 1.0e-6)
      capShapes[capIndex] = shape;
  };
  if (!structuredWing.ribCaps.empty()) {
    std::atomic_size_t nextCapRib{0};
    std::vector<std::future<void>> capWorkers;
    const auto count = ribGeometryWorkerCount(capsByRib.size(), maximumRibWorkers);
    for (std::size_t worker = 0; worker < count; ++worker)
      capWorkers.push_back(std::async(std::launch::async, [&] {
        for (;;) {
          const auto ribIndex = nextCapRib.fetch_add(1);
          if (ribIndex >= capsByRib.size()) return;
          for (const auto capIndex : capsByRib[ribIndex]) buildCap(capIndex);
          ++completedCapRibs;
        }
      }));
    for (auto& worker : capWorkers) worker.get();
    if (progress)
      progress(94, "Rib cap collision checks complete (" +
          std::to_string(completedCapRibs.load()) + "/" +
          std::to_string(capsByRib.size()) + " ribs complete)");
    // Keep compound mutation and STEP ordering deterministic. Caps have
    // already been cut against every obstacle, so do not repeat the generic
    // spar/part Boolean collision pass when registering the finished caps.
    for (std::size_t i = 0; i < capShapes.size(); ++i)
      if (!capShapes[i].IsNull())
        addShape(capShapes[i], PartMaterial::Wood, true, structuredWing.ribCaps[i].name);
  }
  if (timings) timings->ribCapsMs = elapsedMs(capStageStart);

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
