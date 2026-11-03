#include "domain/WingStructure.h"
#include "domain/DxfExporter.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <regex>
#include <QTemporaryDir>
#include <cstdlib>
using namespace designrc::domain;

bool hasStep(const PartDrawing& drawing, double x, double thickness, bool top) {
  for (const auto& path : drawing.paths) {
    if (path.layer != "RIB_OUTLINE" || path.spline) continue;
    for (std::size_t i = 1; i < path.points.size(); ++i) {
      const auto a = path.points[i-1], b = path.points[i];
      if (std::abs(a.x-x) < 1e-6 && std::abs(b.x-x) < 1e-6 &&
          std::abs(a.y-b.y) >= thickness-.01 && (top ? a.y+b.y > 0 : a.y+b.y < 0)) return true;
    }
  }
  return false;
}

int run() {
  WingParameters p;
  p.rootChord=p.tipChord=200; p.halfSpan=300; p.ribCount=7; p.dihedralDegrees=0;
  const auto foil=AirfoilProfile::nacaSymmetric(.15);
  const auto ribs=generateRibs(p,foil,foil);
  for (const bool leading : {true,false}) {
    StructureParameters s;
    if (leading) {s.leadingEdgeType=2; s.leadingEdgeWidth=5; s.leadingEdgeHeight=.01;}
    else {s.trailingEdgeType=2; s.trailingEdgeWidth=15; s.trailingEdgeHeight=.01;}
    double required=0;
    try {static_cast<void>(applyWingStructure(ribs,s));}
    catch(const EdgeHeightError& e) {required=e.cutHeightMm();}
    assert(required>0);
    auto& height=leading?s.leadingEdgeHeight:s.trailingEdgeHeight;
    height=required;
    assert(!applyWingStructure(ribs,s).ribs.empty());
    height=std::round(required*100)/100;
    assert(!applyWingStructure(ribs,s).ribs.empty());
    height=required-.02;
    bool failed=false;
    try {static_cast<void>(applyWingStructure(ribs,s));} catch(const EdgeHeightError&) {failed=true;}
    assert(failed);
  }
  StructureParameters controls;
  controls.ailerons=true; controls.flaps=true;
  controls.flapStartRib=2; controls.flapStopRib=4;
  controls.aileronStartRib=4; controls.aileronStopRib=7;
  controls.aileronWidth=controls.flapWidth=40;
  controls.aileronHingePostHeight=controls.flapHingePostHeight=.01;
  controls.builtUpAileron.enabled=controls.builtUpFlap.enabled=true;
  for(const bool aileron : {false,true}) {
    bool failed=false;
    try {static_cast<void>(applyWingStructure(ribs,controls));}
    catch(const EdgeHeightError& e) {
      assert(e.edgeName()==(aileron?"Aileron Hinge Post":"Flap Hinge Post"));
      (aileron?controls.aileronHingePostHeight:controls.flapHingePostHeight)=e.cutHeightMm();
      failed=true;
    }
    assert(failed);
  }
  assert(applyWingStructure(ribs,controls).controlSurfaces.size()==2);
  for(const int leading : {0,3,4}) for(const int mask : {1,2,3}) for(const bool taper : {false,true}) {
    StructureParameters s;
    s.leadingEdgeType=leading; s.leadingEdgeTubeOd=2; s.leadingEdgeTubeId=1; s.leadingEdgeRodOd=2;
    s.topTeSheeting=(mask&1)!=0; s.bottomTeSheeting=(mask&2)!=0;
    s.topTeSheetingWidth=30; s.bottomTeSheetingWidth=28;
    s.topTeSheetingThickness=s.bottomTeSheetingThickness=1.5875;
    s.topTeSheetingTaper=s.bottomTeSheetingTaper=taper;
    const auto wing=applyWingStructure(ribs,s);
    for(const auto& rib : wing.ribs) {
      const auto drawing=makeStructuredRibPartDrawing(rib,rib.name);
      if(mask&1) assert(hasStep(drawing,170,1.5875,true));
      if(mask&2) assert(hasStep(drawing,172,1.5875,false));
    }
  }
  // Wing TE sheeting must remain present inside built-up controls even when
  // the optional control skins are off, including in manufacturing contours.
  controls.topTeSheeting=true;
  controls.topTeSheetingWidth=25.4; controls.topTeSheetingThickness=1.5875;
  auto wing=applyWingStructure(ribs,controls);
  for(const auto& c : wing.controlSurfaces) {
    assert(!c.builtUpWing->sheeting.empty());
    for(std::size_t i=0;i<c.builtUpWing->ribs.size();++i) {
      const auto& rib=c.builtUpWing->ribs[i];
      assert(rib.name==std::string{c.name=="Aileron"?"AR":"FR"}+std::to_string(c.startRibIndex+i+1));
      const auto drawing=makeStructuredRibPartDrawing(rib,rib.name);
      assert(drawing.label==rib.name);
      assert(hasStep(drawing,174.6,1.5875,true));
    }
  }
  {
    const auto& rib=wing.controlSurfaces.back().builtUpWing->ribs.front();
    const auto drawing=makeStructuredRibPartDrawing(rib,rib.name);
    assert(drawing.label=="AR4");
    QTemporaryDir directory;
    assert(directory.isValid());
    const std::filesystem::path folder{directory.path().toStdWString()};
    exportStructuredRibDxf(rib,folder/"AR4.dxf",rib.name);
    exportStructuredRibSvg(rib,folder/"AR4.svg",rib.name);
    std::ifstream dxf{folder/"AR4.dxf"};
    bool notch=false;
    std::string code,value,type,layer;
    double x1=0,x2=0,y1=0,y2=0;
    const auto checkLine=[&] {
      notch=notch||(type=="LINE"&&layer=="RIB_OUTLINE"&&std::abs(x1-174.6)<1e-5&&
          std::abs(x2-174.6)<1e-5&&std::abs(y2-y1)>1.58);
    };
    while(std::getline(dxf,code)&&std::getline(dxf,value)) {
      const int group=std::stoi(code);
      if(group==0) {checkLine();type=value;layer.clear();}
      else if(group==8) layer=value;
      else if(type=="LINE") {
        if(group==10)x1=std::stod(value);
        if(group==11)x2=std::stod(value);
        if(group==20)y1=std::stod(value);
        if(group==21)y2=std::stod(value);
      }
    }
    checkLine(); assert(notch);
    std::ifstream svg{folder/"AR4.svg"};
    const std::string text{std::istreambuf_iterator<char>{svg},{}};
    const std::regex polyline{R"tag(<polyline points="([^"]+)")tag"};
    notch=false;
    for(auto it=std::sregex_iterator(text.begin(),text.end(),polyline);it!=std::sregex_iterator{};++it) {
      std::istringstream points{(*it)[1].str()};
      std::string pair; std::vector<Point2> path;
      while(points>>pair) {const auto comma=pair.find(',');path.push_back({std::stod(pair.substr(0,comma)),std::stod(pair.substr(comma+1))});}
      for(std::size_t i=1;i<path.size();++i)
        notch=notch||(std::abs(path[i-1].x-path[i].x)<1e-5&&
            std::abs(std::abs(path[i].y-path[i-1].y)-1.5875)<1e-5);
    }
    assert(notch);
  }
  // Additional skin butts against the wing TE sheet; the two depths are not
  // stacked and the wing's selected thickness retains its own metadata.
  controls.builtUpAileron.topSheeting=true;
  controls.builtUpAileron.topSheetingWidth=40;
  controls.builtUpAileron.topSheetingThickness=.5;
  wing=applyWingStructure(ribs,controls);
  const auto& control=*wing.controlSurfaces.back().builtUpWing;
  assert(control.sheeting.size()==2);
  bool wingSheet=false, skin=false;
  for(const auto& sheet : control.sheeting) {
    wingSheet=wingSheet||(sheet.inheritedWingSheeting&&sheet.nominalThickness==1.5875);
    skin=skin||(sheet.supplementalControlSheeting&&sheet.nominalThickness==.5);
  }
  assert(wingSheet&&skin);
  // Saved FR3 case: adding an opposite flap skin must not force a taper
  // onto the inherited, explicitly untapered wing TE sheet (or vice versa).
  for(const bool top : {true,false}) {
    WingParameters exact;
    exact.halfSpan=400; exact.rootChord=exact.tipChord=200;
    exact.sweep=15; exact.tipTwistDegrees=2; exact.dihedralDegrees=0; exact.ribCount=9;
    const auto sections=generateRibs(exact,AirfoilProfile::nacaSymmetric(.15),AirfoilProfile::nacaSymmetric(.10));
    StructureParameters s;
    s.flaps=true; s.flapStartRib=2; s.flapStopRib=5; s.flapWidth=50; s.flapHingePostHeight=30;
    s.builtUpFlap.enabled=true;
    const auto bare=applyWingStructure(sections,s);
    s.topTeSheeting=top; s.bottomTeSheeting=!top;
    s.topTeSheetingWidth=s.bottomTeSheetingWidth=25.4;
    s.topTeSheetingThickness=s.bottomTeSheetingThickness=1.5875;
    s.topTeSheetingTaper=s.bottomTeSheetingTaper=false;
    s.builtUpFlap.topSheeting=s.builtUpFlap.bottomSheeting=true;
    s.builtUpFlap.topSheetingWidth=s.builtUpFlap.bottomSheetingWidth=50;
    s.builtUpFlap.topSheetingThickness=s.builtUpFlap.bottomSheetingThickness=1.5875;
    const auto sheeted=applyWingStructure(sections,s);
    const auto drawingAt=[&](const StructuredWing& model) {
      const auto& rib=model.controlSurfaces.front().builtUpWing->ribs.at(1);
      assert(rib.name=="FR3");
      return makeStructuredRibPartDrawing(rib,rib.name);
    };
    const auto surfaceAt=[&](const PartDrawing& part,double x) {
      double y=top?-1e9:1e9;
      for(const auto& path:part.paths) if(path.layer=="RIB_OUTLINE")
        for(std::size_t i=0;i<path.points.size();++i) {
          const auto a=path.points[i],b=path.points[(i+1)%path.points.size()];
          if(std::abs(b.x-a.x)<1e-9 || x<std::min(a.x,b.x) || x>std::max(a.x,b.x)) continue;
          const double v=a.y+(b.y-a.y)*(x-a.x)/(b.x-a.x);
          y=top?std::max(y,v):std::min(y,v);
        }
      assert(std::abs(y)<1e8);
      return y;
    };
    const auto original=drawingAt(bare),cut=drawingAt(sheeted);
    for(const double x:{180.0,188.0,190.0})
      assert(std::abs(std::abs(surfaceAt(original,x)-surfaceAt(cut,x))-1.5875)<.002);
  }
  StructureParameters stockSettings=controls;
  stockSettings.topTeSheeting=stockSettings.bottomTeSheeting=false;
  stockSettings.trailingEdgeType=2; stockSettings.trailingEdgeWidth=10; stockSettings.trailingEdgeHeight=30;
  auto stockPanel1=applyWingStructure(ribs,stockSettings);
  auto stockPanel2=applyWingStructure(ribs,stockSettings);
  std::size_t aileronTe=1,flapTe=1;
  nameControlTrailingEdgeStock(stockPanel1,aileronTe,flapTe);
  nameControlTrailingEdgeStock(stockPanel2,aileronTe,flapTe);
  for(const auto* panel:{&stockPanel1,&stockPanel2}) {
    const auto number=panel==&stockPanel1?"1":"2";
    for(const auto& c:panel->controlSurfaces) {
      const auto name=std::string{c.name=="Aileron"?"ATE":"FTE"}+number;
      const auto& stock=c.builtUpWing->sheetStockParts.front();
      assert(stock.name==name);
      assert(makeSheetStockPartDrawing(stock,stock.name).label==name);
      assert(c.builtUpWing->profiledMembers.front().name==name);
    }
  }
  // Saved Built-upAilerons/FR3: the bevel must not double back past the
  // top notch, and a control skin must reach its hinge spar even when its
  // old stored Width is narrower than the control or the wing TE sheet.
  for (const bool inherited : {false, true}) for (const bool bottom : {false, true}) {
    WingParameters panel;
    panel.rootChord = panel.tipChord = 200; panel.halfSpan = 400;
    panel.ribCount = 9; panel.sweep = 15; panel.tipTwistDegrees = 2; panel.dihedralDegrees = 0;
    const auto sections = generateRibs(panel, AirfoilProfile::nacaSymmetric(.15), AirfoilProfile::nacaSymmetric(.10));
    StructureParameters settings;
    settings.ailerons = settings.flaps = true;
    settings.flapStartRib = 2; settings.flapStopRib = 5;
    settings.aileronStartRib = 5; settings.aileronStopRib = 9;
    settings.flapWidth = settings.aileronWidth = 50;
    settings.topTeSheeting = settings.bottomTeSheeting = inherited;
    settings.topTeSheetingWidth = settings.bottomTeSheetingWidth = 25.4;
    settings.topTeSheetingThickness = settings.bottomTeSheetingThickness = .5;
    for (auto* options : {&settings.builtUpAileron, &settings.builtUpFlap}) {
      options->enabled = options->hingeSpars = true;
      options->hingeSparWidth = 4; options->hingeSparHeight = 3.5; options->hingeAngleDegrees = 45;
      options->topSheeting = true; options->bottomSheeting = bottom;
      options->topSheetingWidth = options->bottomSheetingWidth = 20; // narrower than wing TE sheet
      options->topSheetingThickness = options->bottomSheetingThickness = .5;
    }
    const auto model = applyWingStructure(sections, settings);
    for (const auto& control : model.controlSurfaces) {
      const auto& built = *control.builtUpWing;
      for (const auto& rib : built.ribs) {
        const auto& outline = rib.outerOutline;
        for (std::size_t i = 0; i < outline.size(); ++i) {
          const auto a = outline[(i + outline.size() - 1) % outline.size()];
          const auto b = outline[i], c = outline[(i + 1) % outline.size()];
          const double cross = (b.x-a.x)*(c.y-b.y) - (b.y-a.y)*(c.x-b.x);
          const double dot = (b.x-a.x)*(c.x-b.x) + (b.y-a.y)*(c.y-b.y);
          assert(std::abs(cross) > 1e-8 || dot >= -1e-8);
        }
      }
      for (const bool top : {true, false}) {
        if (!top && !bottom) continue;
        const auto spar = std::find_if(built.profiledMembers.begin(), built.profiledMembers.end(), [&](const auto& member) {
          return member.name == (top ? "Control Top hinge spar" : "Control Bottom hinge spar");
        });
        assert(spar != built.profiledMembers.end());
        bool found = false;
        for (const auto& sheet : built.sheeting) {
          if (sheet.inheritedWingSheeting || (sheet.name.find("top") != std::string::npos ||
              sheet.name.find("Top") != std::string::npos) != top) continue;
          found = true;
          for (std::size_t i = 0; i < sheet.profiles.size(); ++i) {
            const double rear = std::max_element(spar->profiles[i].begin(), spar->profiles[i].end(),
                [](Point2 a, Point2 b) { return a.x < b.x; })->x;
            const double start = std::min_element(sheet.profiles[i].begin(), sheet.profiles[i].end(),
                [](Point2 a, Point2 b) { return a.x < b.x; })->x;
            assert(std::abs(start - rear) < 1e-7);
          }
        }
        assert(found);
      }
    }
    if (const auto* output = std::getenv("DESIGNRC_HINGE_RIB_TEST_OUTPUT")) {
      const auto& rib = model.controlSurfaces.front().builtUpWing->ribs.at(1);
      exportStructuredRibSvg(rib, output, "FR3");
    }
  }
  return 0;
}
int main() {
#ifdef _WIN32
  _set_error_mode(_OUT_TO_STDERR);
#endif
  try {return run();}
  catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
