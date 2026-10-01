#include "domain/AirfoilProfile.h"
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
#include <BRepCheck_Analyzer.hxx>
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
#include <numbers>

int runTest(int argc, char* argv[]) {
  using designrc::domain::AirfoilProfile;
  const auto stage = [](const char* name) {
    std::cerr << "Geometry regression stage: " << name << '\n';
  };
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
  const bool focusedGeometryOnly =
      teSheetingSolidOnly || multiSparSheetingSolidOnly ||
      woodJoinerCollisionOnly || sparEndFacesOnly || buildTabClearanceOnly || cancelLighteningOnly;
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
    auto wing = applyWingStructure(generateRibs(p, foil, foil), s);
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
      s.ribThickness = p.ribThickness; s.addBuildTabs = true;
      s.spars = {{25, 1, 0, 2, 3.0, 8.0}};
      auto wing = applyWingStructure(tabRibs, s);
      // Inject a residual lip, or a curve dipping below its end points, to
      // verify the independent completed-solid correction actually runs.
      if (twist != 0.0) {
        auto& rib = wing.ribs.front();
        bool injected = false;
        for (auto& segment : rib.outlineSegments) {
          if (segment.points.size() != 2) continue;
          const auto a = segment.points[0], b = segment.points[1];
          if (std::abs(std::abs(b.x - a.x) - 25.4 * 3.0 / 16.0) > 1.0e-7) continue;
          segment.points.insert(segment.points.begin() + 1,
              Point2{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5 - 0.4});
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
        const auto& plane = *wing.ribs.at(checked++).buildPlane;
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
      if (checked != 3) throw std::runtime_error("Missing tabbed ribs");
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
      const auto wing = applyWingStructure(endRibs, s);
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
    auto wing = designrc::domain::applyWingStructure(ribs, structure);
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
    const auto wing = designrc::domain::applyWingStructure(ribs, structure);
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
    auto wing = designrc::domain::applyWingStructure(teRibs, structure);
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
      const auto variantWing = designrc::domain::applyWingStructure(
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
  const auto earlyCfSheeted = designrc::domain::applyWingStructure(ribs, earlyCfLeadingSheeting);
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
  const auto controlSheetedWing = designrc::domain::applyWingStructure(ribs, controlSheeting);
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
  const auto savedProjectWing = designrc::domain::applyWingStructure(
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
  const auto structured = designrc::domain::applyWingStructure(ribs, structureParameters);
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
  const auto carbon = designrc::domain::applyWingStructure(ribs, carbonParameters);
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
  const auto multiSparWing = designrc::domain::applyWingStructure(
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
    const auto collidingWing = designrc::domain::applyWingStructure(
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
  auto joinerCollisionWing = designrc::domain::applyWingStructure(
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

  auto overlappingJoinerWing = designrc::domain::applyWingStructure(ribs, {});
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
  auto overlappingWoodJoinerWing = designrc::domain::applyWingStructure(
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

  auto unmirroredJoinerWing = designrc::domain::applyWingStructure(ribs, {});
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
  const auto spoilerWing = designrc::domain::applyWingStructure(ribs, spoilerParameters);
  const auto spoilerShape = designrc::geometry::buildStructuredWingPreview(
      spoilerWing, parameters.ribThickness);
  if (spoilerShape.IsNull()) return 18;
  auto lightenedSpoilerParameters = spoilerParameters;
  lightenedSpoilerParameters.spoilerLighteningHoles = true;
  lightenedSpoilerParameters.spoilerMinimumWoodMargin = 6.0;
  lightenedSpoilerParameters.spoilerMinimumCircleDistance = 12.0;
  const auto lightenedSpoilerWing = designrc::domain::applyWingStructure(
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
  auto ribLightenedWing = designrc::domain::applyWingStructure(
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
  const auto centerSpoilerWing = designrc::domain::applyWingStructure(
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
    const auto wired = designrc::domain::applyWingStructure(ribs, wiringSparCollision);
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
  auto wiringJoinerWing = designrc::domain::applyWingStructure(
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
      designrc::domain::applyWingStructure(ribs, ribletParameters);
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
