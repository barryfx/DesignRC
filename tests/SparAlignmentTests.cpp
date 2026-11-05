#include "gui/SparAlignment.h"
#include "TestWingStructure.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QGroupBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QTabWidget>
#include <filesystem>
#include <cassert>
#include <cmath>

namespace {
using namespace designrc;
using namespace designrc::gui;
WingPanelData fixture() {
  WingPanelData d;
  d.rootChord = 254; d.tipChord = 152.4; d.sweep = 25.4; d.panelSpan = 698.5;
  d.ribCount = 11; d.twist = d.dihedral = 0;
  d.ailerons = d.flaps = false;
  d.spars.resize(2);
  for (std::size_t i = 0; i < d.spars.size(); ++i) {
    auto& s = d.spars[i];
    s.material = 0; s.verticalLocation = static_cast<int>(i);
    s.woodWidth = 6.35; s.woodHeight = 3.175;
    s.chordLocationPercent = s.tipChordLocationPercent = 30;
  }
  d.joinerPanelMode = 1;
  d.fixedJoiners = {FixedJoinerData{}};
  d.fixedJoiners[0].chordLocationPercent = 30;
  d.fixedJoiners[0].ribBays = 7;
  return d;
}
QString part(const WingPanelData& d, const QString& path) { return d.referenceId + "/" + path; }
QString reference(const WingPanelData& d, const QString& label) {
  for (const auto& choice : sparAlignmentReferences({d}, 0))
    if (choice.label == label) return choice.key;
  assert(false && "Expected alignment reference");
  return {};
}
bool rejected(std::vector<WingPanelData> panels) {
  try { resolveSparParallelLocations(panels); }
  catch (const std::invalid_argument&) { return true; }
  return false;
}
void testLinkedRearSparCollision() {
  const auto fixturePath = std::filesystem::path{__FILE__}.parent_path() /
      "fixtures" / "Ailerons_Spars_Parallel_Collision.designrc";
  QFile file{QString::fromStdWString(fixturePath.wstring())};
  assert(file.open(QIODevice::ReadOnly));
  const auto project = QJsonDocument::fromJson(file.readAll()).object();
  const auto original = panelDataFromJson(project["panels"].toArray()[0].toObject());
  assert(original.spars.size() == 3 && original.spars.back().chordLocationPercent == 90);
  const auto unit = static_cast<DisplayUnit>(project["globalUnit"].toInt());
  WingPanelEditor editor{original, unit};
  QString error;
  assert(!editor.validate(error));
  assert(error.contains("intersects rear TE") && error.contains("Manual"));
  assert(editor.correctRearSparLocations());
  const auto moved = editor.data();
  assert(moved.spars.back().parallelTo.isEmpty());
  assert(moved.spars.back().chordLocationPercent < 90);
  assert(moved.spars.back().tipChordLocationPercent > 0);
  assert(editor.validate(error));
  for (std::size_t i = 0; i < 2; ++i) {
    assert(moved.spars[i].parallelTo == original.spars[i].parallelTo);
    assert(moved.spars[i].chordLocationPercent == original.spars[i].chordLocationPercent);
  }
  assert(editor.rollbackRearSparLocations());
  const auto restored = editor.data();
  assert(restored.spars.back().parallelTo == original.spars.back().parallelTo);
  assert(restored.spars.back().chordLocationPercent == 90);
  assert(restored.spars.back().manualTipChordLocationPercent == 25);
  assert(editor.correctRearSparLocations());
  editor.acceptRearSparLocations();
  assert(!editor.rollbackRearSparLocations());
  assert(editor.validate(error));
  // A later width change must keep advancing the already corrected spar.
  auto* width = editor.findChild<LengthInput*>("aileronWidth");
  width->findChild<QDoubleSpinBox*>()->setValue((original.aileronWidth + 5) / 25.4);
  assert(editor.data().spars.back().chordLocationPercent < moved.spars.back().chordLocationPercent);
}
void testLinkedRearSparWidthChanges() {
  for (const auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    for (int obstruction = 0; obstruction < 3; ++obstruction) {
      auto d = fixture();
      d.rootChord = d.tipChord = 200; d.sweep = 0;
      d.trailingEdgeType = obstruction == 0 ? 2 : 0;
      d.trailingEdgeWidth = 20;
      d.ailerons = obstruction == 1; d.flaps = obstruction == 2;
      d.aileronWidth = d.flapWidth = 20;
      d.aileronStartRib = d.flapStartRib = 2;
      d.aileronStopRib = 11; d.flapStopRib = 10;
      auto rear = d.spars.front();
      rear.referenceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
      rear.chordLocationPercent = rear.tipChordLocationPercent = 82;
      rear.parallelTo = part(d, "fixed/" + d.fixedJoiners[0].referenceId);
      d.spars.push_back(rear);
      WingPanelEditor editor{d, unit};
      QString error;
      assert(editor.validate(error));
      const auto* name = obstruction == 0 ? "trailingEdgeWidth" : obstruction == 1 ? "aileronWidth" : "flapWidth";
      auto* width = editor.findChild<LengthInput*>(name)->findChild<QDoubleSpinBox*>();
      width->setValue(unit == DisplayUnit::Inches ? 40 / 25.4 : 40);
      const auto moved = editor.data();
      assert(moved.spars.back().parallelTo.isEmpty());
      assert(moved.spars.back().chordLocationPercent < 80);
      assert(editor.validate(error));
      assert(editor.rollbackRearSparLocations());
      assert(editor.data().spars.back().parallelTo == rear.parallelTo);
      assert(editor.data().spars.back().chordLocationPercent == 82);

      // A front spar blocking the proposed relocation must leave everything intact.
      d.trailingEdgeWidth = d.aileronWidth = d.flapWidth = 40;
      d.spars[0].chordLocationPercent = d.spars[0].tipChordLocationPercent = obstruction == 0 ? 78 : 74;
      d.spars[0].woodWidth = 4;
      WingPanelEditor blocked{d, unit};
      assert(!blocked.correctRearSparLocations());
      assert(blocked.data().spars.back().chordLocationPercent == 82);
      assert(blocked.data().spars.back().parallelTo == rear.parallelTo);
    }
  }
}
void testResolution() {
  auto d = fixture();
  d.spars[0].parallelTo = part(d, "fixed/" + d.fixedJoiners[0].referenceId);
  d.spars[1].parallelTo = sparAlignmentKey(d, d.spars[0].referenceId);
  std::vector<WingPanelData> panels{d};
  resolveSparParallelLocations(panels);
  for (const auto& s : panels[0].spars) {
    assert(std::abs(s.tipChordLocationPercent - 100.0 / 3) < 1e-9);
    assert(s.manualTipChordLocationPercent == 30);
  }
  const auto reopened = panelDataFromJson(panelDataToJson(panels[0]));
  assert(reopened.referenceId == d.referenceId);
  assert(reopened.spars[0].parallelTo == d.spars[0].parallelTo);
  assert(reopened.fixedJoiners[0].referenceId == d.fixedJoiners[0].referenceId);
  assert(reopened.spars[0].manualTipChordLocationPercent == 30);
  domain::WingParameters w;
  w.rootChord=d.rootChord; w.tipChord=d.tipChord; w.sweep=d.sweep; w.halfSpan=d.panelSpan; w.ribCount=d.ribCount;
  const auto ribs=domain::generateRibs(w,d.rootAirfoil,d.tipAirfoil);
  domain::StructureParameters structure;
  for (const auto& s : panels[0].spars)
    structure.spars.push_back({s.chordLocationPercent,s.verticalLocation,s.material,s.type,
        s.woodHeight,s.woodWidth,s.tubeOd,s.tubeId,s.rodOd,s.stripWidth,s.stripThickness,s.tipChordLocationPercent});
  const auto wing=testsupport::buildStructure(ribs,structure);
  for (const auto& member : wing.members)
    if (member.name.starts_with("Spar "))
      for (std::size_t i=0;i<member.centers.size();++i)
        assert(std::abs(ribs[i].leadingEdgeOffset + member.centers[i].x - 76.2) < 1e-8);
  auto cycle=d;
  cycle.spars[0].parallelTo=sparAlignmentKey(cycle,cycle.spars[1].referenceId);
  assert(rejected({cycle}));
  auto missing=d;
  missing.fixedJoiners.clear();
  assert(rejected({missing}));
  auto outside=fixture();
  // A forward root next to the LE cannot follow a strongly aft-swept reference.
  outside.spars[0].chordLocationPercent=0;
  outside.sweep=100;
  outside.spars[0].parallelTo=part(outside,"fixed/"+outside.fixedJoiners[0].referenceId);
  assert(rejected({outside}));
  auto other=fixture();
  panels={d,other};
  for (const auto& choice:sparAlignmentReferences(panels,0)) assert(choice.key.startsWith(d.referenceId+"/"));
  panels[0].spars[0].parallelTo=part(other,"edge/le");
  assert(rejected(panels));
  const auto oldId=d.referenceId;
  reidentifySparReferencePanel(d);
  assert(d.referenceId!=oldId && d.spars[0].parallelTo.startsWith(d.referenceId+"/"));
  panels={d}; resolveSparParallelLocations(panels);
}
void testEdgesAndControls() {
  auto d=fixture();
  d.spars.resize(1);
  d.spars[0].chordLocationPercent=50;
  d.planform.leading={{0,0},{-15,.5},{25.4,1}};
  d.planform.trailing={{254,0},{240,.5},{177.8,1}};
  d.planform.leadingSourceSpan=d.planform.trailingSourceSpan=d.panelSpan;
  d.spars[0].parallelTo=part(d,"edge/te");
  std::vector<WingPanelData> panels{d}; resolveSparParallelLocations(panels);
  const double end=25.4+panels[0].spars[0].tipChordLocationPercent/100*152.4;
  assert(std::abs((end-127)-(177.8-254))<1e-8);
  d.ailerons=true; d.aileronStartRib=2; d.aileronStopRib=10; d.aileronPerpendicularToRibs=true;
  d.aileronWidth=100;
  d.spars[0].chordLocationPercent=30;
  d.spars[0].parallelTo=reference(d,"Aileron hinge");
  panels={d}; resolveSparParallelLocations(panels);
  assert(std::abs(panels[0].spars[0].tipChordLocationPercent-100.0/3)<1e-8);
  d.flaps=true; d.flapStartRib=2; d.flapStopRib=5; d.flapPerpendicularToRibs=false;
  assert(!reference(d,"Flap hinge").isEmpty());
  d.turbulators=true; d.leTopSheet=true; d.teBottomSheet=true; d.topTeSheeting=true;
  assert(!reference(d,"Turbulator 1").isEmpty());
  assert(!reference(d,"Front Top Sheeting (aft edge)").isEmpty());
  assert(!reference(d,"Rear Bottom Sheeting (forward edge)").isEmpty());
  assert(!reference(d,"Top TE Sheeting").isEmpty());
  d.flaps=false; d.topTeSheeting=false;
  d.builtUpAileron.enabled=true;
  d.aileronHingePostHeight=40;
  domain::SparParameters controlSpar;
  controlSpar.referenceId="control-spar-test";
  controlSpar.material=0; controlSpar.verticalLocation=0;
  controlSpar.woodWidth=3.175; controlSpar.woodHeight=1.5875;
  d.builtUpAileron.spars={controlSpar};
  d.spars[0].parallelTo=reference(d,"Aileron Spar 1");
  panels={d}; resolveSparParallelLocations(panels);
  assert(std::isfinite(panels[0].spars[0].tipChordLocationPercent));
  assert(panels[0].spars[0].tipChordLocationPercent<100.0/3);
}
void testTwistedSparDirection() {
  const auto d=fixture();
  domain::WingParameters w;
  w.rootChord=d.rootChord; w.tipChord=d.tipChord; w.sweep=d.sweep;
  w.halfSpan=d.panelSpan; w.ribCount=d.ribCount;
  w.tipTwistDegrees=-3; w.dihedralDegrees=7;
  const auto ribs=domain::generateRibs(w,d.rootAirfoil,d.tipAirfoil);
  for(const int vertical:{0,1,2}) {
    domain::SparParameters s;
    s.chordLocationPercent=30; s.verticalLocation=vertical; s.material=0;
    s.woodWidth=6.35; s.woodHeight=3.175;
    s.tipChordLocationPercent=domain::sparTipParallelTo(ribs,s,{.05,1});
    domain::StructureParameters settings; settings.spars={s};
    const auto wing=testsupport::buildStructure(ribs,settings);
    const auto& member=wing.members.front();
    const auto point=[&](std::size_t i) {
      const auto c=member.centers[i]; const auto& r=ribs[i];
      const auto translation=domain::ribTwistTranslation(r);
      const double t=r.twistDegrees*std::acos(-1.0)/180;
      const double a=r.ribPlaneAngleDegrees*std::acos(-1.0)/180;
      const double z=std::sin(t)*c.x+std::cos(t)*c.y+translation.y;
      return domain::Point2{r.leadingEdgeOffset+std::cos(t)*c.x-std::sin(t)*c.y+translation.x,
          r.spanPosition-std::sin(a)*z};
    };
    const auto root=point(0),tip=point(ribs.size()-1);
    assert(std::abs((tip.x-root.x)-.05*(tip.y-root.y))<1e-7);
  }
}
void testInheritedRootProfile() {
  auto inner=fixture(),outer=fixture();
  inner.tipAirfoil=domain::AirfoilProfile::nacaSymmetric(.06);
  inner.twist=-5;
  outer.rootChord=inner.tipChord; outer.tipChord=100; outer.sweep=20; outer.twist=-3;
  outer.spars.resize(1);
  outer.spars[0].parallelTo=part(outer,"fixed/"+outer.fixedJoiners[0].referenceId);
  std::vector<WingPanelData> panels{inner,outer}; resolveSparParallelLocations(panels);
  domain::WingParameters w;
  w.rootChord=outer.rootChord; w.tipChord=outer.tipChord; w.sweep=outer.sweep;
  w.halfSpan=outer.panelSpan; w.ribCount=outer.ribCount;
  w.rootTwistDegrees=-5; w.tipTwistDegrees=-8;
  const auto ribs=domain::generateRibs(w,inner.tipAirfoil,outer.tipAirfoil);
  domain::SparParameters spar;
  spar.chordLocationPercent=30; spar.verticalLocation=0; spar.material=0;
  spar.woodWidth=6.35; spar.woodHeight=3.175;
  const double expected=domain::sparTipParallelTo(ribs,spar,{0,1});
  assert(std::abs(panels[1].spars[0].tipChordLocationPercent-expected)<1e-8);
}
void testEditor() {
  auto d=fixture();
  WingPanelEditor editor{d,DisplayUnit::Millimeters};
  auto boxes=editor.findChildren<QComboBox*>("sparParallelTo");
  assert(boxes.size()==2);
  auto* combo=boxes[0];
  const QString joiner=part(d,"fixed/"+d.fixedJoiners[0].referenceId);
  assert(combo->findData(joiner)>=0);
  assert(combo->findData(sparAlignmentKey(d,d.spars[0].referenceId))<0);
  auto tips=editor.findChildren<QDoubleSpinBox*>("sparTipChordLocation");
  combo->setCurrentIndex(combo->findData(joiner));
  assert(!tips[0]->isEnabled() && std::abs(tips[0]->value()-33.33)<1e-8);
  auto roots=editor.findChildren<QDoubleSpinBox*>("sparRootChordLocation");
  roots[0]->setValue(25);
  assert(std::abs(tips[0]->value()-25)<1e-8);
  combo->setCurrentIndex(0);
  assert(tips[0]->isEnabled() && tips[0]->value()==30);
  // Refresh uses current widget values; deleted references never retarget to
  // the row that happens to take the old number.
  combo->setCurrentIndex(combo->findData(sparAlignmentKey(d,d.spars[1].referenceId)));
  assert(!tips[0]->isEnabled());
  auto deletes=editor.findChildren<QCheckBox*>("sparDeleteSelection");
  deletes[1]->setChecked(true);
  editor.findChild<QPushButton*>("deleteSparButton")->click();
  QString error;
  assert(!editor.validate(error) && error.contains("no longer available"));
  boxes=editor.findChildren<QComboBox*>("sparParallelTo"); boxes[0]->setCurrentIndex(0);
  assert(editor.data().spars[0].parallelTo.isEmpty());
  // Reopening restores the saved reference and the exact manual fallback.
  d=fixture(); d.spars[0].parallelTo=part(d,"fixed/"+d.fixedJoiners[0].referenceId);
  std::vector<WingPanelData> resolved{d}; resolveSparParallelLocations(resolved);
  editor.setData(panelDataFromJson(panelDataToJson(resolved[0])));
  boxes=editor.findChildren<QComboBox*>("sparParallelTo");
  assert(boxes[0]->currentData().toString()==d.spars[0].parallelTo);
  boxes[0]->setCurrentIndex(0);
  assert(editor.data().spars[0].tipChordLocationPercent==30);
  auto other=fixture();
  editor.setSparReferenceContext(0,[&] {return std::vector<WingPanelData>{editor.data(),other};});
  editor.findChild<QPushButton*>("addSparButton")->click();
  editor.refreshSparReferences(true);
  const auto current=editor.data();
  boxes=editor.findChildren<QComboBox*>("sparParallelTo");
  assert(current.spars.size()==3);
  assert(boxes[0]->findData(sparAlignmentKey(current,current.spars.back().referenceId))>=0);
  for(int i=1;i<boxes[0]->count();++i)
    assert(boxes[0]->itemData(i).toString().startsWith(current.referenceId+"/"));
}
}
void testSparParallelReferences() { testResolution(); testEdgesAndControls(); testTwistedSparDirection(); testInheritedRootProfile(); testEditor(); testLinkedRearSparCollision(); testLinkedRearSparWidthChanges(); }

void testSparJoinerWarning() {
  auto d = fixture();
  int dialogs = 0;
  QString text;
  QString answer = "Cancel";
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (!box || box->objectName() != "sparJoinerAlignmentWarning") return;
    ++dialogs;
    text = box->text();
    assert(box->buttons().size() == 2);
    for (auto* button : box->buttons())
      if (button->text() == answer) { button->click(); return; }
    assert(false && "Missing warning dialog button");
  });
  timer.start(1);
  assert(!confirmSparJoinerAlignment(nullptr, {d}));
  assert(dialogs == 1);
  assert(text == "Warning:  One or more spar 1, 2 has the same root chord location as joiner 1, but is not parallel to the joiner.  Select Parallel to Joiner 1 to make it align.");
  answer = "Continue";
  assert(confirmSparJoinerAlignment(nullptr, {d}));
  assert(dialogs == 2); // Each new Generate press asks again.
  // Actual parallel geometry needs no link and no warning.
  for (auto& s : d.spars) s.tipChordLocationPercent = 100.0 / 3;
  assert(confirmSparJoinerAlignment(nullptr, {d}));
  assert(dialogs == 2);
  for (auto& s : d.spars) {
    s.tipChordLocationPercent = 30;
    s.parallelTo = part(d, "fixed/" + d.fixedJoiners[0].referenceId);
  }
  assert(confirmSparJoinerAlignment(nullptr, {d}));
  assert(dialogs == 2);
  // Different root locations and inactive joiner modes do not warn.
  for (auto& s : d.spars) { s.parallelTo.clear(); s.chordLocationPercent = 20; }
  assert(confirmSparJoinerAlignment(nullptr, {d}));
  assert(dialogs == 2);
  d = fixture(); d.joinerPanelMode = 0;
  d.removableJoiners = {RemovableJoinerData{}};
  d.removableJoiners[0].chordLocationPercent = 30;
  answer = "Cancel";
  assert(!confirmSparJoinerAlignment(nullptr, {d}));
  assert(dialogs == 3);
  // Multiple panels and joiners still use one dialog for the button press.
  auto other = fixture();
  auto extra = other.fixedJoiners.front();
  extra.referenceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  other.fixedJoiners.push_back(extra);
  assert(!confirmSparJoinerAlignment(nullptr, {d, other}));
  assert(dialogs == 4 && text.contains("Panel 1") && text.contains("Panel 2") && text.contains("joiner 2"));
  timer.stop();

  for (auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    auto defaults = installedDefaultPanelData(unit);
    assert(defaults.spars.size() == 2 && defaults.fixedJoiners.size() == 1);
    const auto id = part(defaults, "fixed/" + defaults.fixedJoiners[0].referenceId);
    assert(defaults.spars[0].referenceId != defaults.spars[1].referenceId);
    for (const auto& s : defaults.spars) assert(s.material == 0 && s.parallelTo == id);
    WingPanelEditor editor{panelDataFromJson(panelDataToJson(defaults)), unit};
    const auto boxes = editor.findChildren<QComboBox*>("sparParallelTo");
    assert(boxes.size() == 2);
    for (auto* box : boxes) assert(box->currentData().toString() == id);
    std::vector<WingPanelData> panels{editor.data()};
    resolveSparParallelLocations(panels, unit);
    assert(confirmSparJoinerAlignment(nullptr, panels, unit));
    assert(dialogs == 4);
  }
}

void testSpoilerPlacementControls() {
  auto d = fixture();
  auto rear = d.spars.front();
  rear.referenceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  rear.chordLocationPercent = 75;
  rear.parallelTo = part(d, "edge/te");
  d.spars.push_back(rear);
  d.spoilers = true; d.spoilerChordLocationPercent = 30;
  d.spoilerImmediatelyBehindSpar = false;
  WingPanelEditor editor{d};
  auto* automatic = editor.findChild<QCheckBox*>("spoilerImmediatelyBehindSpar");
  auto* location = editor.findChild<QDoubleSpinBox*>("spoilerChordLocationPercent");
  auto* selection = editor.findChild<QComboBox*>("spoilerSpar");
  assert(automatic && location && !automatic->isChecked() && location->isEnabled());
  assert(selection && selection->count() == 2);
  assert(selection->itemText(0) == "Spar 1" && selection->itemText(1) == "Spar 3");
  assert(automatic->parentWidget() == selection->parentWidget());
  assert(!selection->isEnabled());
  assert(location->value() == 30);
  automatic->setChecked(true);
  assert(editor.data().spoilerImmediatelyBehindSpar && !location->isEnabled());
  assert(selection->isEnabled());
  selection->setCurrentIndex(0);
  const auto chosen = d.spars.front().referenceId;
  assert(editor.data().spoilerSparReferenceId == chosen);
  auto* tabs = editor.findChild<QTabWidget*>();
  for (int i = 0; i < tabs->count(); ++i)
    if (tabs->tabText(i) == "Spoilers") tabs->setCurrentIndex(i);
  editor.resize(800, 900); editor.show(); QApplication::processEvents();
  assert(selection->isVisible());
  assert(automatic->mapTo(&editor, QPoint{}).y() > location->mapTo(&editor, QPoint{}).y());
  assert(selection->mapTo(&editor, QPoint{}).x() > automatic->mapTo(&editor, QPoint{}).x());
  auto saved = panelDataFromJson(panelDataToJson(editor.data()));
  assert(saved.spoilerSparReferenceId == chosen && saved.spoilerImmediatelyBehindSpar);
  std::swap(saved.spars[0], saved.spars[2]);
  editor.setData(saved);
  assert(selection->currentData().toString() == chosen && selection->currentText() == "Spar 3");
  automatic->setChecked(false);
  assert(location->isEnabled() && location->value() == 30);
  const auto reopened = panelDataFromJson(panelDataToJson(editor.data()));
  assert(!reopened.spoilerImmediatelyBehindSpar && reopened.spoilerChordLocationPercent == 30);
  assert(reopened.spars.front().parallelTo == rear.parallelTo);
  saved.spars = {d.spars.front()};
  editor.setData(saved);
  assert(selection->count() == 1 && !selection->isVisible() && !location->isEnabled());
  saved.spars.front().verticalLocation = 1;
  editor.setData(saved);
  assert(selection->count() == 0 && !selection->isVisible());
  assert(!automatic->isChecked() && location->isEnabled());
}
