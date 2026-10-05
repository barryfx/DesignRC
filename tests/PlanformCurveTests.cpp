#include "domain/PlanformCurves.h"
#include "domain/WingDesign.h"
#include "domain/WingStructure.h"
#include "domain/DxfExporter.h"
#include <QTemporaryDir>
#undef slots
#include <cassert>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace designrc::domain;
int main(int argc, char* argv[]) {
  if (argc == 4 && std::string{argv[1]} == "--import") {
    try {
      const auto curve = importPlanformCurves(std::filesystem::path{argv[2]}, std::stod(argv[3]));
      std::cout << "Import passed: spans " << curve.leadingSourceSpan << ", " << curve.trailingSourceSpan
                << " mm; root/tip chords " << curve.chord(0) << ", " << curve.chord(1) << " mm\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
#ifdef _WIN32
  _set_error_mode(_OUT_TO_STDERR);
#endif
  QTemporaryDir directory;
  assert(directory.isValid());
  const auto load = [&](const std::string& data, const char* extension, double span = 300) {
    const auto path = std::filesystem::path{directory.path().toStdWString()} /
                      (std::string{"curves"} + extension);
    {
      std::ofstream out{path};
      out << data;
    }
    return importPlanformCurves(path, span);
  };
  const auto svg = [](std::string body, std::string dimensions =
                                            "width='250mm' height='100mm' viewBox='0 0 250 100'") {
    // Keep span longer than chord under the automatic long-axis rule.
    return "<svg xmlns='http://www.w3.org/2000/svg' " + dimensions +
           "><g transform='scale(1 3)'>" + body + "</g></svg>";
  };
  const auto rejected = [&](std::string data, const char* ext = ".svg", double span = 300) {
    bool failed = false;
    try {
      (void)load(data, ext, span);
    } catch (const std::invalid_argument&) {
      failed = true;
    }
    assert(failed);
  };
  const auto valid =
      svg("<path d='M 2 1 C -8 34 -8 68 2 101'/><path d='M 202 -2 Q 242 48 162 98'/>");
  auto curve = load(valid, ".svg");
  const auto alongX = load(
      "<svg xmlns='http://www.w3.org/2000/svg' width='500mm' height='200mm' viewBox='0 0 500 200'>"
      "<path d='M0 0 Q250 20 500 15'/><path d='M0 155 L500 128'/></svg>", ".svg", 500);
  assert(alongX.chord(0) == 155 && alongX.chord(1) == 113);
  assert(alongX.leadingSourceSpan == 500 && alongX.trailingSourceSpan == 500);
  const auto compound = load(svg("<path d='M0 0 L0 100 M200 0 L160 100'/>"), ".svg");
  assert(compound.chord(0) == 200 && compound.chord(1) == 160);
  auto shiftedCanvas = load(svg("<path d='M0 0 Q-20 50 0 100'/><path d='M200 0 L160 100'/>",
                                "width='300mm' height='140mm' viewBox='-50 -20 300 140'"),
                            ".svg");
  assert(shiftedCanvas.leadingX(0) == 0 && std::abs(shiftedCanvas.leadingX(.5) + 10) < .02);
  auto editorMetadata =
      load("<!DOCTYPE svg PUBLIC '-//W3C//DTD SVG 1.1//EN' "
           "'http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd'>"
           "<svg xmlns='http://www.w3.org/2000/svg' "
           "xmlns:sodipodi='http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd' "
           "width='250mm' height='100mm' viewBox='0 0 250 100' transform='scale(1 3)'><sodipodi:namedview/>"
           "<path d='M0 0 L0 100'/><path d='M200 0 L160 100'/></svg>",
           ".svg");
  assert(editorMetadata.chord(0) == 200);

  assert(std::abs(curve.chord(0) - 200) < 1e-8);
  assert(std::abs(curve.chord(1) - 160) < 1e-8);
  assert(curve.leadingX(.5) < -7.4);
  assert(curve.trailingX(.5) > 199.9);
  assert(curve.area(100) > 18000);
  assert(curve.leading.front().y == 0 && curve.trailing.front().y == 0);
  assert(curve.leading.back().y == 1 && curve.trailing.back().y == 1);
  curve.validate(306.35);
  bool mismatch = false;
  try {
    curve.validate(306.36);
  } catch (...) {
    mismatch = true;
  }
  assert(mismatch);
  auto inches = load(svg("<line x1='0' y1='0' x2='0' y2='4'/><line x1='8' y1='0' x2='6' y2='4'/>",
                         "width='10in' height='4in' viewBox='0 0 10 4'"),
                     ".svg", 304.8);
  assert(std::abs(inches.chord(0) - 203.2) < 1e-8);
  auto transformed = load(svg("<g transform='translate(1 0)'><path d='m 0 0 c -10 33 -10 66 0 "
                              "100'/><path d='M 200 0 L 160 100'/></g>"),
                          ".svg");
  assert(transformed.leadingX(.5) < -7.4);
  auto quadratic =
      load(svg("<path d='M0 0 Q -10 25 0 50 T 0 100'/><path d='M200 0 L160 100'/>"), ".svg");
  assert(quadratic.leadingX(.25) < -4.9 && quadratic.leadingX(.75) > 4.9);
  auto arc = load(svg("<path d='M0 0 A100 100 0 0 0 0 100'/><path d='M200 0 L160 100'/>"), ".svg");
  assert(std::abs(arc.leadingX(.5)) > 13);
  const auto compactArc =
      load(svg("<style>.outline{fill:none;stroke:black;stroke-width:1}</style>"
               "<path class='outline' d='M0 0 A100 100 0 00 0 100'/><path d='M200 0 L160 100'/>"),
           ".svg");
  assert(std::abs(compactArc.leadingX(.5) - arc.leadingX(.5)) < .01);
  rejected(svg(
      "<style>path{transform:scale(2)}</style><path d='M0 0 L0 100'/><path d='M200 0 L160 100'/>"));
  rejected(svg("<path d='M0 0 L0 100'/><path d='M200 0 L160 100'/>", "viewBox='0 0 250 100'"));
  rejected(
      svg("<path d='M0 0 L0 100'/><path d='M200 0 L160 100'/>", "width='250px' height='100px'"));
  rejected(svg("<path d='M0 0 L0 100 Z'/><path d='M200 0 L160 100'/>"));
  rejected(svg("<path d='M0 0 L0 100 M10 0 L10 100'/><path d='M200 0 L160 100'/>"));
  rejected(svg("<line x1='0' y1='0' x2='0' y2='100'/><line x1='5' y1='0' x2='160' y2='100'/>"));
  rejected(svg("<path d='M0 0 L0 100'/><path d='M200 7 L160 107'/>"));
  rejected(svg("<path d='M0 0 L0 100'/><path d='M200 0 L160 110'/>"));
  rejected(svg("<path d='M0 0 L0 -100'/><path d='M200 0 L160 -100'/>"));
  rejected(svg("<path d='M0 0 L0 80 L10 50 L0 100'/><path d='M200 0 L160 100'/>"));
  rejected(svg("<path d='M0 0 L0 100'/><path d='M200 0 L-5 50 L160 100'/>"));
  rejected(svg("<path d='M0 0 L0 100'/><path d='M200 0 L160 100'/><line x2='2' y2='2'/>"));
  const auto dxf = [](std::string entities, int units = 4) {
    std::istringstream input{entities};
    std::ostringstream stretched;
    std::string code, value;
    while (std::getline(input, code) && std::getline(input, value)) {
      stretched << code << '\n';
      if (code == "20" || code == "21") stretched << std::stod(value) * 3;
      else stretched << value;
      stretched << '\n';
    }
    return "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n" + std::to_string(units) +
           "\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" + stretched.str() + "0\nENDSEC\n0\nEOF\n";
  };
  const std::string le = "0\nLINE\n10\n0\n20\n1\n30\n0\n11\n0\n21\n101\n31\n0\n";
  const std::string te = "0\nLINE\n10\n200\n20\n-1\n11\n160\n21\n99\n";
  const auto dxfCurve = load(dxf(le + te), ".dxf");
  assert(dxfCurve.chord(.5) == 180);
  rejected(dxf(le + te, 0), ".dxf");
  const std::string point = "0\nPOINT\n10\n9999\n20\n-9999\n30\n10\n";
  const auto withPoints = load(dxf(point + le + point + te + point), ".dxf");
  for (const double station : {0., .25, .5, .75, 1.}) {
    assert(withPoints.leadingX(station) == dxfCurve.leadingX(station));
    assert(withPoints.trailingX(station) == dxfCurve.trailingX(station));
  }
  rejected(dxf(point), ".dxf");
  rejected(dxf(le + point), ".dxf");
  rejected(dxf(le + te + point + le), ".dxf");
  rejected(dxf(le + te + "0\nCIRCLE\n10\n0\n20\n0\n40\n1\n"), ".dxf");
  rejected(dxf(le + te + "31\n0.000000001\n"), ".dxf");
  rejected(dxf("0\nLWPOLYLINE\n70\n0\n10\n0\n20\n0\n10\n0\n20\n100\n210\n1\n230\n0\n" + te),
           ".dxf");
  const auto negativeNormal =
      load(dxf(le + "0\nLWPOLYLINE\n70\n0\n10\n-200\n20\n0\n10\n-160\n20\n100\n230\n-1\n"), ".dxf");
  assert(negativeNormal.chord(0) == 200 && negativeNormal.chord(1) == 160);
  const auto spline = load(dxf("0\nSPLINE\n70\n8\n71\n2\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n"
                               "10\n0\n20\n0\n10\n-20\n20\n50\n10\n0\n20\n100\n" +
                               te),
                           ".dxf");
  assert(std::abs(spline.leadingX(.5) + 10) < .02);
  const auto poly = load(
      dxf("0\nLWPOLYLINE\n70\n0\n90\n2\n10\n0\n20\n0\n42\n0.1\n10\n0\n20\n100\n" + te), ".dxf");
  assert(std::abs(poly.leadingX(.5)) > 4.9);

  WingParameters p;
  p.halfSpan = 300;
  p.rootChord = 200;
  p.tipChord = 160;
  p.dihedralDegrees = 0;
  p.ribCount = 5;
  p.planform = curve;
  const auto foil = AirfoilProfile::nacaSymmetric(.12);
  auto ribs = generateRibs(p, foil, foil);
  assert(std::abs(ribs[2].chord - curve.chord(.5)) < 1e-8);
  auto between = interpolateRib(ribs[0], ribs[1], .5);
  assert(std::abs(between.leadingEdgeOffset - curve.leadingX(.125)) < 1e-8);
  assert(std::abs(calculateWingMetrics(p).planformArea - 2 * curve.area(300)) < 1e-8);
  StructureParameters s;
  s.ailerons = true;
  s.aileronStartRib = 2;
  s.aileronStopRib = 5;
  s.aileronWidth = 40;
  auto wing = applyWingStructure(ribs, s);
  assert(wing.ribs.size() == 5 && wing.surfaceWing && wing.surfaceWing->ribs.size() > 5);
  const auto& c = wing.controlSurfaces.front();
  for (std::size_t i = c.startRibIndex; i <= c.stopRibIndex; ++i) {
    auto min = std::min_element(c.profiles[i - c.startRibIndex].begin(),
                                c.profiles[i - c.startRibIndex].end(),
                                [](auto a, auto b) { return a.x < b.x; });
    const double x = ribs[i].leadingEdgeOffset + min->x;
    const double t = double(i - c.startRibIndex) / (c.stopRibIndex - c.startRibIndex);
    assert(std::abs(x - (c.hingeRootX + (c.hingeTipX - c.hingeRootX) * t)) < 1e-6);
  }
  s.aileronHingeParallelY = true;
  s.aileronWidth = 70;
  wing = applyWingStructure(ribs, s);
  assert(wing.controlSurfaces.front().hingeRootX == wing.controlSurfaces.front().hingeTipX);
  s.aileronWidth = 5;
  bool crossed = false;
  try {
    (void)applyWingStructure(ribs, s);
  } catch (const std::invalid_argument&) {
    crossed = true;
  }
  assert(crossed);
  s = {};
  s.leadingEdgeType = 3;
  s.leadingEdgeTubeOd = 3;
  s.leadingEdgeTubeId = 2;
  wing = applyWingStructure(ribs, s);
  assert(wing.surfaceWing && wing.members.front().centers.size() == 5);
  s = {};
  s.trailingEdgeType = 2;
  s.trailingEdgeWidth = 12;
  s.trailingEdgeHeight = 12;
  s.trailingEdgeSlotted = true;
  wing = applyWingStructure(ribs, s);
  assert(wing.sheetStockParts.front().outline.size() > 20);
  assert(wing.sheetStockParts.front().slots.empty());
  exportSheetStockSvg(wing.sheetStockParts.front(),
                      std::filesystem::path{directory.path().toStdWString()} / "stock.svg", "TE");
  // A deep indentation between two ribs must not hide a straight spar escaping the wing.
  p.ribCount = 2;
  p.planform.leading = {{0, 0}, {100, .4}, {0, 1}};
  p.planform.trailing = {{200, 0}, {200, 1}};
  s = {};
  SparParameters spar;
  spar.chordLocationPercent = 25;
  spar.tipChordLocationPercent = 25;
  spar.type = 1;
  spar.rodOd = 3;
  s.spars = {spar};
  bool escaped = false;
  try {
    (void)applyWingStructure(generateRibs(p, foil, foil), s);
  } catch (const std::invalid_argument&) {
    escaped = true;
  }
  assert(escaped);
  std::cout << "Curve import, normalization, rib, hinge, export and collision regressions passed\n";
}
