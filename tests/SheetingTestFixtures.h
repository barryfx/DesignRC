#pragma once
#include "TestWingStructure.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace sheettest {
using namespace designrc::domain;
inline WingParameters panel() {
  WingParameters p;
  p.rootChord = 200; p.tipChord = 160; p.sweep = 20;
  p.halfSpan = 180; p.ribCount = 5; p.ribThickness = 2; p.dihedralDegrees = 0;
  return p;
}
inline StructureParameters stock() {
  StructureParameters s;
  s.ribThickness = 2;
  for (const auto [percent, side] : {std::pair{25., 0}, {30., 1}, {10., 0}, {15., 1}, {5., 2}, {65., 0}, {60., 1}}) {
    SparParameters spar;
    spar.material = 0; spar.verticalLocation = side;
    spar.chordLocationPercent = spar.tipChordLocationPercent = percent;
    spar.woodWidth = 4; spar.woodHeight = 2;
    s.spars.push_back(spar);
  }
  return s;
}
inline void sheet(StructureParameters& s, bool top, bool rear, int mode, bool cover) {
  if (rear && top) { s.teTopSheet = true; s.teTopSheetStopRib = 3; s.teTopSheetThickness = 1; s.teTopSheetBoundaryMode = mode; s.teTopSheetCoverRibs = cover; s.teTopSheetStopChordPercent = 40; }
  if (rear && !top) { s.teBottomSheet = true; s.teBottomSheetStopRib = 3; s.teBottomSheetThickness = 1; s.teBottomSheetBoundaryMode = mode; s.teBottomSheetCoverRibs = cover; s.teBottomSheetStopChordPercent = 40; }
  if (!rear && top) { s.leTopSheet = true; s.leTopSheetStopRib = 3; s.leTopSheetThickness = 1; s.leTopSheetBoundaryMode = mode; s.leTopSheetCoverRibs = cover; s.leTopSheetStopChordPercent = 40; }
  if (!rear && !top) { s.leBottomSheet = true; s.leBottomSheetStopRib = 3; s.leBottomSheetThickness = 1; s.leBottomSheetBoundaryMode = mode; s.leBottomSheetCoverRibs = cover; s.leBottomSheetStopChordPercent = 40; }
}
inline void testBoundaries() {
  const auto p = panel();
  const auto foil = AirfoilProfile::nacaSymmetric(.12);
  const auto ribs = generateRibs(p, foil, foil);
  const auto original = stock();
  const auto baseline = testsupport::buildStructure(ribs, original);
  for (bool top : {true, false}) for (bool rear : {true, false})
    for (int mode : {kSheetStopChord, kSheetUpToSpar, kSheetMidSpar}) for (bool cover : {false, true}) {
      auto s = original;
      sheet(s, top, rear, mode, cover);
      const auto wing = testsupport::buildStructure(ribs, s);
      assert(wing.sheeting.size() == 1 && wing.sheeting.front().profiles.size() == 3);
      const auto& profile = wing.sheeting.front().profiles.front();
      double edge = rear ? 1e9 : -1e9;
      for (const auto point : profile) edge = rear ? std::min(edge, point.x) : std::max(edge, point.x);
      const double first = top ? 20 : 30;
      const double expected = mode == kSheetStopChord ? 80 : mode == kSheetMidSpar ? first :
          first + (rear != cover ? 2 : -2);
      assert(std::abs(edge - expected) < 1e-7);
      for (std::size_t i = 0; i < original.spars.size(); ++i) {
        const auto& spar = original.spars[i];
        const double x = spar.chordLocationPercent * 2;
        const bool underSheet = rear ? x + 2 > expected + 1e-8 : x - 2 < expected - 1e-8;
        const bool inset = cover && spar.verticalLocation == (top ? 0 : 1) && underSheet;
        for (std::size_t r = 0; r < ribs.size(); ++r) {
          const double movement = wing.members[i].centers[r].y - baseline.members[i].centers[r].y;
          if (!inset) assert(std::abs(movement) < 1e-8);
          else assert((top ? -movement : movement) >= 1 - 1e-8);
          if (inset && r > 0)
            assert(std::abs(movement - (wing.members[i].centers[0].y - baseline.members[i].centers[0].y)) < 1e-7);
        }
      }
    }
  // Caps fill the exposed skin and meet sheeting, including the uncovered
  // half of a recessed spar at a Mid Spar boundary.
  for (bool top : {true, false}) for (bool rear : {true, false})
    for (int mode : {kSheetStopChord, kSheetUpToSpar, kSheetMidSpar}) for (bool tube : {false, true}) {
      auto capped = original;
      if (tube) for (auto& spar : capped.spars) {
        spar.material = 1; spar.type = 0; spar.tubeOd = 4; spar.tubeId = 3;
      }
      sheet(capped, top, rear, mode, true);
      capped.topRibCaps = capped.bottomRibCaps = true;
      capped.topRibCapThickness = capped.bottomRibCapThickness = 1;
      capped.topRibCapWidth = capped.bottomRibCapWidth = 3;
      const auto wing = testsupport::buildStructure(ribs, capped);
      for (std::size_t r = 0; r < 3; ++r) {
        const auto& skin = wing.sheeting.front().profiles[r];
        double boundary = rear ? 1e9 : -1e9;
        for (auto point : skin) boundary = rear ? std::min(boundary, point.x) : std::max(boundary, point.x);
        bool meets = false;
        for (const auto& cap : wing.ribCaps) {
          if (cap.top != top || cap.ribIndex != r) continue;
          double a = 1e9, b = -1e9;
          for (auto point : cap.profile) { a = std::min(a, point.x); b = std::max(b, point.x); }
          assert(rear ? b <= boundary + 1e-7 : a >= boundary - 1e-7);
          if (std::abs((rear ? b : a) - boundary) < 1e-7) meets = true;
        }
        if (!meets) throw std::runtime_error("Cap fails to meet sheet: top=" + std::to_string(top) + " rear=" + std::to_string(rear) + " mode=" + std::to_string(mode) + " rib=" + std::to_string(r) + " boundary=" + std::to_string(boundary));
      }
    }
  // Complementary Mid Spar sheets meet without double-recessing their shared stock.
  auto s = original;
  sheet(s, true, false, kSheetMidSpar, true);
  sheet(s, true, true, kSheetMidSpar, true);
  const auto joined = testsupport::buildStructure(ribs, s);
  assert(joined.sheeting.size() == 2);
  const double depth = baseline.members[2].centers[0].y - joined.members[2].centers[0].y;
  assert(depth >= 1 && depth < 2);
}
}
