#include "TestWingStructure.h"
#include "domain/AirfoilProfile.h"
#include "domain/DxfExporter.h"
#include "domain/WingDesign.h"
#include "domain/WingStructure.h"
#include "geometry/OcctRibBuilder.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <QFile>
#include <QTemporaryDir>

int runTest(int argc, char* argv[]) {
  using designrc::domain::AirfoilProfile;
  if (argc > 1 && std::string{argv[1]} == "--control-direction") {
    using namespace designrc::domain;
    for (bool curved : {false, true}) for (bool flap : {false, true}) for (int construction = 0; construction < 3; ++construction) {
      std::cerr << "Perpendicular control: curved=" << curved << " flap=" << flap
                << " construction=" << construction << '\n';
      WingParameters panel;
      panel.halfSpan = 160; panel.rootChord = 200; panel.tipChord = 180;
      panel.sweep = 40; panel.ribCount = 7; panel.ribThickness = 2; panel.dihedralDegrees = 0;
      if (curved) {
        panel.planform = {{{0, 0}, {4, .5}, {15, 1}}, {{200, 0}, {215, .5}, {205, 1}}, 160, 160};
        panel.tipTwistDegrees = 2;
      }
      const auto foil = AirfoilProfile::nacaSymmetric(.15);
      const auto ribs = generateRibs(panel, foil, foil);
      StructureParameters settings;
      settings.ribThickness = 2;
      settings.ailerons = !flap; settings.flaps = flap;
      settings.aileronStartRib = settings.flapStartRib = 2;
      settings.aileronStopRib = 7; settings.flapStopRib = 6;
      settings.aileronWidth = settings.flapWidth = 40;
      settings.aileronHingePostHeight = settings.flapHingePostHeight = 40;
      settings.aileronPerpendicularToRibs = settings.flapPerpendicularToRibs = true;
      auto& options = flap ? settings.builtUpFlap : settings.builtUpAileron;
      options.enabled = construction != 0; options.hingeSpars = construction == 2;
      options.hingeSparWidth = 3; options.hingeSparHeight = 1;
      options.topSheeting = options.bottomSheeting = true;
      options.topSheetingThickness = options.bottomSheetingThickness = .5;
      const auto wing = testsupport::buildStructure(ribs, settings);
      const auto& control = wing.controlSurfaces.front();
      assert(control.perpendicularToRibs && std::abs(control.hingeRootX - control.hingeTipX) < 1e-8);
      const auto& root = ribs[control.startRibIndex];
      assert(std::abs(root.leadingEdgeOffset + root.chord - control.hingeRootX - 40) < 1e-8);
      for (std::size_t i = 0; i < control.profiles.size(); ++i) {
        const auto first = std::min_element(control.profiles[i].begin(), control.profiles[i].end(),
            [](Point2 a, Point2 b) { return a.x < b.x; });
        assert(std::abs(first->x + ribs[control.startRibIndex + i].leadingEdgeOffset - control.hingeRootX) < 1e-7);
      }
      designrc::geometry::MaterialShapeSet parts;
      for (std::size_t r = 0; r < wing.ribs.size(); ++r)
        for (const auto& segment : wing.ribs[r].outlineSegments)
          for (std::size_t i = 1; i < segment.points.size(); ++i)
            assert(std::hypot(segment.points[i].x - segment.points[i-1].x,
                              segment.points[i].y - segment.points[i-1].y) >= 1e-7);
      std::string lastStage;
      TopoDS_Shape shape;
      try {
        shape = designrc::geometry::buildStructuredWingPreview(wing, 2, nullptr, &parts,
            [&](int, const std::string& stage) { lastStage = stage; }, 1);
      } catch (...) {
        std::cerr << "Failed during " << lastStage << '\n';
        throw;
      }
      assert(!shape.IsNull() && !parts.parts.empty());
      for (const auto& part : parts.parts)
        if (!BRepCheck_Analyzer{part.shape, false}.IsValid())
          throw std::runtime_error("Invalid perpendicular control component: " + part.name);
    }
    {
      WingParameters tapered;
      tapered.rootChord = 200; tapered.tipChord = 140; tapered.sweep = 30;
      tapered.halfSpan = 180; tapered.ribCount = 7; tapered.dihedralDegrees = 0;
      const auto foil = AirfoilProfile::nacaSymmetric(.15);
      const auto ribs = generateRibs(tapered, foil, foil);
      SparParameters spar;
      spar.material = 0; spar.verticalLocation = 0; spar.woodWidth = 4; spar.woodHeight = 2;
      spar.chordLocationPercent = spar.tipChordLocationPercent = 85;
      std::vector<double> front(ribs.size(), 0), rear;
      for (const auto& rib : ribs) rear.push_back(rib.chord - 40);
      const auto fitted = fitSparAheadOfRearBoundary(ribs, spar, front, rear);
      assert(fitted);
      spar.chordLocationPercent = fitted->first; spar.tipChordLocationPercent = fitted->second;
      StructureParameters settings;
      settings.trailingEdgeType = 2; settings.trailingEdgeWidth = 40; settings.trailingEdgeHeight = 40;
      settings.spars = {spar};
      const auto wing = applyWingStructure(ribs, settings);
      const auto& centers = wing.members.front().centers;
      const double slopeX = ribs.back().leadingEdgeOffset + centers.back().x -
          ribs.front().leadingEdgeOffset - centers.front().x;
      assert(std::abs(slopeX + 30) < .04);
      const auto shape = designrc::geometry::buildStructuredWingPreview(wing, settings.ribThickness);
      assert((!shape.IsNull() && BRepCheck_Analyzer{shape, false}.IsValid()));
      auto blocked = spar; blocked.woodWidth = 6;
      SparParameters original = spar; original.chordLocationPercent = original.tipChordLocationPercent = 85;
      assert(!fitSparAheadOfRearBoundary(ribs, original, front, rear, {}, {blocked}));
    }
    WingParameters panel;
    panel.rootChord = 200; panel.tipChord = 160; panel.sweep = -30;
    panel.ribCount = 7; panel.dihedralDegrees = 0;
    const auto foil = AirfoilProfile::nacaSymmetric(.15);
    StructureParameters invalid;
    invalid.ailerons = invalid.aileronPerpendicularToRibs = true;
    invalid.aileronStartRib = 2; invalid.aileronStopRib = 7; invalid.aileronWidth = 20;
    bool rejected = false;
    try { static_cast<void>(applyWingStructure(generateRibs(panel, foil, foil), invalid)); }
    catch (const std::invalid_argument& error) { rejected = std::string{error.what()}.find("crosses the TE") != std::string::npos; }
    assert(rejected);
    return 0;
  }
  if (argc > 1 && std::string{argv[1]} == "--hinge-spars") {
    using namespace designrc::domain;
    for (int fixture = 0; fixture < 4; ++fixture) {
      WingParameters p;
      p.halfSpan = 250; p.rootChord = 200; p.tipChord = 180;
      p.sweep = 8; p.ribCount = 7; p.dihedralDegrees = 0;
      if (fixture >= 2) {
        p.planform = {{{0, 0}, {3, .5}, {0, 1}},
                     {{200, 0}, {207, .5}, {180, 1}}, 250, 250};
        p.tipTwistDegrees = 2;
      }
      const auto ribs = generateRibs(p, AirfoilProfile::nacaSymmetric(.15), AirfoilProfile::nacaSymmetric(.12));
      StructureParameters settings;
      settings.ailerons = settings.flaps = true;
      settings.flapStartRib = 2; settings.flapStopRib = 4;
      settings.aileronStartRib = 4; settings.aileronStopRib = 7;
      settings.flapWidth = settings.aileronWidth = 50;
      settings.trailingEdgeType = 2; settings.trailingEdgeWidth = 10; settings.trailingEdgeHeight = 30;
      for (auto* options : {&settings.builtUpAileron, &settings.builtUpFlap}) {
        options->enabled = options->hingeSpars = true;
        options->hingeSparWidth = 6; options->hingeSparHeight = 1.5;
        options->hingeAngleDegrees = fixture == 0 ? 0 : fixture == 1 ? 45 : fixture == 2 ? 30 : 60;
        options->topSheeting = options->bottomSheeting = (fixture % 2) == 1;
        options->topSheetingWidth = options->bottomSheetingWidth = 50;
        options->topSheetingThickness = options->bottomSheetingThickness = .5;
      }
      auto wing = applyWingStructure(ribs, settings);
      for (const auto& control : wing.controlSurfaces) {
        assert(control.hingeSpars && control.hingePostWidth == 0 && control.builtUpWing);
        const auto& built = *control.builtUpWing;
        assert(std::none_of(built.members.begin(), built.members.end(),
            [](const auto& member) { return member.name == "Movable hinge post"; }));
        assert(std::count_if(built.profiledMembers.begin(), built.profiledMembers.end(),
            [](const auto& member) { return member.name.find("hinge spar") != std::string::npos; }) == 4);
        const auto interpolate = [](const std::vector<Point2>& surface, double x) {
          const auto high = std::lower_bound(surface.begin(), surface.end(), x,
              [](Point2 point, double target) { return point.x < target; });
          assert(high != surface.begin() && high != surface.end());
          const auto low = std::prev(high);
          return low->y + (high->y - low->y) * (x - low->x) / (high->x - low->x);
        };
        const auto front = [](const auto& profile) {
          return std::min_element(profile.begin(), profile.end(), [](Point2 a, Point2 b) { return a.x < b.x; })->x;
        };
        const double rootX = ribs[control.startRibIndex].leadingEdgeOffset + front(control.profiles.front());
        const double tipX = ribs[control.stopRibIndex].leadingEdgeOffset + front(control.profiles.back());
        for (std::size_t i = 0; i < built.ribs.size(); ++i) {
          const auto& rib = built.ribs[i];
          const double t = (rib.rib.spanPosition - ribs[control.startRibIndex].spanPosition) /
              (ribs[control.stopRibIndex].spanPosition - ribs[control.startRibIndex].spanPosition);
          const double leading = rootX + t * (tipX - rootX) - rib.rib.leadingEdgeOffset;
          const auto raw = rib.rib.profile.resampled(81);
          std::vector<Point2> upper;
          for (int j = 80; j >= 0; --j) upper.push_back({raw[j].x * rib.rib.chord, raw[j].y * rib.rib.chord});
          const double top = interpolate(upper, leading);
          const double tangent = std::tan(control.hingeAngleDegrees * std::numbers::pi / 180.0);
          const auto distance = [&](Point2 point) { return point.x - leading + tangent * (point.y - top); };
          bool hasBevelEdge = false;
          for (std::size_t j = 0; j < rib.outerOutline.size(); ++j) {
            assert(distance(rib.outerOutline[j]) >= -1e-7);
            if (std::abs(distance(rib.outerOutline[j])) < 1e-7 &&
                std::abs(distance(rib.outerOutline[(j + 1) % rib.outerOutline.size()])) < 1e-7) hasBevelEdge = true;
          }
          assert(hasBevelEdge);
          const auto drawing = makeStructuredRibPartDrawing(rib, rib.name);
          for (const auto& path : drawing.paths) {
            if (path.layer != "RIB_OUTLINE") continue;
            for (const auto point : path.points) assert(distance(point) >= -1e-7);
          }
          for (const auto& member : built.profiledMembers) {
            if (!member.name.starts_with("Control ")) continue;
            const auto& profile = member.profiles[i];
            for (const auto point : profile) assert(distance(point) >= -1e-7);
            const bool upperSide = member.name.find("Top") != std::string::npos;
            double aft = profile.front().x, floor = profile.front().y;
            for (const auto point : profile) {
              aft = std::max(aft, point.x);
              floor = upperSide ? std::min(floor, point.y) : std::max(floor, point.y);
            }
            assert(std::any_of(rib.outerOutline.begin(), rib.outerOutline.end(), [&](Point2 point) {
              return std::hypot(point.x - aft, point.y - floor) < 1e-7;
            }));
          }
        }
      }
      designrc::geometry::MaterialShapeSet parts;
      static_cast<void>(designrc::geometry::buildStructuredWingPreview(wing, 3, nullptr, &parts));
      int sparCount = 0;
      for (const auto& part : parts.parts) {
        assert(part.name.find("hinge post") == std::string::npos);
        if (part.name.find("hinge spar") == std::string::npos) continue;
        ++sparCount;
        assert(BRepCheck_Analyzer{part.shape}.IsValid());
        const auto& control = wing.controlSurfaces[part.name.starts_with("Flap") ? 0 : 1];
        const auto& built = *control.builtUpWing;
        const auto& first = built.ribs.front().rib;
        const auto& last = built.ribs.back().rib;
        const double plane = first.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
        const auto station = [&](double y, double z) { return y * std::cos(plane) + z * std::sin(plane); };
        double low = std::numeric_limits<double>::max(), high = -low;
        for (TopExp_Explorer vertices{part.shape, TopAbs_VERTEX}; vertices.More(); vertices.Next()) {
          const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
          low = std::min(low, station(point.Y(), point.Z()));
          high = std::max(high, station(point.Y(), point.Z()));
        }
        assert(std::abs(low - station(first.spanPosition, first.dihedralHeight) - first.ribThicknessStartFactor * 3) < 1e-6);
        assert(std::abs(high - station(last.spanPosition, last.dihedralHeight) - (last.ribThicknessStartFactor + 1) * 3) < 1e-6);
      }
      assert(sparCount == 8);
      settings.builtUpAileron.hingeSparHeight = 50;
      bool invalid = false;
      try { static_cast<void>(applyWingStructure(ribs, settings)); }
      catch (const std::invalid_argument&) { invalid = true; }
      assert(invalid);
    }
    return 0;
  }
  if (argc > 1 && std::string{argv[1]} == "--builtup") {
    using namespace designrc::domain;
    WingParameters p;
    p.halfSpan = 250; p.rootChord = p.tipChord = 200;
    p.dihedralDegrees = 0; p.ribCount = 7;
    const auto foil = AirfoilProfile::nacaSymmetric(.15);
    for (const bool curved : {false, true}) {
      if (curved) {
        p.planform = {{{0, 0}, {3, .5}, {0, 1}},
                     {{200, 0}, {207, .5}, {200, 1}}, 250, 250};
        p.tipTwistDegrees = 3;
      }
      auto ribs = generateRibs(p, foil, foil);
      StructureParameters settings;
      settings.ailerons = settings.flaps = true;
      settings.flapStartRib = 2; settings.flapStopRib = 4;
      settings.aileronStartRib = 4; settings.aileronStopRib = 7;
      settings.flapWidth = settings.aileronWidth = 40;
      settings.builtUpAileron.enabled = settings.builtUpFlap.enabled = true;
      settings.builtUpAileron.topSheeting = true;
      settings.builtUpAileron.topSheetingWidth = 45; // full chord coverage clips at the front
      settings.builtUpAileron.topSheetingThickness = .5;
      settings.builtUpFlap.bottomSheeting = true;
      settings.builtUpFlap.bottomSheetingWidth = 45;
      settings.builtUpFlap.bottomSheetingThickness = .5;
      SparParameters spar;
      spar.verticalLocation = 0; spar.material = 0;
      spar.woodHeight = 1; spar.woodWidth = 2;
      settings.builtUpAileron.spars.push_back(spar);
      auto carbon = spar;
      carbon.verticalLocation = 1; carbon.material = 1;
      carbon.type = 0; carbon.tubeOd = 1; carbon.tubeId = .5;
      settings.builtUpFlap.spars.push_back(carbon);
      auto wing = testsupport::buildStructure(ribs, settings);
      assert(wing.controlSurfaces.size() == 2);
      for (const auto& c : wing.controlSurfaces) {
        assert(c.builtUpWing && !c.builtUpWing->ribs.empty());
        assert(c.builtUpWing->sheeting.size() == 1);
        for (const auto& r : c.builtUpWing->ribs) {
          assert(!r.outerOutline.empty());
          for (const auto point : r.outerOutline) assert(point.x > 150);
        }
      }
      designrc::geometry::MaterialShapeSet materials;
      std::mutex progressMutex;
      std::vector<std::pair<int, std::string>> events;
      auto shape = designrc::geometry::buildStructuredWingPreview(wing, settings.ribThickness,
          nullptr, &materials, [&](int value, const std::string& message) {
            std::scoped_lock lock{progressMutex};
            events.emplace_back(value, message);
          });
      assert(!shape.IsNull());
      assert(!events.empty() && events.back().first == 100 && events.back().second == "Panel geometry complete");
      int previousProgress = 0, completeCount = 0;
      for (const auto& [value, message] : events) {
        assert(value >= previousProgress && value <= 100);
        previousProgress = value;
        if (message == "Panel geometry complete") ++completeCount;
        if (value == 100) assert(message == "Panel geometry complete");
      }
      assert(completeCount == 1);
      for (const std::string name : {"Aileron", "Flap"}) {
        bool hasRibs = false, hasSheets = false, hasSpars = false, hasMesh = false, finished = false;
        int updates = 0;
        for (const auto& [value, message] : events) {
          if (!message.starts_with(name + " (")) continue;
          assert(value < 100);
          ++updates;
          hasRibs = hasRibs || message.find("Building Rib Solids") != std::string::npos;
          hasSheets = hasSheets || message.find("Lofting") != std::string::npos;
          hasSpars = hasSpars || message.find("Building Spar") != std::string::npos;
          hasMesh = hasMesh || message.find("Meshing geometry") != std::string::npos;
          finished = finished || message.ends_with("Geometry complete");
        }
        assert(hasRibs && hasSheets && hasSpars && hasMesh && finished && updates > 5);
      }
      if (!curved) {
        std::atomic_bool cancelled{false};
        bool stopped = false;
        try {
          static_cast<void>(designrc::geometry::buildStructuredWingPreview(wing, settings.ribThickness,
              nullptr, nullptr, [&](int, const std::string& message) {
                if ((message.starts_with("Aileron (") || message.starts_with("Flap (")) &&
                    message.find("Building Rib Solids") != std::string::npos) cancelled = true;
              }, 2, [&] { return cancelled.load(); }));
        } catch (const designrc::geometry::GeometryCancelled&) { stopped = true; }
        assert(cancelled && stopped);
      }
      bool hasRib = false, hasSpar = false, hasSheet = false;
      for (const auto& part : materials.parts) {
        assert(part.name != "Aileron" && part.name != "Flap");
        assert(BRepCheck_Analyzer{part.shape}.IsValid());
        hasRib = hasRib || part.name.starts_with("AR");
        assert(!part.name.starts_with("Aileron R") && !part.name.starts_with("Flap R"));
        hasSpar = hasSpar || part.name.find("Aileron Spar") != std::string::npos;
        hasSheet = hasSheet || part.name.find("sheeting") != std::string::npos;
      }
      assert(hasRib && hasSpar && hasSheet);
      // Whole-wing TE options are independent of control settings.
      settings.topTeSheeting = true; settings.topTeSheetingWidth = 10;
      settings.topTeSheetingThickness = .5;
      const auto withWingSheet = testsupport::buildStructure(ribs, settings);
      assert(!withWingSheet.sheeting.empty());
      assert(withWingSheet.controlSurfaces.back().builtUpWing->sheeting.size() == 2);
      designrc::geometry::MaterialShapeSet sheetParts;
      static_cast<void>(designrc::geometry::buildStructuredWingPreview(withWingSheet,settings.ribThickness,nullptr,&sheetParts));
      for(const auto& boundary : withWingSheet.controlSurfaces) {
        const auto& rib=withWingSheet.ribs[boundary.startRibIndex].rib;
        const double fixedEnd=rib.spanPosition+(rib.ribThicknessStartFactor+1)*settings.ribThickness;
        const double x=rib.leadingEdgeOffset+rib.chord-5;
        const auto sheetVolumeAt=[&](const double span) {
          const auto probe=BRepPrimAPI_MakeBox{gp_Pnt{x,span-.1,-50},2,.2,100}.Shape();
          double volume=0;
          for(const auto& part:sheetParts.parts) {
            if(part.name.find("sheeting")==std::string::npos) continue;
            assert(BRepCheck_Analyzer{part.shape}.IsValid());
            BRepAlgoAPI_Common common{part.shape,probe};
            assert(common.IsDone());
            GProp_GProps properties;
            BRepGProp::VolumeProperties(common.Shape(),properties);
            volume+=properties.Mass();
          }
          return volume;
        };
        assert(sheetVolumeAt(fixedEnd+boundary.gap*.5)<1e-6);
        assert(sheetVolumeAt(fixedEnd-.5)>1e-4);
        assert(sheetVolumeAt(fixedEnd+boundary.gap+.5)>1e-4);
      }
      settings.topTeSheeting = false;
      settings.builtUpFlap.topSheeting = true;
      settings.builtUpFlap.topSheetingWidth = 20;
      settings.builtUpFlap.topSheetingThickness = .5;
      settings.trailingEdgeType = 2; settings.trailingEdgeWidth = 12; settings.trailingEdgeHeight = 20;
      const auto stockWing = testsupport::buildStructure(ribs, settings);
      assert(!stockWing.controlSurfaces.back().builtUpWing->sheetStockParts.empty());
      designrc::geometry::MaterialShapeSet stockParts;
      static_cast<void>(designrc::geometry::buildStructuredWingPreview(stockWing, settings.ribThickness, nullptr, &stockParts));
      for(const auto& name:{"ATE1","FTE1"}) {
        assert(std::any_of(stockParts.parts.begin(),stockParts.parts.end(),[&](const auto& part){return part.name==name;}));
      }
      settings.trailingEdgeWidth = 45;
      bool noGap = false;
      try { static_cast<void>(testsupport::buildStructure(ribs, settings)); }
      catch (const std::invalid_argument& error) { noGap = std::string{error.what()}.find("No gap") != std::string::npos; }
      assert(noGap);
      settings.trailingEdgeType = 0;
      settings.builtUpAileron.spars.front().verticalLocation = 2;
      bool rejected = false;
      try { static_cast<void>(testsupport::buildStructure(ribs, settings)); }
      catch (const std::invalid_argument&) { rejected = true; }
      assert(rejected);
    }

    // Regression: Built-upAilerons.designrc has no imported curves or sheeting,
    // but morphing airfoils and twist made its bottom spar fail a tangent test.
    for (const bool top : {false, true}) for (const double twist : {-2.0, 2.0}) {
      WingParameters exact;
      exact.halfSpan = 400; exact.rootChord = exact.tipChord = 200;
      exact.sweep = 15; exact.tipTwistDegrees = twist; exact.dihedralDegrees = 0;
      exact.ribCount = 9;
      const auto ribs = generateRibs(exact, AirfoilProfile::nacaSymmetric(.15),
                                     AirfoilProfile::nacaSymmetric(.10));
      StructureParameters settings;
      settings.leadingEdgeType = 2; settings.leadingEdgeWidth = 5; settings.leadingEdgeHeight = 17.06;
      settings.trailingEdgeType = 2; settings.trailingEdgeWidth = 12; settings.trailingEdgeHeight = 12;
      settings.ailerons = settings.flaps = true;
      settings.aileronWidth = settings.flapWidth = 40;
      settings.aileronStartRib = 5; settings.aileronStopRib = 9;
      settings.flapStartRib = 2; settings.flapStopRib = 5;
      settings.aileronHingePostWidth = settings.flapHingePostWidth = 4;
      settings.aileronHingePostHeight = settings.flapHingePostHeight = 4;
      settings.builtUpAileron.enabled = settings.builtUpFlap.enabled = true;
      SparParameters spar;
      spar.material = 0; spar.verticalLocation = top ? 0 : 1;
      spar.woodHeight = 1.5875; spar.woodWidth = 3.175;
      spar.chordLocationPercent = spar.tipChordLocationPercent = 25;
      settings.builtUpAileron.spars.push_back(spar);
      const auto wing = testsupport::buildStructure(ribs, settings);
      for (const auto& control : wing.controlSurfaces) {
        const auto& members = control.builtUpWing->members;
        const auto post = std::find_if(members.begin(), members.end(),
            [](const auto& m) { return m.name == "Movable hinge post"; });
        assert(post != members.end());
        assert(post->width == control.hingePostWidth && post->height == control.hingePostHeight);
      }
      const auto& built = *wing.controlSurfaces.back().builtUpWing;
      const auto sparPart = std::find_if(built.members.begin(), built.members.end(),
          [](const auto& m) { return m.name == "Spar 1"; });
      assert(sparPart != built.members.end());
      const auto world = [](const RibDefinition& rib, Point2 center) {
        const double angle = rib.twistDegrees * std::numbers::pi / 180.0;
        const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
        const auto shift = ribTwistTranslation(rib);
        return Point2{rib.leadingEdgeOffset + std::cos(angle) * center.x - std::sin(angle) * center.y + shift.x,
          rib.dihedralHeight + std::cos(plane) * (std::sin(angle) * center.x + std::cos(angle) * center.y + shift.y)};
      };
      const auto first = world(built.ribs.front().rib, sparPart->centers.front());
      const auto last = world(built.ribs.back().rib, sparPart->centers.back());
      for (std::size_t i = 0; i < built.ribs.size(); ++i) {
        const double t = (built.ribs[i].rib.spanPosition - built.ribs.front().rib.spanPosition) /
            (built.ribs.back().rib.spanPosition - built.ribs.front().rib.spanPosition);
        const auto point = world(built.ribs[i].rib, sparPart->centers[i]);
        assert(std::abs(point.x - (first.x + t * (last.x - first.x))) < 1e-8);
        assert(std::abs(point.y - (first.y + t * (last.y - first.y))) < 1e-8);
        const auto& rib = built.ribs[i].rib;
        const auto outline = rib.profile.resampled(81);
        std::vector<Point2> upper, lower;
        for (std::size_t j = 0; j <= 80; ++j)
          upper.push_back({outline[80 - j].x * rib.chord, outline[80 - j].y * rib.chord});
        for (std::size_t j = 80; j < outline.size(); ++j)
          lower.push_back({outline[j].x * rib.chord, outline[j].y * rib.chord});
        const auto at = [](const std::vector<Point2>& surface, double x) {
          const auto high = std::lower_bound(surface.begin(), surface.end(), x,
              [](const auto p, double value) { return p.x < value; });
          assert(high != surface.begin() && high != surface.end());
          const auto low = std::prev(high);
          return low->y + (high->y - low->y) * (x - low->x) / (high->x - low->x);
        };
        const double angle = rib.twistDegrees * std::numbers::pi / 180.0;
        const auto shift = ribTwistTranslation(rib);
        for (const double dx : {-spar.woodWidth * .5, spar.woodWidth * .5})
          for (const double dz : {-spar.woodHeight * .5, spar.woodHeight * .5}) {
            const double x = point.x + dx - rib.leadingEdgeOffset - shift.x;
            const double z = point.y + dz - rib.dihedralHeight - shift.y;
            const Point2 corner{std::cos(angle) * x + std::sin(angle) * z,
                                -std::sin(angle) * x + std::cos(angle) * z};
            assert(corner.y <= at(upper, corner.x) + .01);
            assert(corner.y >= at(lower, corner.x) - .01);
          }
      }
      designrc::geometry::MaterialShapeSet parts;
      static_cast<void>(designrc::geometry::buildStructuredWingPreview(wing, 3, nullptr, &parts));
      for (const auto& name : {"Aileron Movable hinge post", "Flap Movable hinge post"}) {
        const auto post = std::find_if(parts.parts.begin(), parts.parts.end(),
            [&](const auto& part) { return part.name == name; });
        assert(post != parts.parts.end());
        assert(BRepCheck_Analyzer{post->shape}.IsValid());
        const auto fixed = std::find_if(parts.parts.begin(), parts.parts.end(),
            [](const auto& part) { return part.name == "Flap/Aileron hinge post"; });
        assert(fixed != parts.parts.end());
        BRepExtrema_DistShapeShape gap{post->shape, fixed->shape};
        gap.Perform();
        assert(gap.IsDone() && gap.Value() > .5 && gap.Value() < settings.controlSurfaceGap + 1);
      }
      settings.builtUpAileron.spars.front().woodHeight = 50;
      bool invalid = false;
      try { static_cast<void>(testsupport::buildStructure(ribs, settings)); }
      catch (const std::invalid_argument&) { invalid = true; }
      assert(invalid);
    }
    return 0;
  }

  const auto stage = [](const char* name) {
    std::cerr << "Geometry regression stage: " << name << '\n';
  };
  const bool sweepAssemblyOnly = argc > 1 && std::string{argv[1]} == "--sweep-assembly";
  const bool sweepTwistOnly = argc > 1 && std::string{argv[1]} == "--sweep-twist";
  const bool sweepFixtureOnly = sweepAssemblyOnly || sweepTwistOnly || (argc > 1 && std::string{argv[1]} == "--sweep-fixture");
  if (sweepFixtureOnly) {
    using namespace designrc::domain;
    QTemporaryDir directory;
    const auto path = directory.filePath("Curved_LE_TE.svg");
    QFile file{path};
    assert(file.open(QIODevice::WriteOnly));
    file.write("<svg width='500mm' height='200mm' viewBox='0 0 500 200'>"
               "<path d='m 0.36802972,0 c 0,0 202.02663028,0 212.94699028,1.1776952 "
               "10.92035,1.177695 270.27888,4.121933 287.20541,13.5434948'/>"
               "<path d='M 0.73605947,154.57249 498.68028,127.70632 v 0'/></svg>");
    file.close();
    WingParameters p;
    p.halfSpan = 500;
    p.ribCount = 11;
    p.dihedralDegrees = 4;
    p.planform = importPlanformCurves(std::filesystem::path{path.toStdWString()}, p.halfSpan);
    p.rootChord = p.planform.chord(0);
    p.tipChord = p.planform.chord(1);
    StructureParameters settings;
    settings.leadingEdgeType = 2;
    settings.leadingEdgeWidth = 4.7625;
    settings.leadingEdgeHeight = 15.875;
    settings.trailingEdgeType = 2;
    settings.trailingEdgeWidth = 25.4;
    settings.trailingEdgeHeight = 9.525;
    for (const double twist : {0.0, 6.0, -6.0}) {
      if (sweepTwistOnly && twist == 0) continue;
      p.tipTwistDegrees = twist;
      auto ribs = generateRibs(p, AirfoilProfile::nacaSymmetric(.15), AirfoilProfile::nacaSymmetric(.10));
      if (sweepAssemblyOnly) {
        const double angle = p.dihedralDegrees * std::numbers::pi / 180;
        for (std::size_t i = 0; i < ribs.size(); ++i) {
          const double span = p.halfSpan * i / (ribs.size() - 1);
          ribs[i].spanPosition = std::cos(angle) * span;
          ribs[i].dihedralHeight = std::sin(angle) * span;
          ribs[i].ribPlaneAngleDegrees = i == 0 ? 0 : p.dihedralDegrees;
          ribs[i].ribThicknessStartFactor = i == 0 ? 0 : i + 1 == ribs.size() ? -1 : -.5;
        }
      }
      auto wing = testsupport::buildStructure(ribs, settings);
      designrc::geometry::PanelBuildTimings timing;
      designrc::geometry::MaterialShapeSet materials;
      const auto shape = designrc::geometry::buildStructuredWingPreview(wing, p.ribThickness, &timing, &materials);
      assert(!shape.IsNull());
      for (const auto& part : materials.parts) assert((BRepCheck_Analyzer{part.shape, false}.IsValid()));
      assert(timing.guideSweeps == 2 && timing.reducedLoftFallbacks == 0);
      assert(timing.maximumSweepProfiles < (sweepAssemblyOnly ? 64 : 32));
      for (const auto& stock : wing.profiledMembers) {
        const auto part = std::find_if(materials.parts.begin(), materials.parts.end(),
                                      [&](const auto& candidate) { return candidate.name == stock.name; });
        assert(part != materials.parts.end());
        BRep_Builder builder;
        TopoDS_Compound faces;
        builder.MakeCompound(faces);
        for (TopExp_Explorer face{part->shape, TopAbs_FACE}; face.More(); face.Next())
          builder.Add(faces, face.Current());
        for (const std::size_t i : {std::size_t{1}, std::size_t{5}, std::size_t{9}}) {
          const auto& rib = wing.ribs[i].rib;
          const auto translation = ribTwistTranslation(rib);
          const double angle = rib.twistDegrees * std::numbers::pi / 180;
          const double plane = rib.ribPlaneAngleDegrees * std::numbers::pi / 180;
          for (std::size_t j = 0; j < stock.profiles[i].size(); j += 12) {
            const auto point = stock.profiles[i][j];
            const double x = std::cos(angle) * point.x - std::sin(angle) * point.y + translation.x;
            const double z = std::sin(angle) * point.x + std::cos(angle) * point.y + translation.y;
            const gp_Pnt expected{rib.leadingEdgeOffset + x, rib.spanPosition - std::sin(plane) * z,
                                  rib.dihedralHeight + std::cos(plane) * z};
            BRepExtrema_DistShapeShape distance{BRepBuilderAPI_MakeVertex{expected}.Vertex(), faces};
            if (!distance.IsDone() || distance.Value() > 0.05)
              throw std::runtime_error("Swept " + stock.name + " misses rib " + std::to_string(i) +
                                       " point " + std::to_string(j) + " at twist " + std::to_string(twist) +
                                       " by " + std::to_string(distance.Value()) + " mm");
          }
        }
      }
      std::cerr << "Dense curve fixture: " << p.planform.leading.size() << " imported points, "
                << twist << " degree twist, " << timing.guideSweeps << " sweeps, "
                << timing.maximumSweepProfiles << " maximum profiles, "
                << timing.profiledStockMs << " ms building LE/TE\n";
    }
    return 0;
  }
  const bool teSheetingSolidOnly = argc > 1 &&
      std::string{argv[1]} == "--te-sheeting-solid";
  const bool multiSparSheetingSolidOnly = argc > 1 &&
      std::string{argv[1]} == "--multi-spar-sheeting-solid";
  const bool woodJoinerCollisionOnly = argc > 1 &&
      std::string{argv[1]} == "--wood-joiner-collision";
  const bool sparEndFacesOnly = argc > 1 &&
      std::string{argv[1]} == "--spar-end-faces";
  const bool buildTabClearanceOnly = argc > 1 &&
      std::string{argv[1]} == "--build-tab-clearance";
  const bool cancelLighteningOnly = argc > 1 &&
      std::string{argv[1]} == "--cancel-lightening";
  const bool planformOnly = argc > 1 && std::string{argv[1]} == "--planform";
  const bool focusedGeometryOnly =
      planformOnly || teSheetingSolidOnly || multiSparSheetingSolidOnly ||
      woodJoinerCollisionOnly || sparEndFacesOnly || buildTabClearanceOnly || cancelLighteningOnly;
  if (!focusedGeometryOnly || planformOnly) {
    stage("imported planform LE stocks and auxiliary surface stations");
    using namespace designrc::domain;
    WingParameters p;
    p.halfSpan = 140;
    p.ribCount = 3;
    p.dihedralDegrees = 0;
    p.rootChord = p.tipChord = 200;
    p.planform = {{{0, 0}, {-8, .3}, {0, 1}}, {{200, 0}, {220, .3}, {200, 1}}, 140, 140};
    const auto foil = AirfoilProfile::nacaSymmetric(.12);
    const auto ribs = generateRibs(p, foil, foil);
    for (int type : {2, 3, 4, 5, 6, 7}) {
      std::cerr << "  curved LE type " << type << '\n';
      StructureParameters settings;
      settings.leadingEdgeType = type;
      settings.leadingEdgeWidth = 6;
      settings.leadingEdgeHeight = 20;
      settings.leadingEdgeTubeOd = 3;
      settings.leadingEdgeTubeId = 2;
      settings.leadingEdgeRodOd = 3;
      settings.notchedLeadingEdgeWidth = 8;
      settings.notchedLeadingEdgeHeight = 4;
      auto wing = testsupport::buildStructure(ribs, settings);
      assert(wing.ribs.size() == 3 && wing.surfaceWing);
      designrc::geometry::MaterialShapeSet materials;
      designrc::geometry::PanelBuildTimings timing;
      const auto shape =
          designrc::geometry::buildStructuredWingPreview(wing, p.ribThickness, &timing, &materials);
      assert(!shape.IsNull());
      assert(timing.guideSweeps > 0 && timing.reducedLoftFallbacks == 0);
      bool found = false;
      for (const auto& part : materials.parts)
        if (part.name.find("leading edge") != std::string::npos) {
          assert((BRepCheck_Analyzer{part.shape, false}.IsValid()));
          std::size_t faces = 0;
          for (TopExp_Explorer face{part.shape, TopAbs_FACE}; face.More(); face.Next()) ++faces;
          assert(faces <= 12); // No separate ruled faces at every sampled station.
          Bnd_Box box;
          BRepBndLib::Add(part.shape, box);
          double xmin, ymin, zmin, xmax, ymax, zmax;
          box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
          assert(xmin < -7.0);
          found = true;
        }
      assert(found);
    }
    stage("smooth mixed straight/curved edges and endpoint lofts");
    for (int mode : {0, 1, 2}) {
      auto mixed = p;
      mixed.planform.leading.clear();
      mixed.planform.trailing.clear();
      for (int i = 0; i <= 32; ++i) {
        const double t = i / 32.0;
        const double bend = 20 * t * (1 - t);
        mixed.planform.leading.push_back({mode == 1 ? -bend : 0, t});
        mixed.planform.trailing.push_back({200 + (mode == 0 ? bend : 0), t});
      }
      StructureParameters settings;
      settings.leadingEdgeType = 2;
      settings.leadingEdgeWidth = 6;
      settings.leadingEdgeHeight = 20;
      settings.trailingEdgeType = 2;
      settings.trailingEdgeWidth = 20;
      settings.trailingEdgeHeight = 6;
      auto wing = testsupport::buildStructure(generateRibs(mixed, foil, foil), settings);
      designrc::geometry::MaterialShapeSet materials;
      const auto shape = designrc::geometry::buildStructuredWingPreview(wing, mixed.ribThickness, nullptr, &materials);
      assert(!shape.IsNull());
      std::size_t edges = 0;
      for (const auto& part : materials.parts) {
        if (part.name.find("edge") == std::string::npos) continue;
        ++edges;
        assert((BRepCheck_Analyzer{part.shape, false}.IsValid()));
        std::size_t faces = 0;
        for (TopExp_Explorer face{part.shape, TopAbs_FACE}; face.More(); face.Next()) ++faces;
        assert(faces <= 8);
      }
      assert(edges >= 2);
    }
    stage("curved TE, straight hinges, sheeting and spar");
    p.ribCount = 5;
    for (bool parallel : {false, true}) {
      StructureParameters settings;
      settings.ailerons = true;
      settings.aileronStartRib = 2;
      settings.aileronStopRib = 5;
      settings.aileronWidth = 50;
      settings.aileronHingeParallelY = parallel;
      settings.topTeSheeting = true;
      settings.topTeSheetingWidth = 10;
      settings.topTeSheetingThickness = 1;
      settings.leadingEdgeType = 4;
      settings.leadingEdgeRodOd = 3;
      SparParameters spar;
      spar.type = 1;
      spar.rodOd = 3;
      settings.spars = {spar};
      auto wing = testsupport::buildStructure(generateRibs(p, foil, foil), settings);
      designrc::geometry::MaterialShapeSet materials;
      const auto shape =
          designrc::geometry::buildStructuredWingPreview(wing, p.ribThickness, nullptr, &materials);
      assert(!shape.IsNull());
      for (const auto& part : materials.parts)
        if (!BRepCheck_Analyzer{part.shape, false}.IsValid())
          throw std::runtime_error("Invalid curved control component: " + part.name);
    }
    stage("curved panel with twist, dihedral, riblets, tabs and caps");
    p.tipTwistDegrees = 2;
    p.dihedralDegrees = 4;
    StructureParameters detail;
    detail.leadingEdgeType = 4;
    detail.leadingEdgeRodOd = 3;
    SparParameters spar;
    spar.type = 1;
    spar.rodOd = 3;
    detail.spars = {spar};
    detail.topRibCaps = true;
    detail.topRibCapWidth = 5;
    detail.topRibCapThickness = 1;
    detail.addFrontBuildTab = true;
    detail.addRearBuildTab = true;
    detail.riblets = true;
    detail.ribletStartRib = 1;
    detail.ribletEndRib = 5;
    detail.ribletsPerBay = 1;
    auto detailed = testsupport::buildStructure(generateRibs(p, foil, foil), detail);
    addRiblets(detailed, detail);
    assert(detailed.riblets.size() == 4 && !detailed.ribCaps.empty());
    const auto detailShape =
        designrc::geometry::buildStructuredWingPreview(detailed, p.ribThickness);
    assert(!detailShape.IsNull());
    stage("curved panel front sheeting and spoiler");
    p.tipTwistDegrees = 0;
    p.dihedralDegrees = 0;
    StructureParameters sheet;
    SparParameters top;
    top.material = 0;
    top.verticalLocation = 0;
    top.woodWidth = 6;
    top.woodHeight = 3;
    sheet.spars = {top};
    sheet.leTopSheet = true;
    sheet.leTopSheetThickness = 1;
    sheet.leTopSheetStopRib = 5;
    sheet.spoilers = true;
    sheet.spoilerStartRib = 2;
    sheet.spoilerEndRib = 5;
    sheet.spoilerImmediatelyBehindSpar = true;
    sheet.spoilerWidth = 15;
    sheet.spoilerThickness = 1;
    auto sheeted = testsupport::buildStructure(generateRibs(p, foil, foil), sheet);
    const auto sheetShape = designrc::geometry::buildStructuredWingPreview(sheeted, p.ribThickness);
    assert(!sheetShape.IsNull());
    if (planformOnly) return 0;
  }
  if (!focusedGeometryOnly || cancelLighteningOnly) {
    stage("cancel inside lightening-hole Boolean cuts");
    using namespace designrc::domain;
    WingParameters p;
    p.ribCount = 3; p.rootChord = p.tipChord = 200.0;
    const auto foil = AirfoilProfile::nacaSymmetric(0.15);
    StructureParameters s;
    s.ribLighteningHoles = true;
    s.ribLighteningStartRib = 1; s.ribLighteningStopRib = 3;
    s.ribLighteningMinimumWoodMargin = 3.0;
    s.ribLighteningMinimumHoleDistance = 5.0;
    auto wing = testsupport::buildStructure(generateRibs(p, foil, foil), s);
    addRibLighteningHoles(wing, s);
    if (wing.ribs.front().internalCutouts.empty()) return 120;
    for (const std::size_t workers : {std::size_t{1}, std::size_t{3}}) {
      std::atomic_bool cutting{false};
      std::atomic_int polls{0}, finishedRibs{0};
      bool cancelled = false;
      try {
        static_cast<void>(designrc::geometry::buildStructuredWingPreview(
            wing, p.ribThickness, nullptr, nullptr,
            [&](int, const std::string& message) {
              if (message.find("Cutting Lightening Holes") != std::string::npos) {
                cutting = true;
                if (message.find("complete") != std::string::npos) ++finishedRibs;
              }
            }, workers, [&] { return cutting && ++polls >= 64; }));
      } catch (const designrc::geometry::GeometryCancelled&) {
        cancelled = true;
      }
      if (!cancelled || polls < 64 || finishedRibs != 0) return 121;
    }
    // The cancellable cut must still produce a valid rib when not cancelled.
    wing.ribs.erase(wing.ribs.begin() + 1, wing.ribs.end());
    const auto shape = designrc::geometry::buildStructuredWingPreview(
        wing, p.ribThickness, nullptr, nullptr, {}, 1, [] { return false; });
    if (!BRepCheck_Analyzer{shape}.IsValid()) return 122;
    if (cancelLighteningOnly) return 0;
  }
  if (!focusedGeometryOnly || buildTabClearanceOnly) {
    stage("finished build-tab clearance");
    using namespace designrc::domain;
    for (const int sides : {1, 2, 3}) {
    for (const double twist : {-6.0, 0.0, 6.0}) {
      WingParameters p;
      p.ribCount = 3; p.rootChord = 220; p.tipChord = 150;
      p.halfSpan = 250; p.dihedralDegrees = 12; p.tipTwistDegrees = twist;
      p.ribThickness = 4.0;
      const auto foil = AirfoilProfile::nacaSymmetric(0.12);
      auto tabRibs = generateRibs(p, foil, foil);
      tabRibs.front().ribThicknessStartFactor = 0.0;
      tabRibs.back().ribThicknessStartFactor = -1.0;
      tabRibs.back().ribPlaneAngleDegrees = 20.0;
      if (twist > 0.0) {
        for (auto& rib : tabRibs) { rib.spanPosition += 350.0; rib.dihedralHeight += 55.0; }
        tabRibs.front().ribPlaneAngleDegrees = 8.0;
      }
      StructureParameters s;
      s.ribThickness = p.ribThickness;
      s.addFrontBuildTab = s.addRearBuildTab = (sides & 1) != 0;
      s.addTopFrontBuildTab = s.addTopRearBuildTab = (sides & 2) != 0;
      s.frontBuildTabLocationPercent = 20.0;
      s.rearBuildTabLocationPercent = 70.0;
      s.topFrontBuildTabLocationPercent = 20.0;
      s.topRearBuildTabLocationPercent = 70.0;
      s.buildTabHeightAboveTable = 12.7;
      s.buildTabWidth = 8.0;
      s.spars = {{25, 1, 0, 2, 3.0, 8.0}};
      auto wing = testsupport::buildStructure(tabRibs, s);
      // Inject a residual lip, or a curve dipping below its end points, to
      // verify the independent completed-solid correction actually runs.
      if (twist != 0.0) {
        auto& rib = wing.ribs.front();
        bool injected = false;
        for (auto& segment : rib.outlineSegments) {
          if (segment.points.size() != 2) continue;
          const auto a = segment.points[0], b = segment.points[1];
          if (std::abs(std::abs(b.x - a.x) - s.buildTabWidth) > 1.0e-7) continue;
          segment.points.insert(segment.points.begin() + 1,
              Point2{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5 +
                  ((a.y + b.y) > 0.0 ? 0.4 : -0.4)});
          segment.spline = twist > 0.0;
          injected = true;
          break;
        }
        if (!injected) throw std::runtime_error("Missing support edge for clearance regression");
      }
      designrc::geometry::MaterialShapeSet materials;
      const auto shape = designrc::geometry::buildStructuredWingPreview(
          wing, p.ribThickness, nullptr, &materials);
      if (!BRepCheck_Analyzer{shape}.IsValid())
        throw std::runtime_error("Invalid build-tab corrected shape");
      std::size_t checked = 0;
      for (const auto& part : materials.parts) {
        if (!part.name.starts_with("Rib ")) continue;
        const auto& rib = wing.ribs.at(checked++);
        for (const auto& support : {rib.buildPlane, rib.topBuildPlane}) {
        if (!support) continue;
        const auto& plane = *support;
        gp_Trsf align;
        align.SetRotation(gp_Ax1{gp_Pnt{0, 0, 0}, gp_Dir{1, 0, 0}},
            std::atan2(plane.spanNormal, plane.verticalNormal));
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(BRepBuilderAPI_Transform{part.shape, align}.Shape(), bounds, false, false);
        double x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        if (std::abs(z0 - plane.offset) > 1.0e-6)
          throw std::runtime_error("Finished rib does not rest on the build plane");
        }
      }
      if (checked != 3) throw std::runtime_error("Missing tabbed ribs");
    }
    }
    if (buildTabClearanceOnly) return 0;
  }
  if (!focusedGeometryOnly || sparEndFacesOnly) {
    stage("spar outer end faces");
    using namespace designrc::domain;
    // Reproduce the two wood spars in RibCapTipOverhang, then cover angled
    // panel joints, swept spar axes, tubes, rods and carbon strips as well.
    for (const bool angledJoint : {false, true}) {
      WingParameters p;
      p.rootChord = 254.0; p.tipChord = 152.4;
      p.halfSpan = 300.0; p.ribCount = 3; p.ribThickness = 2.38125;
      p.dihedralDegrees = 0.0;
      const auto foil = AirfoilProfile::nacaSymmetric(0.12);
      auto endRibs = generateRibs(p, foil, foil);
      const double inclination = (angledJoint ? 25.0 : 4.0) * std::numbers::pi / 180.0;
      for (std::size_t i = 0; i < endRibs.size(); ++i) {
        auto& rib = endRibs[i];
        const double span = rib.spanPosition;
        rib.spanPosition = (angledJoint ? 350.0 : 0.0) + span * std::cos(inclination);
        rib.dihedralHeight = span * std::sin(inclination);
        rib.leadingEdgeOffset = 25.4 * span / p.halfSpan;
        rib.ribPlaneAngleDegrees = i == 0 ? (angledJoint ? 17.5 : 0.0) :
            i + 1 == endRibs.size() ? (angledJoint ? 30.0 : 4.0) :
            inclination * 180.0 / std::numbers::pi;
        rib.ribThicknessStartFactor = i == 0 ? 0.0 :
            i + 1 == endRibs.size() ? -1.0 : -0.5;
      }
      StructureParameters s;
      s.ribThickness = p.ribThickness;
      s.spars = {
          {25, 0, 0, 2, 3.175, 6.35},
          {25, 1, 0, 0, 3.175, 6.35},
          {45, 2, 1, 0, 5, 9, 4, 3},
          {60, 2, 1, 1, 5, 9, 6, 5, 3},
          {75, 2, 1, 2, 5, 9, 6, 5, 6, 3, 1}};
      const auto wing = testsupport::buildStructure(endRibs, s);
      designrc::geometry::MaterialShapeSet materials;
      const auto shape = designrc::geometry::buildStructuredWingPreview(
          wing, p.ribThickness, nullptr, &materials);
      if (shape.IsNull()) throw std::runtime_error("Missing spar end-face geometry");
      int checked = 0;
      for (const auto& part : materials.parts) {
        if (!part.name.starts_with("Spar ")) continue;
        ++checked;
        if (!BRepCheck_Analyzer{part.shape}.IsValid())
          throw std::runtime_error("Invalid trimmed spar: " + part.name);
        for (const bool tipEnd : {false, true}) {
          const auto& rib = tipEnd ? endRibs.back() : endRibs.front();
          const double angle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
          gp_Trsf rotation;
          rotation.SetRotation(gp_Ax1{gp_Pnt{0, 0, 0}, gp_Dir{1, 0, 0}}, -angle);
          Bnd_Box bounds;
          BRepBndLib::AddOptimal(BRepBuilderAPI_Transform{part.shape, rotation}.Shape(), bounds, false, false);
          double x0, y0, z0, x1, y1, z1;
          bounds.Get(x0, y0, z0, x1, y1, z1);
          const double boundary = rib.spanPosition * std::cos(angle) + rib.dihedralHeight * std::sin(angle);
          // Both outside faces lie on the endpoint stations in this fixture.
          // Check equality too: stopping short would leave an unfilled notch.
          if (std::abs((tipEnd ? y1 : y0) - boundary) > 1.0e-5)
            throw std::runtime_error("Spar does not end at outer rib face: " + part.name);
        }
      }
      if (checked != 5) throw std::runtime_error("Missing spar end-face regressions");
    }
    if (sparEndFacesOnly) return 0;
  }
  const auto cappedGeometryWorkers =
      designrc::geometry::ribGeometryWorkerCount(100, 3);
  if (cappedGeometryWorkers < 1 || cappedGeometryWorkers > 3) return 42;
  const auto root = argc > 1 && !focusedGeometryOnly
      ? AirfoilProfile::fromDatFile(std::filesystem::path{argv[1]})
      : AirfoilProfile::nacaSymmetric(0.15);
  const auto tip = argc > 2 && !focusedGeometryOnly
      ? AirfoilProfile::fromDatFile(std::filesystem::path{argv[2]})
      : AirfoilProfile::nacaSymmetric(0.10);

  designrc::domain::WingParameters parameters;
  const auto ribs = designrc::domain::generateRibs(parameters, root, tip);
  const auto verifyWoodJoinerCollision = [&] {
    designrc::domain::StructureParameters structure;
    structure.topSpar = true;
    structure.bottomSpar = true;
    structure.centerSparWoodJoiner = true;
    auto wing = testsupport::buildStructure(ribs, structure);
    if (wing.joiners.size() != 1) return 116;
    wing.joiners.front().name = "Joiner 1";
    auto second = wing.joiners.front();
    second.name = "Joiner 2";
    wing.joiners.push_back(std::move(second));
    try {
      static_cast<void>(designrc::geometry::buildStructuredWingPreview(
          wing, parameters.ribThickness));
    } catch (const std::invalid_argument& error) {
      const std::string message = error.what();
      if (message.find("Joiner collision") != std::string::npos &&
          message.find("Joiner 1") != std::string::npos &&
          message.find("Joiner 2") != std::string::npos)
        return 0;
    }
    return 117;
  };
  if (woodJoinerCollisionOnly) return verifyWoodJoinerCollision();
  const auto verifyMultiSparSheetingSolid = [&] {
    designrc::domain::StructureParameters structure;
    structure.spars = {
        {30, 0, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0},
        {45, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
        {65, 1, 1, 2, 5.0, 9.0, 6.0, 5.0, 6.0, 7.0, 1.5},
        {30, 1, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
    structure.sparShearWebs = true;
    structure.sparShearWebThickness = 3.0;
    structure.leTopSheet = structure.teTopSheet = true;
    structure.leBottomSheet = structure.teBottomSheet = true;
    structure.leTopSheetStopRib = structure.teTopSheetStopRib = 3;
    structure.leBottomSheetStopRib = structure.teBottomSheetStopRib = 3;
    const auto wing = testsupport::buildStructure(ribs, structure);
    designrc::geometry::MaterialShapeSet materials;
    const auto shape = designrc::geometry::buildStructuredWingPreview(
        wing, parameters.ribThickness, nullptr, &materials);
    if (shape.IsNull()) return 52;
    std::size_t ribIndex = 0;
    for (const auto& part : materials.parts) {
      if (!part.name.starts_with("Rib ")) continue;
      if (ribIndex >= wing.ribs.size()) return 53;
      const double angle = wing.ribs[ribIndex].rib.ribPlaneAngleDegrees *
          std::numbers::pi / 180.0;
      const gp_Dir normal{0.0, std::cos(angle), std::sin(angle)};
      std::size_t meshedCaps = 0;
      for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
           faces.More(); faces.Next()) {
        const auto face = TopoDS::Face(faces.Current());
        const BRepAdaptor_Surface faceSurface{face};
        if (faceSurface.GetType() != GeomAbs_Plane ||
            std::abs(faceSurface.Plane().Axis().Direction().Dot(normal)) < 0.99)
          continue;
        TopLoc_Location location;
        if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCaps;
      }
      if (meshedCaps < 2) return 54;
      ++ribIndex;
    }
    return ribIndex == ribs.size() ? 0 : 55;
  };
  if (multiSparSheetingSolidOnly)
    return verifyMultiSparSheetingSolid();
  const auto verifyTeSheetingRibSolids = [&] {
    auto teParameters = parameters;
    teParameters.halfSpan = 698.5;
    teParameters.rootChord = 254.0;
    teParameters.tipChord = 152.4;
    teParameters.sweep = 25.4;
    teParameters.dihedralDegrees = 4.0;
    teParameters.ribCount = 11;
    teParameters.ribThickness = 2.38125;
    const auto teRibs = designrc::domain::generateRibs(
        teParameters, root, tip);
    designrc::domain::StructureParameters structure;
    structure.ribThickness = teParameters.ribThickness;
    structure.spars = {
        {25.0, 0, 0, 2, 3.175, 6.35, 6.0, 5.0, 6.0, 6.0, 1.0, 25.0},
        {25.0, 1, 0, 0, 3.175, 6.35, 6.0, 5.0, 6.0, 6.0, 1.0, 25.0}};
    structure.leadingEdgeType = 2;
    structure.leadingEdgeWidth = 4.7625;
    structure.leadingEdgeHeight = 15.875;
    structure.leTopSheet = true;
    structure.leBottomSheet = true;
    structure.leTopSheetThickness = 1.5875;
    structure.leBottomSheetThickness = 1.5875;
    structure.leTopSheetStopRib = 2;
    structure.leBottomSheetStopRib = 2;
    structure.teTopSheet = true;
    structure.teBottomSheet = true;
    structure.teTopSheetThickness = 1.5875;
    structure.teBottomSheetThickness = 1.5875;
    structure.teTopSheetStopRib = 2;
    structure.teBottomSheetStopRib = 2;
    structure.topTeSheeting = true;
    // TESheetingTest.designrc used a 70%-chord start at its 254 mm root,
    // which migrates to a constant 76.2 mm width.
    structure.topTeSheetingWidth = 76.2;
    structure.topTeSheetingThickness = 1.5875;
    structure.topTeSheetingTaper = false;
    auto wing = testsupport::buildStructure(teRibs, structure);
    // Exercise the same repeated V-joiner cut found in
    // TESheetingTest.designrc: it connects the inner faces of matching top
    // and bottom spars and separates the root rib into two solids.
    const auto topSpar = std::find_if(wing.members.begin(), wing.members.end(),
        [](const auto& member) {
          return member.name.starts_with("Spar ") &&
              member.verticalLocation == 0;
        });
    const auto bottomSpar = std::find_if(
        wing.members.begin(), wing.members.end(), [](const auto& member) {
          return member.name.starts_with("Spar ") &&
              member.verticalLocation == 1;
        });
    if (topSpar == wing.members.end() || bottomSpar == wing.members.end())
      return 51;
    const double joinerX = 0.25 * wing.ribs.front().rib.chord;
    const double joinerHalfThickness = 3.175 * 0.5;
    const double joinerBottom =
        bottomSpar->centers.front().y + bottomSpar->height * 0.5;
    const double joinerTop =
        topSpar->centers.front().y - topSpar->height * 0.5;
    const std::vector<designrc::domain::Point2> joinerCut{
        {joinerX - joinerHalfThickness, joinerBottom},
        {joinerX + joinerHalfThickness, joinerBottom},
        {joinerX + joinerHalfThickness, joinerTop},
        {joinerX - joinerHalfThickness, joinerTop}};
    wing.ribs.front().booleanCutouts.push_back(joinerCut);
    wing.ribs.front().booleanCutouts.push_back(joinerCut);
    designrc::geometry::MaterialShapeSet materials;
    const auto shape = designrc::geometry::buildStructuredWingPreview(
        wing, teParameters.ribThickness, nullptr, &materials);
    if (shape.IsNull()) return 47;
    std::size_t solidRibs = 0;
    for (const auto& part : materials.parts) {
      if (!part.name.starts_with("Rib ")) continue;
      std::size_t ribSolids = 0;
      for (TopExp_Explorer solids{part.shape, TopAbs_SOLID};
           solids.More(); solids.Next())
        ++ribSolids;
      if (ribSolids == 0) return 48;
      const double ribAngle = wing.ribs[solidRibs].rib.ribPlaneAngleDegrees *
          std::numbers::pi / 180.0;
      const auto& rib = wing.ribs[solidRibs].rib;
      const bool centerRoot = std::abs(rib.spanPosition) < 1.0e-9 &&
          std::abs(rib.ribThicknessStartFactor) < 1.0e-9;
      const gp_Dir ribNormal = centerRoot
          ? gp_Dir{0.0, 1.0, 0.0}
          : gp_Dir{0.0, std::cos(ribAngle), std::sin(ribAngle)};
      std::size_t meshedCaps = 0;
      for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
           faces.More(); faces.Next()) {
        const auto face = TopoDS::Face(faces.Current());
        const BRepAdaptor_Surface surface{face};
        if (surface.GetType() != GeomAbs_Plane ||
            std::abs(surface.Plane().Axis().Direction().Dot(ribNormal)) < 0.99)
          continue;
        TopLoc_Location location;
        if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCaps;
      }
      if (meshedCaps < 2 * ribSolids) {
        std::cerr << part.name << " has " << meshedCaps
                  << " triangulated end caps\n";
        for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
             faces.More(); faces.Next()) {
          const auto face = TopoDS::Face(faces.Current());
          const BRepAdaptor_Surface surface{face};
          TopLoc_Location location;
          std::cerr << "  face type=" << static_cast<int>(surface.GetType())
                    << " meshed="
                    << !BRep_Tool::Triangulation(face, location).IsNull();
          if (surface.GetType() == GeomAbs_Plane)
            std::cerr << " normal-dot="
                      << surface.Plane().Axis().Direction().Dot(ribNormal);
          std::cerr << '\n';
        }
        return 50;
      }
      ++solidRibs;
    }
    if (solidRibs != teRibs.size()) return 49;
    const auto verifyAdditionalTeVariant = [&](const bool top,
                                               const bool bottom) {
      auto variant = structure;
      variant.topTeSheeting = top;
      variant.bottomTeSheeting = bottom;
      variant.topTeSheetingWidth = 25.4;
      variant.bottomTeSheetingWidth = 25.4;
      variant.topTeSheetingThickness = 1.5875;
      variant.bottomTeSheetingThickness = 1.5875;
      variant.topTeSheetingTaper = false;
      variant.bottomTeSheetingTaper = false;
      const auto variantWing = testsupport::buildStructure(
          teRibs, variant);
      designrc::geometry::MaterialShapeSet variantMaterials;
      const auto variantShape = designrc::geometry::buildStructuredWingPreview(
          variantWing, teParameters.ribThickness, nullptr, &variantMaterials);
      if (variantShape.IsNull()) return false;
      std::size_t variantSolidRibs = 0;
      for (const auto& part : variantMaterials.parts) {
        if (!part.name.starts_with("Rib ")) continue;
        bool hasSolid = false;
        for (TopExp_Explorer solids{part.shape, TopAbs_SOLID};
             solids.More(); solids.Next())
          hasSolid = true;
        if (!hasSolid) return false;
        ++variantSolidRibs;
      }
      return variantSolidRibs == teRibs.size();
    };
    if (!verifyAdditionalTeVariant(false, true)) return 124;
    if (!verifyAdditionalTeVariant(true, true)) return 125;
    return 0;
  };
  if (teSheetingSolidOnly) return verifyTeSheetingRibSolids();
  designrc::domain::StructureParameters earlyCfLeadingSheeting;
  earlyCfLeadingSheeting.leadingEdgeType = 3;
  earlyCfLeadingSheeting.leTopSheet = true;
  earlyCfLeadingSheeting.leBottomSheet = true;
  earlyCfLeadingSheeting.leTopSheetStopRib = 3;
  earlyCfLeadingSheeting.leBottomSheetStopRib = 3;
  const auto earlyCfSheeted = testsupport::buildStructure(ribs, earlyCfLeadingSheeting);
  const auto carbonLeadingEdge = std::find_if(
      earlyCfSheeted.members.begin(), earlyCfSheeted.members.end(),
      [](const auto& member) {
        return member.name == "CF tube leading edge";
      });
  if (carbonLeadingEdge == earlyCfSheeted.members.end()) return 28;
  const auto sheetingOverlapsLeadingEdgeNotch =
      [&](const std::string& name) {
        const auto sheet = std::find_if(
            earlyCfSheeted.sheeting.begin(), earlyCfSheeted.sheeting.end(),
            [&](const auto& part) { return part.name == name; });
        if (sheet == earlyCfSheeted.sheeting.end() ||
            sheet->profiles.empty() ||
            sheet->profiles.size() > carbonLeadingEdge->centers.size())
          return false;
        const double radius = carbonLeadingEdge->width * 0.5;
        for (std::size_t ribIndex = 0;
             ribIndex < sheet->profiles.size(); ++ribIndex) {
          if (sheet->profiles[ribIndex].empty()) return false;
          const auto& start = sheet->profiles[ribIndex].front();
          const auto& center = carbonLeadingEdge->centers[ribIndex];
          if (std::hypot(start.x - center.x, start.y - center.y) >=
              radius - 1.0e-5)
            return false;
        }
        return true;
      };
  if (!sheetingOverlapsLeadingEdgeNotch("Front top sheeting") ||
      !sheetingOverlapsLeadingEdgeNotch("Front bottom sheeting"))
    return 29;
  stage("early CF leading-edge sheeting");
  const auto earlyCfSheetedShape = designrc::geometry::buildStructuredWingPreview(
      earlyCfSheeted, parameters.ribThickness);
  if (earlyCfSheetedShape.IsNull()) return 8;
  designrc::domain::StructureParameters controlSheeting;
  controlSheeting.teTopSheet = true;
  controlSheeting.teTopSheetStopRib = static_cast<int>(ribs.size());
  const auto controlSheetedWing = testsupport::buildStructure(ribs, controlSheeting);
  designrc::geometry::MaterialShapeSet controlSheetingMaterials;
  stage("rear sheeting spline loft");
  const auto controlSheetedShape = designrc::geometry::buildStructuredWingPreview(
      controlSheetedWing, parameters.ribThickness, nullptr, &controlSheetingMaterials);
  if (controlSheetedShape.IsNull()) return 11;
  bool foundSplineSheeting = false;
  for (const auto& part : controlSheetingMaterials.parts) {
    if (part.name.find("sheeting") == std::string::npos) continue;
    foundSplineSheeting = true;
    std::size_t faceCount = 0;
    for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
         faces.More(); faces.Next())
      ++faceCount;
    // A continuous sheet retains sections at both faces of each rib, but each
    // interval now has only four contour faces instead of one face per sampled
    // profile segment.
    if (faceCount > 8 * ribs.size()) return 26;
  }
  if (!foundSplineSheeting) return 27;
  designrc::domain::WingParameters savedProjectParameters;
  savedProjectParameters.halfSpan = 393.7;
  savedProjectParameters.rootChord = savedProjectParameters.tipChord = 152.4;
  savedProjectParameters.sweep = 0.0;
  savedProjectParameters.dihedralDegrees = 10.0;
  savedProjectParameters.ribThickness = 3.175;
  const auto savedProjectRibs = designrc::domain::generateRibs(
      savedProjectParameters, root, root);
  designrc::domain::StructureParameters savedProjectStructure;
  savedProjectStructure.ribThickness = 3.175;
  savedProjectStructure.topSpar = savedProjectStructure.bottomSpar = true;
  savedProjectStructure.topSparHeight = savedProjectStructure.bottomSparHeight = 4.7625;
  savedProjectStructure.topSparWidth = savedProjectStructure.bottomSparWidth = 9.525;
  savedProjectStructure.leadingEdgeType = 4;
  savedProjectStructure.leadingEdgeRodOd = 2.0;
  savedProjectStructure.trailingEdgeType = 2;
  savedProjectStructure.trailingEdgeWidth = 25.4;
  savedProjectStructure.trailingEdgeHeight = 50.0;
  savedProjectStructure.leTopSheet = savedProjectStructure.leBottomSheet = true;
  savedProjectStructure.teTopSheet = savedProjectStructure.teBottomSheet = true;
  savedProjectStructure.leTopSheetThickness = savedProjectStructure.leBottomSheetThickness = 2.38125;
  savedProjectStructure.teTopSheetThickness = savedProjectStructure.teBottomSheetThickness = 2.38125;
  savedProjectStructure.leTopSheetStopRib = savedProjectStructure.leBottomSheetStopRib = 2;
  savedProjectStructure.teTopSheetStopRib = savedProjectStructure.teBottomSheetStopRib = 2;
  const auto savedProjectWing = testsupport::buildStructure(
      savedProjectRibs, savedProjectStructure);
  if (!savedProjectWing.ribs[0].booleanHoles.empty() ||
      !savedProjectWing.ribs[1].booleanHoles.empty() ||
      !savedProjectWing.ribs[2].booleanHoles.empty() ||
      savedProjectWing.ribs[0].partOutline.empty() ||
      savedProjectWing.ribs[1].partOutline.empty() ||
      savedProjectWing.ribs[2].partOutline.empty() ||
      !savedProjectWing.ribs[0].holes.empty() ||
      !savedProjectWing.ribs[1].holes.empty() ||
      !savedProjectWing.ribs[2].holes.empty()) return 9;
  stage("saved project exposed leading edge");
  const auto savedProjectShape = designrc::geometry::buildStructuredWingPreview(
      savedProjectWing, savedProjectParameters.ribThickness);
  if (savedProjectShape.IsNull()) return 10;
  stage("TE sheeting solids");
  if (const int result = verifyTeSheetingRibSolids(); result != 0)
    return result;
  const auto shape = designrc::geometry::buildWingPreview(ribs, parameters.ribThickness, false);
  std::size_t solidCount = 0;
  std::size_t meshedCapCount = 0;
  for (TopExp_Explorer explorer{shape, TopAbs_SOLID}; explorer.More(); explorer.Next()) {
    ++solidCount;
    for (TopExp_Explorer faces{explorer.Current(), TopAbs_FACE}; faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface{face};
      if (surface.GetType() != GeomAbs_Plane || std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
        continue;
      TopLoc_Location location;
      if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCapCount;
    }
  }
  for (TopExp_Explorer faces{shape, TopAbs_FACE, TopAbs_SOLID}; faces.More(); faces.Next()) {
    const auto face = TopoDS::Face(faces.Current());
    const BRepAdaptor_Surface surface{face};
    if (surface.GetType() != GeomAbs_Plane || std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
      continue;
    TopLoc_Location location;
    if (!BRep_Tool::Triangulation(face, location).IsNull()) ++meshedCapCount;
  }
  if (solidCount != parameters.ribCount || meshedCapCount < parameters.ribCount * 2) return 2;

  designrc::domain::StructureParameters structureParameters;
  structureParameters.topSpar = true;
  structureParameters.bottomSpar = true;
  structureParameters.shearWebs = true;
  structureParameters.topRearSpar = true;
  structureParameters.turbulators = true;
  structureParameters.turbulatorCount = 2;
  structureParameters.leadingEdgeType = 2;
  structureParameters.leadingEdgeHeight = 50.0;
  structureParameters.trailingEdgeType = 2;
  structureParameters.trailingEdgeHeight = 50.0;
  structureParameters.trailingEdgeSlotted = true;
  structureParameters.trailingEdgeSlotDepth = 6.0;
  structureParameters.ailerons = true;
  structureParameters.aileronStartRib = 3;
  structureParameters.aileronStopRib = 8;
  structureParameters.aileronWidth = 35.0;
  structureParameters.aileronHingePostWidth = 6.0;
  structureParameters.aileronHingePostHeight = 10.0;
  structureParameters.flaps = true;
  structureParameters.flapStartRib = 2;
  structureParameters.flapStopRib = 3;
  structureParameters.flapWidth = structureParameters.aileronWidth;
  structureParameters.flapHingePostWidth = 6.0;
  structureParameters.flapHingePostHeight = 10.0;
  structureParameters.controlSurfaceGap = 1.5;
  structureParameters.behindSparJoiner = true;
  structureParameters.behindSparJoinerType = 2;
  structureParameters.fiftyPercentJoiner = true;
  structureParameters.fiftyPercentJoinerType = 1;
  structureParameters.teBottomSheet = true;
  structureParameters.teTopSheet = true;
  structureParameters.teTopSheetStopRib = 3;
  structureParameters.teBottomSheetStopRib = 3;
  const auto structured = testsupport::buildStructure(ribs, structureParameters);
  designrc::geometry::MaterialShapeSet structuredMaterials;
  std::atomic_bool reportedRibs{false};
  std::atomic_bool reportedSheeting{false};
  std::atomic_bool reportedMeshing{false};
  stage("full structured wing");
  const auto structuredShape =
      designrc::geometry::buildStructuredWingPreview(
          structured, parameters.ribThickness, nullptr, &structuredMaterials,
          [&](const int, const std::string& message) {
            reportedRibs = reportedRibs ||
                message.find("Rib Solids") != std::string::npos;
            reportedSheeting = reportedSheeting ||
                message.find("sheeting") != std::string::npos;
            reportedMeshing = reportedMeshing ||
                message.find("Meshing") != std::string::npos;
          });
  if (!reportedRibs || !reportedSheeting || !reportedMeshing) return 40;
  bool foundSplineLeadingEdge = false;
  bool foundSplineTrailingEdge = false;
  bool foundSplineAileron = false;
  bool foundSplineFlap = false;
  for (const auto& part : structuredMaterials.parts) {
    const bool leadingEdge =
        part.name.find("leading edge") != std::string::npos;
    const bool trailingEdge =
        part.name.find("trailing edge") != std::string::npos;
    const bool aileron = part.name == "Aileron";
    const bool flap = part.name == "Flap";
    if (!leadingEdge && !trailingEdge && !aileron && !flap) continue;
    std::size_t faceCount = 0;
    std::size_t splineFaceCount = 0;
    std::size_t solidCount = 0;
    for (TopExp_Explorer faces{part.shape, TopAbs_FACE};
         faces.More(); faces.Next()) {
      ++faceCount;
      const BRepAdaptor_Surface surface{TopoDS::Face(faces.Current())};
      if (surface.GetType() == GeomAbs_BSplineSurface)
        ++splineFaceCount;
    }
    for (TopExp_Explorer solids{part.shape, TopAbs_SOLID};
         solids.More(); solids.Next())
      ++solidCount;
    if (faceCount > 6) {
      std::cerr << part.name << " has " << faceCount
                << " faces after spline lofting\n";
      return 43;
    }
    if (splineFaceCount < 2) {
      std::cerr << part.name << " has only " << splineFaceCount
                << " B-spline faces\n";
      return 45;
    }
    if ((aileron || flap) && solidCount != 1) {
      std::cerr << part.name << " has " << solidCount
                << " solids instead of one\n";
      return 46;
    }
    foundSplineLeadingEdge = foundSplineLeadingEdge || leadingEdge;
    foundSplineTrailingEdge = foundSplineTrailingEdge || trailingEdge;
    foundSplineAileron = foundSplineAileron || aileron;
    foundSplineFlap = foundSplineFlap || flap;
  }
  if (!foundSplineLeadingEdge || !foundSplineTrailingEdge ||
      !foundSplineAileron || !foundSplineFlap)
    return 44;
  std::size_t structuredSolidCount = 0;
  for (TopExp_Explorer explorer{structuredShape, TopAbs_SOLID}; explorer.More(); explorer.Next())
    ++structuredSolidCount;
  if (structuredSolidCount < parameters.ribCount) return 4;

  designrc::domain::StructureParameters carbonParameters;
  carbonParameters.carbonSpar = 1;
  carbonParameters.leadingEdgeType = 3;
  const auto carbon = testsupport::buildStructure(ribs, carbonParameters);
  auto numberedCarbon = carbon;
  for (auto& member : numberedCarbon.members)
    if (member.name.find("leading edge") != std::string::npos) member.name = "LE1";
  designrc::geometry::MaterialShapeSet carbonMaterials;
  stage("numbered carbon members");
  const auto numberedCarbonShape = designrc::geometry::buildStructuredWingPreview(
      numberedCarbon, parameters.ribThickness, nullptr, &carbonMaterials);
  if (numberedCarbonShape.IsNull() ||
      !TopExp_Explorer{carbonMaterials.carbonFiber, TopAbs_FACE}.More()) return 12;
  stage("carbon members");
  const auto carbonShape = designrc::geometry::buildStructuredWingPreview(carbon, parameters.ribThickness);
  if (carbonShape.IsNull()) return 5;
  std::size_t carbonSolids = 0;
  std::size_t carbonMeshedCaps = 0;
  for (TopExp_Explorer solids{carbonShape, TopAbs_SOLID}; solids.More(); solids.Next()) {
    ++carbonSolids;
    for (TopExp_Explorer faces{solids.Current(), TopAbs_FACE}; faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface{face};
      if (surface.GetType() != GeomAbs_Plane ||
          std::abs(surface.Plane().Axis().Direction().Y()) < 0.99)
        continue;
      TopLoc_Location location;
      if (!BRep_Tool::Triangulation(face, location).IsNull()) ++carbonMeshedCaps;
    }
  }
  if (carbonSolids < parameters.ribCount + 2 ||
      carbonMeshedCaps < parameters.ribCount * 2) return 6;
  const auto assemblyShape = designrc::geometry::buildMirroredWingAssemblyPreview(
      {carbon}, {parameters.ribThickness});
  if (assemblyShape.IsNull()) return 7;
  designrc::domain::StructureParameters multiSparParameters;
  multiSparParameters.spars = {
      {30, 0, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {45, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {65, 1, 1, 2, 5.0, 9.0, 6.0, 5.0, 6.0, 7.0, 1.5},
      {30, 1, 0, 0, 4.0, 8.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  multiSparParameters.sparShearWebs = true;
  multiSparParameters.sparShearWebThickness = 3.0;
  multiSparParameters.leTopSheet = multiSparParameters.teTopSheet = true;
  multiSparParameters.leBottomSheet = multiSparParameters.teBottomSheet = true;
  multiSparParameters.leTopSheetStopRib = multiSparParameters.teTopSheetStopRib = 3;
  multiSparParameters.leBottomSheetStopRib = multiSparParameters.teBottomSheetStopRib = 3;
  const auto multiSparWing = testsupport::buildStructure(
      ribs, multiSparParameters);
  stage("multiple spars and sheeting");
  const auto multiSparShape = designrc::geometry::buildStructuredWingPreview(
      multiSparWing, parameters.ribThickness);
  if (multiSparShape.IsNull()) return 13;

  designrc::domain::StructureParameters collidingSpars;
  collidingSpars.spars = {
      {40, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {40, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  bool namedCollision = false;
  try {
    const auto collidingWing = testsupport::buildStructure(
        ribs, collidingSpars);
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        collidingWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedCollision = message.find("Spar 1") != std::string::npos &&
        message.find("Spar 2") != std::string::npos;
  }
  if (!namedCollision) return 14;
  designrc::domain::StructureParameters joinerCollisionParameters;
  joinerCollisionParameters.spars = {
      {40, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 8.0, 6.0, 1.0}};
  auto joinerCollisionWing = testsupport::buildStructure(
      ribs, joinerCollisionParameters);
  const auto& spar = joinerCollisionWing.members.front();
  designrc::domain::JoinerPart collidingJoiner;
  collidingJoiner.name = "Fixed Joiner 1 CF Rod";
  collidingJoiner.kind = designrc::domain::SpanMemberKind::Rod;
  collidingJoiner.outerDiameter = 6.0;
  collidingJoiner.hasExplicitEndpoints = true;
  collidingJoiner.innerEndpoint = {
      ribs[0].leadingEdgeOffset + spar.centers[0].x,
      ribs[0].spanPosition, ribs[0].dihedralHeight + spar.centers[0].y};
  collidingJoiner.outerEndpoint = {
      ribs[1].leadingEdgeOffset + spar.centers[1].x,
      ribs[1].spanPosition, ribs[1].dihedralHeight + spar.centers[1].y};
  joinerCollisionWing.joiners.push_back(collidingJoiner);
  bool namedJoinerCollision = false;
  try {
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        joinerCollisionWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedJoinerCollision = message.find("Fixed Joiner 1") != std::string::npos &&
        message.find("Spar 1") != std::string::npos;
  }
  if (!namedJoinerCollision) return 15;

  auto overlappingJoinerWing = testsupport::buildStructure(ribs, {});
  collidingJoiner.name = "Fixed Joiner 1 CF Tube";
  collidingJoiner.kind = designrc::domain::SpanMemberKind::Tube;
  collidingJoiner.outerDiameter = 6.0;
  collidingJoiner.innerDiameter = 5.0;
  overlappingJoinerWing.joiners.push_back(collidingJoiner);
  collidingJoiner.name = "Fixed Joiner 2 CF Rod";
  collidingJoiner.kind = designrc::domain::SpanMemberKind::Rod;
  collidingJoiner.innerDiameter = 0.0;
  overlappingJoinerWing.joiners.push_back(collidingJoiner);
  bool namedJoinerPairCollision = false;
  try {
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        overlappingJoinerWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedJoinerPairCollision =
        message.find("Joiner collision") != std::string::npos &&
        message.find("Fixed Joiner 1") != std::string::npos &&
        message.find("Fixed Joiner 2") != std::string::npos;
  }
  if (!namedJoinerPairCollision) return 115;

  designrc::domain::StructureParameters woodJoinerPairParameters;
  woodJoinerPairParameters.topSpar = true;
  woodJoinerPairParameters.bottomSpar = true;
  woodJoinerPairParameters.centerSparWoodJoiner = true;
  auto overlappingWoodJoinerWing = testsupport::buildStructure(
      ribs, woodJoinerPairParameters);
  if (overlappingWoodJoinerWing.joiners.size() != 1) return 116;
  overlappingWoodJoinerWing.joiners.front().name = "Joiner 1";
  auto secondWoodJoiner = overlappingWoodJoinerWing.joiners.front();
  secondWoodJoiner.name = "Joiner 2";
  overlappingWoodJoinerWing.joiners.push_back(std::move(secondWoodJoiner));
  bool namedWoodJoinerPairCollision = false;
  try {
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        overlappingWoodJoinerWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedWoodJoinerPairCollision =
        message.find("Joiner collision") != std::string::npos &&
        message.find("Joiner 1") != std::string::npos &&
        message.find("Joiner 2") != std::string::npos;
  }
  if (!namedWoodJoinerPairCollision) return 117;

  auto woodJoinerContactWing = joinerCollisionWing;
  woodJoinerContactWing.joiners.back().name = "Wood Fixed Joiner 1";
  const auto woodJoinerContactShape = designrc::geometry::buildStructuredWingPreview(
      woodJoinerContactWing, parameters.ribThickness);
  if (woodJoinerContactShape.IsNull()) return 16;

  auto unmirroredJoinerWing = testsupport::buildStructure(ribs, {});
  collidingJoiner.name = "Alignment Pin 1 CF";
  collidingJoiner.innerEndpoint = {0.80 * ribs[0].chord, ribs[0].spanPosition, 0.0};
  collidingJoiner.outerEndpoint = {0.80 * ribs[1].chord, ribs[1].spanPosition, 0.0};
  collidingJoiner.mirrorInAssembly = false;
  unmirroredJoinerWing.joiners.push_back(collidingJoiner);
  auto steelJoiner = collidingJoiner;
  steelJoiner.name = "Removable Joiner 1 This Panel Steel";
  steelJoiner.innerEndpoint.x = 0.72 * ribs[0].chord;
  steelJoiner.outerEndpoint.x = 0.72 * ribs[1].chord;
  unmirroredJoinerWing.joiners.push_back(steelJoiner);
  auto fiberglassJoiner = collidingJoiner;
  fiberglassJoiner.name = "Removable Joiner 2 This Panel Fiberglass";
  fiberglassJoiner.kind = designrc::domain::SpanMemberKind::Tube;
  fiberglassJoiner.outerDiameter = 4.0;
  fiberglassJoiner.innerDiameter = 3.0;
  fiberglassJoiner.innerEndpoint.x = 0.88 * ribs[0].chord;
  fiberglassJoiner.outerEndpoint.x = 0.88 * ribs[1].chord;
  unmirroredJoinerWing.joiners.push_back(fiberglassJoiner);
  designrc::geometry::MaterialShapeSet unmirroredMaterials;
  const auto unmirroredShape = designrc::geometry::buildStructuredWingPreview(
      unmirroredJoinerWing, parameters.ribThickness, nullptr, &unmirroredMaterials);
  if (unmirroredShape.IsNull() ||
      !TopExp_Explorer{unmirroredMaterials.unmirroredCarbonFiber, TopAbs_FACE}.More() ||
      !TopExp_Explorer{unmirroredMaterials.unmirroredSteel, TopAbs_FACE}.More() ||
      !TopExp_Explorer{unmirroredMaterials.unmirroredFiberglass, TopAbs_FACE}.More())
    return 17;
  designrc::domain::StructureParameters spoilerParameters;
  spoilerParameters.spoilers = true;
  spoilerParameters.spoilerStartRib = 3;
  spoilerParameters.spoilerEndRib = 7;
  spoilerParameters.spoilerChordLocationPercent = 35;
  spoilerParameters.spoilerWidth = 25.4;
  spoilerParameters.spoilerThickness = 3.0;
  spoilerParameters.spoilerFrameRailWidth = 6.0;
  spoilerParameters.spoilerSupportRailHeight = 3.0;
  spoilerParameters.leTopSheet = true;
  spoilerParameters.teTopSheet = true;
  spoilerParameters.leTopSheetStopRib = static_cast<int>(ribs.size());
  spoilerParameters.teTopSheetStopRib = static_cast<int>(ribs.size());
  const auto spoilerWing = testsupport::buildStructure(ribs, spoilerParameters);
  const auto spoilerShape = designrc::geometry::buildStructuredWingPreview(
      spoilerWing, parameters.ribThickness);
  if (spoilerShape.IsNull()) return 18;
  auto lightenedSpoilerParameters = spoilerParameters;
  lightenedSpoilerParameters.spoilerLighteningHoles = true;
  lightenedSpoilerParameters.spoilerMinimumWoodMargin = 6.0;
  lightenedSpoilerParameters.spoilerMinimumCircleDistance = 12.0;
  const auto lightenedSpoilerWing = testsupport::buildStructure(
      ribs, lightenedSpoilerParameters);
  designrc::geometry::MaterialShapeSet lightenedSpoilerMaterials;
  const auto lightenedSpoilerShape =
      designrc::geometry::buildStructuredWingPreview(
          lightenedSpoilerWing, parameters.ribThickness, nullptr,
          &lightenedSpoilerMaterials);
  if (lightenedSpoilerShape.IsNull()) return 35;
  const auto lightenedSpoilerPart = std::find_if(
      lightenedSpoilerMaterials.parts.begin(),
      lightenedSpoilerMaterials.parts.end(),
      [](const auto& part) { return part.name == "Spoiler"; });
  if (lightenedSpoilerPart == lightenedSpoilerMaterials.parts.end())
    return 36;
  std::size_t lightenedSpoilerFaceCount = 0;
  for (TopExp_Explorer faces{
           lightenedSpoilerPart->shape, TopAbs_FACE};
       faces.More(); faces.Next())
    ++lightenedSpoilerFaceCount;
  if (lightenedSpoilerFaceCount <= 6) return 37;
  auto ribLighteningParameters = spoilerParameters;
  ribLighteningParameters.spoilers = false;
  ribLighteningParameters.ribLighteningHoles = true;
  ribLighteningParameters.ribLighteningStartRib = 3;
  ribLighteningParameters.ribLighteningStopRib = 5;
  ribLighteningParameters.ribLighteningMinimumWoodMargin = 3.0;
  ribLighteningParameters.ribLighteningMinimumHoleDistance = 8.0;
  auto ribLightenedWing = testsupport::buildStructure(
      ribs, ribLighteningParameters);
  designrc::domain::addRibLighteningHoles(
      ribLightenedWing, ribLighteningParameters);
  if (ribLightenedWing.ribs[2].internalCutouts.empty() ||
      ribLightenedWing.ribs[3].internalCutouts.empty() ||
      ribLightenedWing.ribs[4].internalCutouts.empty())
    return 38;
  const auto ribLightenedShape =
      designrc::geometry::buildStructuredWingPreview(
          ribLightenedWing, parameters.ribThickness);
  if (ribLightenedShape.IsNull()) return 39;
  auto centerSpoilerParameters = spoilerParameters;
  centerSpoilerParameters.spoilerStartRib = 1;
  centerSpoilerParameters.spoilerEndRib = 5;
  centerSpoilerParameters.leTopSheet = false;
  centerSpoilerParameters.teTopSheet = false;
  centerSpoilerParameters.spoilerLighteningHoles = true;
  centerSpoilerParameters.spoilerMinimumWoodMargin = 6.0;
  centerSpoilerParameters.spoilerMinimumCircleDistance = 12.0;
  const auto centerSpoilerWing = testsupport::buildStructure(
      ribs, centerSpoilerParameters);
  designrc::geometry::MaterialShapeSet centerSpoilerMaterials;
  const auto centerSpoilerShape =
      designrc::geometry::buildStructuredWingPreview(
          centerSpoilerWing, parameters.ribThickness, nullptr,
          &centerSpoilerMaterials);
  if (centerSpoilerShape.IsNull()) return 21;
  std::size_t centerLongParts = 0;
  for (const auto& part : centerSpoilerMaterials.parts) {
    if (part.name != "Spoiler" &&
        !part.name.starts_with("Spoiler Frame Rail"))
      continue;
    ++centerLongParts;
    if (part.mirrorInAssembly || part.shape.ShapeType() != TopAbs_SOLID)
      return 22;
  }
  if (centerLongParts != 3) return 23;

  designrc::domain::StructureParameters wiringSparCollision;
  wiringSparCollision.spars = {
      {45, 2, 1, 1, 5.0, 9.0, 6.0, 5.0, 8.0, 6.0, 1.0}};
  wiringSparCollision.wiringHoles = true;
  wiringSparCollision.wiringHoleStartRib = 2;
  wiringSparCollision.wiringHoleEndRib = 2;
  wiringSparCollision.wiringHoleChordLocationPercent = 45;
  bool namedWiringSparCollision = false;
  try {
    const auto wired = testsupport::buildStructure(ribs, wiringSparCollision);
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        wired, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedWiringSparCollision = message.find("Wiring Hole R2") != std::string::npos &&
        message.find("Spar 1") != std::string::npos;
  }
  if (!namedWiringSparCollision) return 19;

  designrc::domain::StructureParameters wiringJoinerParameters;
  wiringJoinerParameters.wiringHoles = true;
  wiringJoinerParameters.wiringHoleStartRib = 1;
  wiringJoinerParameters.wiringHoleEndRib = 2;
  wiringJoinerParameters.wiringHoleChordLocationPercent = 70;
  auto wiringJoinerWing = testsupport::buildStructure(
      ribs, wiringJoinerParameters);
  designrc::domain::JoinerPart wiringJoiner;
  wiringJoiner.name = "Fixed Joiner 1 CF Rod";
  wiringJoiner.kind = designrc::domain::SpanMemberKind::Rod;
  wiringJoiner.outerDiameter = 4.0;
  wiringJoiner.hasExplicitEndpoints = true;
  const auto wiringCenterX = [](const auto& rib) {
    return 0.70 * rib.chord + 9.525 * 0.5;
  };
  wiringJoiner.innerEndpoint = {ribs[0].leadingEdgeOffset + wiringCenterX(ribs[0]),
      ribs[0].spanPosition, ribs[0].dihedralHeight};
  wiringJoiner.outerEndpoint = {ribs[1].leadingEdgeOffset + wiringCenterX(ribs[1]),
      ribs[1].spanPosition, ribs[1].dihedralHeight};
  wiringJoinerWing.joiners.push_back(wiringJoiner);
  bool namedWiringJoinerCollision = false;
  try {
    static_cast<void>(designrc::geometry::buildStructuredWingPreview(
        wiringJoinerWing, parameters.ribThickness));
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    namedWiringJoinerCollision = message.find("Wiring Hole") != std::string::npos &&
        message.find("Fixed Joiner 1") != std::string::npos;
  }
  if (!namedWiringJoinerCollision) return 20;

  designrc::domain::StructureParameters ribletParameters;
  ribletParameters.leadingEdgeType = 3;
  ribletParameters.leadingEdgeTubeOd = 4.0;
  ribletParameters.leadingEdgeTubeId = 3.0;
  ribletParameters.spars = {
      {30, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  ribletParameters.riblets = true;
  ribletParameters.ribletStartRib = 2;
  ribletParameters.ribletEndRib = 5;
  ribletParameters.ribletsPerBay = 2;
  auto ribletWing =
      testsupport::buildStructure(ribs, ribletParameters);
  for (std::size_t index = 0; index < ribletWing.ribs.size(); ++index)
    ribletWing.ribs[index].name = "R" + std::to_string(index + 1);
  designrc::domain::addRiblets(ribletWing, ribletParameters);
  designrc::geometry::MaterialShapeSet ribletMaterials;
  const auto ribletShape =
      designrc::geometry::buildStructuredWingPreview(
          ribletWing, parameters.ribThickness, nullptr,
          &ribletMaterials);
  if (ribletShape.IsNull()) return 24;
  if (std::count_if(
          ribletMaterials.parts.begin(), ribletMaterials.parts.end(),
          [](const auto& part) {
            return part.name.size() > 2 &&
                part.name.front() == 'R' &&
                std::isalpha(static_cast<unsigned char>(part.name.back()));
          }) != 6)
    return 25;
  return 0;
}

int main(int argc, char* argv[]) {
  try {
    return runTest(argc, argv);
  } catch (const Standard_Failure& error) {
    std::cerr << "OCCT: " << error.GetMessageString() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  }
  return 3;
}
