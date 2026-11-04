#include "TestWingStructure.h"
#include "gui/TechnicalDrawing.h"
#include "domain/DxfExporter.h"
#include "gui/PlanViewport.h"
#include "gui/PartPdfExporter.h"
#include "gui/WingPanelEditor.h"

#include <QApplication>
#include <QFrame>
#include <QGroupBox>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QTimer>
#include <QMessageBox>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontMetrics>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <numbers>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
void testHingeSparHeightDefaults() {
  using namespace designrc::gui;
  for (const auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    const auto installed = installedDefaultPanelData(unit);
    assert(installed.builtUpAileron.hingeSparHeight == 6.35);
    assert(installed.builtUpFlap.hingeSparHeight == 6.35);
    assert(installed.builtUpAileron.hingeSparWidth == 9.525 && installed.builtUpFlap.hingeSparWidth == 9.525);
    assert(installed.builtUpAileron.hingeAngleDegrees == 35 && installed.builtUpFlap.hingeAngleDegrees == 35);
    WingPanelEditor editor{installed, unit};
    for (const QString prefix : {QString{"aileron"}, QString{"flap"}}) {
      editor.findChild<QRadioButton*>(prefix + "HingeSpars")->setChecked(true);
      auto* height = editor.findChild<LengthInput*>(prefix + "HingeSparHeight");
      assert(height && height->valueMm() == 6.35);
      auto* width = editor.findChild<LengthInput*>(prefix + "HingeSparWidth");
      assert(width && width->valueMm() == 9.525);
      assert(editor.findChild<QDoubleSpinBox*>(prefix + "HingeAngle")->value() == 35);
      if (unit == DisplayUnit::Inches)
        assert(height->findChild<QDoubleSpinBox*>()->text() == "1/4 in" &&
               width->findChild<QDoubleSpinBox*>()->text() == "3/8 in");
    }
    const auto reopened = panelDataFromJson(panelDataToJson(editor.data()));
    assert(reopened.builtUpAileron.hingeSparHeight == 6.35);
    assert(reopened.builtUpFlap.hingeSparHeight == 6.35);
  }
  WingPanelData legacy;
  legacy.builtUpAileron.hingeSparHeight = legacy.builtUpFlap.hingeSparHeight = 1.5;
  legacy.builtUpAileron.hingeSparWidth = legacy.builtUpFlap.hingeSparWidth = 6;
  legacy.builtUpAileron.hingeAngleDegrees = legacy.builtUpFlap.hingeAngleDegrees = 45;
  migrateLegacyHingeSparDefaults(legacy);
  assert(legacy.builtUpAileron.hingeSparHeight == 6.35 && legacy.builtUpFlap.hingeSparHeight == 6.35);
  assert(legacy.builtUpAileron.hingeSparWidth == 9.525 && legacy.builtUpFlap.hingeSparWidth == 9.525);
  assert(legacy.builtUpAileron.hingeAngleDegrees == 35 && legacy.builtUpFlap.hingeAngleDegrees == 35);
  legacy.builtUpAileron.hingeSparHeight = 4.75;
  legacy.builtUpFlap.hingeSparHeight = 8;
  legacy.builtUpAileron.hingeSparWidth = 12; legacy.builtUpFlap.hingeSparWidth = 11;
  legacy.builtUpAileron.hingeAngleDegrees = 20; legacy.builtUpFlap.hingeAngleDegrees = 40;
  migrateLegacyHingeSparDefaults(legacy);
  assert(legacy.builtUpAileron.hingeSparHeight == 4.75 && legacy.builtUpFlap.hingeSparHeight == 8);
  assert(legacy.builtUpAileron.hingeSparWidth == 12 && legacy.builtUpFlap.hingeSparWidth == 11);
  assert(legacy.builtUpAileron.hingeAngleDegrees == 20 && legacy.builtUpFlap.hingeAngleDegrees == 40);
  WingPanelData project;
  project.builtUpAileron.hingeSparHeight = project.builtUpFlap.hingeSparHeight = 1.5;
  project.builtUpAileron.hingeSparWidth = project.builtUpFlap.hingeSparWidth = 6;
  project.builtUpAileron.hingeAngleDegrees = project.builtUpFlap.hingeAngleDegrees = 45;
  const auto original = panelDataFromJson(panelDataToJson(project));
  assert(original.builtUpAileron.hingeSparHeight == 1.5 && original.builtUpFlap.hingeSparHeight == 1.5);
  assert(original.builtUpAileron.hingeSparWidth == 6 && original.builtUpFlap.hingeSparWidth == 6);
  assert(original.builtUpAileron.hingeAngleDegrees == 45 && original.builtUpFlap.hingeAngleDegrees == 45);
}

void testControlDirectionsAndSparSafety() {
  using namespace designrc::gui;
  WingPanelData values;
  values.ailerons = values.flaps = true;
  values.aileronPerpendicularToRibs = values.flapPerpendicularToRibs = true;
  WingPanelEditor editor{values};
  for (const QString prefix : {QString{"aileron"}, QString{"flap"}}) {
    auto* option = editor.findChild<QCheckBox*>(prefix + "PerpendicularToRibs");
    auto* label = editor.findChild<QLabel*>(prefix + "WidthLabel");
    assert(option && option->text() == "Perpendicular to Ribs" && option->isChecked());
    assert(label && label->text() == "Root Width");
    const auto* layout = qobject_cast<QVBoxLayout*>(option->parentWidget()->layout());
    assert(layout && layout->indexOf(option) < layout->indexOf(label->parentWidget()));
    option->setChecked(false); assert(label->text() == "Width");
    option->setChecked(true);
  }
  const auto saved = panelDataToJson(editor.data());
  const auto restored = panelDataFromJson(saved);
  assert(restored.aileronPerpendicularToRibs && restored.flapPerpendicularToRibs);
  WingPanelEditor defaults{restored, DisplayUnit::Inches, true};
  assert(defaults.findChild<QLabel*>("aileronWidthLabel")->text() == "Root Width");
  auto legacy = saved; legacy.remove("aileronPerpendicularToRibs"); legacy.remove("flapPerpendicularToRibs");
  assert(!panelDataFromJson(legacy).aileronPerpendicularToRibs);
  editor.findChild<QCheckBox*>("aileronHingeParallelY")->setChecked(true);
  assert(!editor.data().aileronPerpendicularToRibs);

  WingPanelData panel;
  panel.rootChord = 200; panel.tipChord = 140; panel.sweep = 30;
  panel.trailingEdgeType = 2; panel.trailingEdgeWidth = 40;
  SparDefaults rear;
  rear.material = 0; rear.verticalLocation = 0; rear.woodWidth = 4; rear.woodHeight = 2;
  rear.chordLocationPercent = rear.tipChordLocationPercent = 85;
  panel.spars = {rear};
  WingPanelEditor tapered{panel};
  assert(tapered.correctRearSparLocations());
  const auto fitted = tapered.data().spars.front();
  const double rootX = fitted.chordLocationPercent * 2.0;
  const double tipX = 30 + fitted.tipChordLocationPercent * 1.4;
  assert(std::abs((tipX - rootX) - (-30.0)) < .04);
  assert(std::abs(fitted.chordLocationPercent - fitted.tipChordLocationPercent) > 1);
  assert(tapered.rollbackRearSparLocations());
  assert(tapered.data().spars.front().chordLocationPercent == 85);
  assert(tapered.correctRearSparLocations());
  tapered.acceptRearSparLocations();
  assert(!tapered.rollbackRearSparLocations());

  panel.tipChord = 200; panel.sweep = 0;
  auto front = rear; front.chordLocationPercent = front.tipChordLocationPercent = 78;
  front.woodWidth = 6;
  panel.spars = {front, rear};
  WingPanelEditor blocked{panel};
  assert(!blocked.correctRearSparLocations());
  const auto unchanged = blocked.data().spars;
  assert(unchanged[0].chordLocationPercent == 78 && unchanged[1].chordLocationPercent == 85);
  assert(unchanged[0].tipChordLocationPercent == 78 && unchanged[1].tipChordLocationPercent == 85);
}

void testSleeveIds() {
  using namespace designrc::gui;
  {
    WingPanelData panel;
    panel.joinerPanelMode = 0;
    RemovableJoinerData sleeve;
    sleeve.thisPanelPart = sleeve.adjoiningPanelPart = 0;
    panel.removableJoiners = {sleeve};
    WingPanelEditor editor{panel, DisplayUnit::Millimeters, true};
    auto* od = editor.findChild<LengthInput*>("removableThisSleeveOd");
    auto* id = editor.findChild<LengthInput*>("removableThisSleeveId");
    auto* adjoiningId = editor.findChild<LengthInput*>("removableAdjoiningSleeveId");
    assert(od && id && adjoiningId);
    assert(id->valueMm() == 6 && adjoiningId->valueMm() == 6);
    od->findChild<QDoubleSpinBox*>()->setValue(9);
    assert(id->valueMm() == 8);
    id->findChild<QDoubleSpinBox*>()->setValue(7.5);
    od->findChild<QDoubleSpinBox*>()->setValue(10);
    assert(id->valueMm() == 7.5);
    QString error;
    assert(editor.validate(error));
    const auto saved = panelDataFromJson(panelDataToJson(editor.data()));
    assert(saved.removableJoiners.front().thisSleeveId == 7.5);
    assert(saved.removableJoiners.front().adjoiningSleeveId == 6);
    WingPanelEditor reopened{saved, DisplayUnit::Inches, true};
    assert(reopened.findChild<LengthInput*>("removableThisSleeveId")->valueMm() == 7.5);
    id->findChild<QDoubleSpinBox*>()->setValue(10);
    assert(!editor.validate(error) && error.contains("smaller than OD"));
    auto legacy = panelDataToJson(panel);
    auto rows = legacy["removableJoiners"].toArray();
    auto old = rows[0].toObject(); old.remove("thisSleeveId"); old.remove("adjoiningSleeveId");
    old.insert("thisRodOd", 5); old.insert("adjoiningRodOd", 4); rows[0] = old;
    legacy["removableJoiners"] = rows;
    const auto migrated = panelDataFromJson(legacy).removableJoiners.front();
    assert(migrated.thisSleeveId == 5 && migrated.adjoiningSleeveId == 4);
    const auto startup = installedDefaultPanelData(DisplayUnit::Inches).removableJoiners.front();
    assert(startup.thisSleeveOd == startup.adjoiningSleeveOd);
    assert(startup.thisSleeveId == startup.adjoiningSleeveId);
    panel.removableJoiners.front().thisSleeveOd = 8;
    panel.removableJoiners.front().thisSleeveId = 6.5;
    panel.removableJoiners.front().adjoiningSleeveOd = 9;
    panel.removableJoiners.front().adjoiningSleeveId = 7;
    WingPanelEditor customDefaults{panel, DisplayUnit::Millimeters, true};
    const auto custom = customDefaults.data().removableJoiners.front();
    assert(custom.thisSleeveOd == 8 && custom.thisSleeveId == 6.5);
    assert(custom.adjoiningSleeveOd == 9 && custom.adjoiningSleeveId == 7);
  }
}

void testRearSparPlacement() {
  using namespace designrc::gui;
  {
    for (int obstruction = 0; obstruction < 3; ++obstruction) {
      WingPanelData panel;
      panel.rootChord = panel.tipChord = 200; panel.sweep = 0;
      panel.dihedral = 0; panel.ribCount = 7;
      panel.trailingEdgeType = obstruction == 0 ? 2 : 0;
      panel.trailingEdgeWidth = 40;
      panel.ailerons = obstruction == 1; panel.flaps = obstruction == 2;
      panel.aileronWidth = panel.flapWidth = 40;
      panel.aileronStartRib = panel.flapStartRib = 2;
      panel.aileronStopRib = 7; panel.flapStopRib = 6;
      SparDefaults spar;
      spar.material = 0; spar.verticalLocation = 0;
      spar.woodWidth = 4; spar.woodHeight = 2;
      spar.chordLocationPercent = spar.tipChordLocationPercent = 85;
      panel.spars = {spar};
      WingPanelEditor editor{panel};
      QString error;
      assert(!editor.validate(error) && error.contains("intersects rear TE"));
      assert(editor.data().spars.front().chordLocationPercent == 85);
      assert(editor.correctRearSparLocations());
      assert(editor.validate(error));
      const auto adjusted = editor.data().spars.front();
      assert(adjusted.chordLocationPercent < 80 && adjusted.tipChordLocationPercent < 80);
      const auto* key = obstruction == 0 ? "trailingEdgeWidth" : obstruction == 1 ? "aileronWidth" : "flapWidth";
      editor.findChild<LengthInput*>(key)->findChild<QDoubleSpinBox*>()->setValue(60);
      const auto widened = editor.data().spars.front();
      assert(widened.chordLocationPercent < adjusted.chordLocationPercent);
      assert(widened.tipChordLocationPercent < adjusted.tipChordLocationPercent);
      assert(editor.validate(error));
      assert(!editor.correctRearSparLocations());
    }
  }
}
} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
  _set_error_mode(_OUT_TO_STDERR);
#endif
  QApplication application{argc, argv};
  using namespace designrc::gui;
  testHingeSparHeightDefaults();
  testControlDirectionsAndSparSafety();
  testSleeveIds();
  testRearSparPlacement();
  {
    for (const auto target : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
      const auto previous = target == DisplayUnit::Millimeters
          ? DisplayUnit::Inches : DisplayUnit::Millimeters;
      for (const int joinerMode : {0, 1}) {
        auto saved = installedDefaultPanelData(DisplayUnit::Inches);
        saved.builtUpAileron.spars.push_back({});
        WingPanelEditor defaults{saved, previous, true};
        defaults.convertDefaultLengthUnits(target);
        saved = defaults.data();
        auto current = installedDefaultPanelData(DisplayUnit::Inches);
        current.joinerPanelMode = joinerMode;
        current.panelSpan = 1234.5;
        current.rootChord = 321.25;
        current.sweep = 17.25;
        current.ribCount = 13;
        current.dihedral = 11;
        current.twist = 2;
        current.spars.resize(3, current.spars.front());
        current.builtUpAileron.spars.resize(2);
        current.fixedJoiners.resize(2, current.fixedJoiners.front());
        current.unitOverrides.insert("rootChord", target == DisplayUnit::Inches
            ? UnitOverride::Inches : UnitOverride::Millimeters);
        current.unitOverrides.insert("sweep", previous == DisplayUnit::Inches
            ? UnitOverride::Inches : UnitOverride::Millimeters);
        WingPanelEditor project{current, previous};
        assert(project.applyDefaultLengthUnits(saved, target));
        const auto result = project.data();
        assert(result.panelSpan == saved.panelSpan);
        assert(result.rootChord == current.rootChord);
        assert(result.sweep == saved.sweep);
        assert(result.unitOverrides.value("rootChord") == current.unitOverrides.value("rootChord"));
        assert(result.unitOverrides.value("sweep") == UnitOverride::Global);
        assert(result.ribCount == 13 && result.dihedral == 11 && result.twist == 2);
        assert(result.spars.size() == 3);
        assert(result.spars.back().woodHeight == saved.spars.back().woodHeight);
        assert(result.builtUpAileron.spars.size() == 2);
        assert(result.builtUpAileron.spars.back().woodHeight == saved.builtUpAileron.spars.back().woodHeight);
        if (joinerMode == 1) {
          assert(result.fixedJoiners.size() == 2);
          assert(result.fixedJoiners.back().woodThickness == saved.fixedJoiners.back().woodThickness);
        } else {
          assert(result.removableJoiners.size() == current.removableJoiners.size());
          assert(result.removableJoiners.front().thisRodOd == saved.removableJoiners.front().thisRodOd);
        }
        // Saving defaults again in the same unit preserves current dimensions,
        // even when those defaults were subsequently edited.
        saved.panelSpan += 100;
        assert(!project.applyDefaultLengthUnits(saved, target));
        assert(project.data().panelSpan == result.panelSpan);
        const auto reopened = panelDataFromJson(panelDataToJson(result));
        assert(reopened.panelSpan == result.panelSpan);
        assert(reopened.rootChord == current.rootChord);
      }
    }
  }
  {
    // Defaults conversion covers hidden fields and dynamically added stock,
    // while unit overrides and the current project's dimensions stay intact.
    for (const int joinerMode : {0, 1}) {
      auto source = installedDefaultPanelData(DisplayUnit::Inches);
      source.joinerPanelMode = joinerMode;
      source.sweep = -4.7625;
      source.builtUpAileron.spars.push_back({});
      source.builtUpFlap.spars.push_back({});
      source.unitOverrides.insert("leadingEdgeWidth", UnitOverride::Inches);
      source.unitOverrides.insert("trailingEdgeWidth", UnitOverride::Millimeters);
      WingPanelEditor project{source, DisplayUnit::Inches};
      const auto original = project.data();
      WingPanelEditor defaults{source, DisplayUnit::Inches, true};
      const auto inputs = defaults.findChildren<LengthInput*>();
      std::vector<double> metricValues;
      for (const auto* input : inputs) {
        const double value = input->valueMm();
        metricValues.push_back(input->unitOverride() != UnitOverride::Global
            ? value : std::abs(value - 1.5875) < 1.0e-8
                ? 1.5 : std::round(value));
      }
      defaults.convertDefaultLengthUnits(DisplayUnit::Millimeters);
      for (qsizetype i = 0; i < inputs.size(); ++i)
        assert(std::abs(inputs[i]->valueMm() - metricValues[i]) < 1.0e-8);
      const auto metric = defaults.data();
      assert(metric.panelSpan == 699 && metric.sweep == -5);
      assert(metric.topTeSheetingThickness == 1.5);
      assert(metric.builtUpAileron.topSheetingThickness == 1.5);
      assert(metric.builtUpFlap.bottomSheetingThickness == 1.5);
      assert(metric.dihedral == original.dihedral && metric.twist == original.twist);
      assert(metric.ribCount == original.ribCount);
      assert(metric.spars[0].woodHeight == 3);
      assert(metric.builtUpAileron.spars[0].woodHeight == 5);
      if (joinerMode == 1)
        assert(metric.fixedJoiners[0].woodThickness == 3);
      else
        assert(metric.removableJoiners[0].thisRodOd == 5);
      // Saving/reopening defaults and using them for a new editor retains the
      // rounded dimensions. Merely opening Defaults or keeping mm is a no-op.
      const auto saved = panelDataFromJson(panelDataToJson(metric));
      WingPanelEditor newProject{saved, DisplayUnit::Millimeters};
      assert(newProject.data().panelSpan == 699);
      assert(newProject.data().topTeSheetingThickness == 1.5);
      defaults.convertDefaultLengthUnits(DisplayUnit::Millimeters);
      assert(defaults.data().topTeSheetingThickness == 1.5);
      assert(project.data().panelSpan == original.panelSpan);
      assert(project.data().topTeSheetingThickness == original.topTeSheetingThickness);
      project.setGlobalUnit(DisplayUnit::Millimeters);
      assert(project.data().panelSpan == original.panelSpan);
      assert(project.data().topTeSheetingThickness == original.topTeSheetingThickness);

      defaults.convertDefaultLengthUnits(DisplayUnit::Inches);
      for (qsizetype i = 0; i < inputs.size(); ++i) {
        const double expected = inputs[i]->unitOverride() != UnitOverride::Global
            ? metricValues[i] : std::round(metricValues[i] / 25.4 * 32) / 32 * 25.4;
        assert(std::abs(inputs[i]->valueMm() - expected) < 1.0e-8);
      }
      assert(std::abs(defaults.data().topTeSheetingThickness - 1.5875) < 1.0e-8);
      auto* thickness = defaults.findChild<LengthInput*>("topTeSheetingThickness");
      assert(thickness->findChild<QDoubleSpinBox*>()->text().contains("1/16"));
    }
  }
  {
    WingPanelData values;
    values.ailerons = values.flaps = true;
    for (auto* options : {&values.builtUpAileron, &values.builtUpFlap}) {
      options->enabled = true;
      options->hingeSpars = true;
      options->hingeSparWidth = 7.0;
      options->hingeSparHeight = 1.75;
      options->hingeAngleDegrees = 35;
    }
    WingPanelEditor editor{values};
    for (const QString prefix : {QString{"aileron"}, QString{"flap"}}) {
      auto* post = editor.findChild<QRadioButton*>(prefix + "HingePost");
      auto* spars = editor.findChild<QRadioButton*>(prefix + "HingeSpars");
      auto* postDetails = editor.findChild<QWidget*>(prefix + "HingePostDetails");
      auto* sparDetails = editor.findChild<QWidget*>(prefix + "HingeSparDetails");
      assert(post && spars && postDetails && sparDetails);
      assert(spars->isChecked() && !post->isChecked());
      assert(postDetails->isHidden() && !sparDetails->isHidden());
      assert(!editor.findChild<LengthInput*>(prefix + "TopSheetingWidth"));
      assert(!editor.findChild<LengthInput*>(prefix + "BottomSheetingWidth"));
      assert(!editor.findChild<QCheckBox*>(prefix + "TopSheetingTaper"));
      assert(!editor.findChild<QCheckBox*>(prefix + "BottomSheetingTaper"));
      post->setChecked(true);
      assert(!spars->isChecked() && !postDetails->isHidden() && sparDetails->isHidden());
      assert(!editor.findChild<LengthInput*>(prefix + "TopSheetingWidth"));
      spars->setChecked(true);
      assert(!post->isChecked() && postDetails->isHidden() && !sparDetails->isHidden());
      assert(!editor.findChild<LengthInput*>(prefix + "BottomSheetingWidth"));
    }
    const auto saved = panelDataToJson(editor.data());
    const auto loaded = panelDataFromJson(saved);
    for (const auto* options : {&loaded.builtUpAileron, &loaded.builtUpFlap}) {
      assert(options->hingeSpars && options->hingeSparWidth == 7.0);
      assert(options->hingeSparHeight == 1.75 && options->hingeAngleDegrees == 35);
    }
    auto old = saved;
    for (const QString key : {QString{"builtUpAileron"}, QString{"builtUpFlap"}}) {
      auto options = old.value(key).toObject();
      options.remove("hingeSpars"); options.remove("hingeAngleDegrees");
      options.insert("topSheetingTaper", true);
      old.insert(key, options);
    }
    const auto migrated = panelDataFromJson(old);
    assert(!migrated.builtUpAileron.hingeSpars && !migrated.builtUpFlap.hingeSpars);
    assert(!migrated.builtUpAileron.topSheetingTaper && !migrated.builtUpFlap.topSheetingTaper);
    using namespace designrc::domain;
    WingParameters panel;
    panel.rootChord = panel.tipChord = 200; panel.halfSpan = 250;
    panel.ribCount = 7; panel.dihedralDegrees = panel.sweep = 0;
    const auto foil = AirfoilProfile::nacaSymmetric(.15);
    StructureParameters structure;
    structure.ailerons = structure.flaps = true;
    structure.flapStartRib = 2; structure.flapStopRib = 4;
    structure.aileronStartRib = 4; structure.aileronStopRib = 7;
    structure.flapWidth = structure.aileronWidth = 50;
    structure.builtUpAileron = loaded.builtUpAileron;
    structure.builtUpFlap = loaded.builtUpFlap;
    const auto wing = applyWingStructure(generateRibs(panel, foil, foil), structure);
    const auto plan = buildFlattenedWingPlan({wing}, {3}, {loaded}, false, "HingeSpars.designrc");
    assert(std::count_if(plan.texts.begin(), plan.texts.end(), [](const auto& text) {
      return text.text.contains("hinge spar");
    }) == 8);
    assert(std::count_if(plan.texts.begin(), plan.texts.end(), [](const auto& text) {
      return text.text.contains("Hinge Angle:");
    }) == 2);
    assert(std::none_of(plan.texts.begin(), plan.texts.end(), [](const auto& text) {
      return text.text.contains("Hinge Post:");
    }));
  }
  if (argc > 1 && std::string{argv[1]} == "--hinge-options") return 0;
  {
    // Deleting the last modern spar must not resurrect hidden legacy spars.
    auto defaults=installedDefaultPanelData(DisplayUnit::Inches);
    assert(defaults.topSpar && defaults.bottomSpar && defaults.spars.size()==2);
    WingPanelEditor editor{defaults};
    int changes=0;
    QObject::connect(&editor,&WingPanelEditor::changed,[&] {++changes;});
    for(auto* row:editor.findChildren<QWidget*>("sparEditorRow"))
      row->findChild<QCheckBox*>("sparDeleteSelection")->setChecked(true);
    editor.findChild<QPushButton*>("deleteSparButton")->click();
    const auto empty=editor.data();
    assert(changes>0 && empty.spars.empty());
    assert(!empty.topSpar && !empty.bottomSpar && !empty.topRearSpar && !empty.bottomRearSpar && empty.carbonSpar==0);
    auto saved=panelDataToJson(empty);
    // Reproduce the stale flags in JoinerNumBays without modifying the project.
    saved.insert("topSpar",true); saved.insert("bottomSpar",true);
    saved.insert("topRearSpar",true); saved.insert("bottomRearSpar",true); saved.insert("carbonSpar",1);
    const auto reopened=panelDataFromJson(saved);
    assert(reopened.spars.empty() && !reopened.topSpar && !reopened.bottomSpar &&
        !reopened.topRearSpar && !reopened.bottomRearSpar && reopened.carbonSpar==0);
    WingPanelEditor reopenedEditor{reopened};
    assert(reopenedEditor.findChildren<QWidget*>("sparEditorRow").empty());
    assert(reopenedEditor.data().spars.empty());
    // Old projects without a modern list still migrate their spar switches.
    saved.remove("spars"); saved.remove("sparDefaults");
    const auto legacy=panelDataFromJson(saved);
    assert(!legacy.spars.empty());
  }
  {
    WingPanelData data;
    data.ribCount=5; data.joinerPanelMode=1;
    FixedJoinerData fixed; fixed.ribBays=3;
    data.fixedJoiners={fixed};
    RemovableJoinerData sleeve; sleeve.ribBays=2;
    RemovableJoinerData pin; pin.kind=1; pin.ribBays=4;
    data.removableJoiners={sleeve,pin};
    const auto json=panelDataToJson(data);
    const auto restored=panelDataFromJson(json);
    assert(restored.fixedJoiners.front().ribBays==3);
    assert(restored.removableJoiners.front().ribBays==2);
    assert(!json.value("removableJoiners").toArray()[1].toObject().contains("ribBays"));
    WingPanelEditor editor{data};
    auto* fixedBays=editor.findChild<QSpinBox*>("fixedJoinerRibBays");
    assert(fixedBays && fixedBays->minimum()==1 && fixedBays->maximum()==4 && fixedBays->value()==3);
    auto* ribCount=editor.findChild<QSpinBox*>("ribCount");
    ribCount->setValue(3);
    assert(fixedBays->maximum()==2 && editor.data().fixedJoiners.front().ribBays==2);
    data.joinerPanelMode=0;
    WingPanelEditor removable{data,DisplayUnit::Millimeters,true};
    const auto bays=removable.findChildren<QSpinBox*>("removableJoinerRibBays");
    assert(bays.size()==1 && bays.front()->value()==2 && bays.front()->maximum()==4);
    auto legacy=json;
    auto fixedJson=legacy.value("fixedJoiners").toArray();
    auto oldFixed=fixedJson[0].toObject(); oldFixed.remove("ribBays"); fixedJson[0]=oldFixed;
    legacy.insert("fixedJoiners",fixedJson); legacy.insert("addRib1a",true);
    assert(panelDataFromJson(legacy).fixedJoiners.front().ribBays==2);
  }
  {
    using namespace designrc::domain;
    WingParameters p;
    p.rootChord=p.tipChord=200; p.halfSpan=300; p.ribCount=7; p.dihedralDegrees=0; p.sweep=0;
    const auto foil=AirfoilProfile::nacaSymmetric(.15);
    StructureParameters settings;
    settings.ailerons=settings.flaps=true;
    settings.flapStartRib=2; settings.flapStopRib=4;
    settings.aileronStartRib=4; settings.aileronStopRib=7;
    settings.aileronWidth=settings.flapWidth=40;
    settings.aileronHingePostHeight=settings.flapHingePostHeight=30;
    settings.topTeSheeting=true; settings.topTeSheetingWidth=25.4; settings.topTeSheetingThickness=1.5875;
    auto& a=settings.builtUpAileron; auto& f=settings.builtUpFlap;
    a.enabled=f.enabled=true;
    a.topSheeting=a.bottomSheeting=f.topSheeting=f.bottomSheeting=true;
    a.topSheetingWidth=a.bottomSheetingWidth=f.topSheetingWidth=f.bottomSheetingWidth=40;
    a.topSheetingThickness=a.bottomSheetingThickness=f.topSheetingThickness=f.bottomSheetingThickness=.5;
    SparParameters spar;
    spar.material=0; spar.verticalLocation=0; spar.chordLocationPercent=spar.tipChordLocationPercent=35;
    spar.woodWidth=2; spar.woodHeight=1;
    a.spars.push_back(spar);
    spar.material=1; spar.type=1; spar.rodOd=1; spar.verticalLocation=1;
    f.spars.push_back(spar);
    const auto wing=applyWingStructure(generateRibs(p,foil,foil),settings);
    WingPanelData data;
    data.panelSpan=300; data.rootChord=data.tipChord=200; data.ribCount=7;
    data.ailerons=data.flaps=true; data.aileronWidth=data.flapWidth=40;
    data.aileronHingePostHeight=data.flapHingePostHeight=30;
    data.topTeSheeting=true; data.builtUpAileron=a; data.builtUpFlap=f;
    data.unitOverrides.insert("aileronTopSheetingThickness",UnitOverride::Inches);
    data.unitOverrides.insert("aileronSpar0WoodWidth",UnitOverride::Inches);
    const auto document=buildFlattenedWingPlan({wing},{3},{data},false,"ControlAnnotations.designrc");
    const TechnicalDrawingText *ar=nullptr,*fr=nullptr;
    bool aSkin=false,aTe=false,fSkin=false,aSpar=false,fSpar=false;
    for(const auto& label:document.texts) {
      if(label.text=="AR4") ar=&label;
      if(label.text=="FR4") fr=&label;
      aSkin=aSkin||(label.text.startsWith("Aileron Top Sheeting")&&label.text.contains("Thickness:")&&label.text.contains(" in"));
      aTe=aTe||(label.text.startsWith("Aileron Top TE Sheeting")&&label.text.contains("Thickness:")&&label.text.contains(" mm"));
      fSkin=fSkin||(label.text.startsWith("Flap Bottom Sheeting")&&label.text.contains("Thickness:"));
      aSpar=aSpar||(label.text.startsWith("Aileron Spar 1")&&label.text.contains("Top Wood")&&label.text.contains(" in")&&label.text.contains(" mm"));
      fSpar=fSpar||(label.text.startsWith("Flap Spar 1")&&label.text.contains("CF Rod")&&label.text.contains("OD:"));
      assert(!label.text.startsWith("Aileron R")&&!label.text.startsWith("Flap R"));
    }
    assert(ar&&fr&&std::abs(ar->position.x()-fr->position.x())>5);
    assert(aSkin&&aTe&&fSkin&&aSpar&&fSpar);
    std::size_t leaders=0;
    for(const auto& path:document.paths) if(path.path.elementCount()==3&&std::abs(path.lineWidthMm-.25)<1e-8) ++leaders;
    assert(leaders>=8);
    // Only the two main wing views have a full-chord panel perimeter.
    // Built-up flap/aileron ribs still carry a 200 mm parent airfoil chord.
    std::size_t panelOutlines=0;
    for(const auto& path:document.paths)
      if(std::abs(path.lineWidthMm-.45)<1e-8) ++panelOutlines;
    assert(panelOutlines==2);
    const auto sheetCovers=[](const TechnicalDrawingDocument& plan,const QPointF point) {
      return std::any_of(plan.paths.begin(),plan.paths.end(),[&](const auto& path) {
        return path.fill==QColor{212,189,165,65} && path.path.contains(point);
      });
    };
    // Fixed-wing and moving-control sheets must leave both end gaps open.
    assert(!sheetCovers(document,{52.25,252+185}));
    assert(!sheetCovers(document,{152.25,252+185}));
    assert(sheetCovers(document,{25,252+185}));
    assert(sheetCovers(document,{100,252+185}));
    auto wideSettings=settings;
    wideSettings.topTeSheetingWidth=60;
    const auto wideWing=applyWingStructure(generateRibs(p,foil,foil),wideSettings);
    const auto widePlan=buildFlattenedWingPlan({wideWing},{3},{data},false,"WideControlSheet.designrc");
    assert(!sheetCovers(widePlan,{100,252+159.25}));
    auto stockSettings=settings;
    stockSettings.topTeSheeting=false;
    stockSettings.trailingEdgeType=2; stockSettings.trailingEdgeWidth=10; stockSettings.trailingEdgeHeight=30;
    auto stockData=data;
    stockData.topTeSheeting=false;
    stockData.trailingEdgeType=2; stockData.trailingEdgeWidth=10; stockData.trailingEdgeHeight=30;
    stockData.unitOverrides.insert("trailingEdgeWidth",UnitOverride::Inches);
    const auto stockWing=applyWingStructure(generateRibs(p,foil,foil),stockSettings);
    const auto stockPlan=buildFlattenedWingPlan({stockWing},{3},{stockData},false,"ControlStock.designrc");
    bool ate=false,fte=false;
    for(const auto& label:stockPlan.texts) {
      ate=ate||(label.text.startsWith("ATE1\n")&&label.text.contains(" in")&&label.text.contains("30 mm"));
      fte=fte||(label.text.startsWith("FTE1\n")&&label.text.contains(" in")&&label.text.contains("30 mm"));
    }
    assert(ate&&fte);
    for(const auto& control:wing.controlSurfaces) {
      const auto& ribs=control.builtUpWing->ribs;
      const double start=ribs.front().rib.spanPosition+
          ribs.front().rib.ribThicknessStartFactor*3;
      const double stop=ribs.back().rib.spanPosition+
          (ribs.back().rib.ribThicknessStartFactor+1)*3;
      bool postReachesFaces=false;
      for(const auto& path:document.paths) {
        const auto bounds=path.path.boundingRect();
        if(std::abs(path.lineWidthMm-.22)<1e-8 &&
           std::abs(bounds.top()-172)<1e-6 &&
           std::abs(bounds.height()-control.hingePostWidth)<1e-6 &&
           std::abs(bounds.left()-(300-stop))<1e-6 &&
           std::abs(bounds.right()-(300-start))<1e-6)
          postReachesFaces=true;
      }
      assert(postReachesFaces);
    }
  }

  {
    WingPanelData d;
    d.ailerons = true; d.flaps = true;
    d.topTeSheeting = true; d.topTeSheetingWidth = 12;
    d.builtUpAileron.enabled = true;
    d.builtUpAileron.topSheeting = true;
    d.builtUpAileron.topSheetingWidth = 18;
    d.builtUpFlap.enabled = true;
    d.builtUpFlap.bottomSheeting = true;
    designrc::domain::SparParameters spar;
    spar.verticalLocation = 1; spar.material = 0;
    spar.woodWidth = 2; spar.woodHeight = 1;
    d.builtUpFlap.spars.push_back(spar);
    d.unitOverrides.insert("aileronTopSheetingWidth", UnitOverride::Inches);
    d.unitOverrides.insert("flapSpar0WoodHeight", UnitOverride::Millimeters);
    WingPanelEditor editor{d};
    assert(editor.findChild<QFrame*>("aileronFlapDivider"));
    auto restored = editor.data();
    assert(restored.builtUpAileron.enabled && restored.builtUpFlap.enabled);
    assert(restored.topTeSheeting && restored.topTeSheetingWidth == 12);
    assert(restored.builtUpAileron.topSheetingWidth == 18);
    assert(restored.builtUpFlap.spars.size() == 1);
    auto* add = editor.findChild<QPushButton*>("aileronAddSpar");
    add->click();
    assert(editor.data().builtUpAileron.spars.size() == 1);
    auto* row = add->parentWidget()->findChildren<QGroupBox*>("controlSparEditorRow").back();
    for (auto* radio : row->findChildren<QRadioButton*>())
      if (radio->text() == "Mid") assert(radio->isHidden());
    row->findChild<QCheckBox*>("sparDeleteSelection")->setChecked(true);
    editor.findChild<QPushButton*>("aileronDeleteSpar")->click();
    assert(editor.data().builtUpAileron.spars.empty());
    restored = panelDataFromJson(panelDataToJson(editor.data()));
    assert(restored.builtUpFlap.spars.size() == 1);
    assert(restored.builtUpFlap.spars.front().woodHeight == 1);
    // Defaults uses the same editor with per-parameter unit selectors enabled.
    WingPanelEditor defaults{restored, DisplayUnit::Inches, true};
    defaults.setGlobalUnit(DisplayUnit::Millimeters);
    const auto saved = defaults.data();
    assert(saved.builtUpAileron.enabled && saved.builtUpAileron.topSheetingWidth == 18);
    assert(saved.builtUpFlap.spars.front().verticalLocation == 1);
    assert(saved.unitOverrides.value("aileronTopSheetingWidth") == UnitOverride::Inches);
    assert(saved.unitOverrides.value("flapSpar0WoodHeight") == UnitOverride::Millimeters);
    const auto old = panelDataFromJson(QJsonObject{});
    assert(!old.builtUpAileron.enabled && !old.builtUpFlap.enabled);
    designrc::domain::WingParameters wing;
    wing.halfSpan = 250; wing.rootChord = wing.tipChord = 200; wing.ribCount = 7;
    designrc::domain::StructureParameters structure;
    structure.ailerons = true; structure.aileronStartRib = 2; structure.aileronStopRib = 7;
    structure.builtUpAileron.enabled = true;
    structure.builtUpAileron.topSheeting = true;
    structure.builtUpAileron.topSheetingThickness = .5;
    auto model = testsupport::buildStructure(
        designrc::domain::generateRibs(wing, d.rootAirfoil, d.tipAirfoil), structure);
    const auto drawing = buildFlattenedWingPlan({model}, {3}, {d}, false, "BuiltUp.designrc");
    assert(!drawing.empty());
    bool hasControlRib = false;
    for (const auto& text : drawing.texts) hasControlRib = hasControlRib || text.text.startsWith("AR");
    assert(hasControlRib);
    const auto& rib = model.controlSurfaces.back().builtUpWing->ribs.front();
    const auto cutting = designrc::domain::makeStructuredRibPartDrawing(rib, rib.name);
    assert(!cutting.paths.empty());

  }

  {
    WingPanelData inner, outer;
    outer.planform = {{{0, 0}, {-10, .5}, {0, 1}},
                      {{200, 0}, {210, .5}, {160, 1}}, 300, 300};
    outer.rootChord = 200;
    outer.tipChord = 160;
    outer.curveFileName = "joint.svg";
    for (const bool importedInner : {false, true}) {
      for (const double difference : {-6.35, -2., 0., 2., 6.35}) {
        inner.tipChord = 200 + difference;
        inner.planform = importedInner
            ? designrc::domain::PlanformCurves{{{0, 0}, {0, 1}},
                {{250, 0}, {inner.tipChord, 1}}, 300, 300}
            : designrc::domain::PlanformCurves{};
        std::vector<WingPanelData> panels{inner, outer};
        assert(matchPanelRootChords(panels).isEmpty());
        assert(std::abs(panels[1].rootChord - panels[0].tipChord) < 1e-9);
        assert(std::abs(panels[1].planform.chord(0) - panels[0].tipChord) < 1e-9);
        assert(panels[1].tipChord == 160 && panels[1].planform.chord(1) == 160);
        assert(panels[1].planform.leadingX(.5) == -10);
        assert(std::abs(panels[1].planform.trailingX(.5) - (210 + difference / 2)) < 1e-9);
        const auto once = panels[1].planform.trailing;
        assert(matchPanelRootChords(panels).isEmpty());
        assert(panels[1].planform.trailing[1].x == once[1].x);
        WingPanelEditor editor{outer};
        editor.setMatchedRootChord(panels[1]);
        const auto saved = panelDataFromJson(panelDataToJson(editor.data()));
        assert(std::abs(saved.rootChord - panels[0].tipChord) < 1e-9);
        assert(saved.curveFileName == outer.curveFileName);
      }
    }
    inner.planform = {};
    inner.tipChord = 206.351;
    std::vector<WingPanelData> invalid{inner, outer};
    const auto error = matchPanelRootChords(invalid);
    assert(error.contains("Panel 1") && error.contains("Panel 2") && error.contains("6.35"));
    assert(invalid[1].planform.chord(0) == 200); // No partial edits on failure.
    inner.tipChord = 193.65;
    auto narrow = outer;
    narrow.planform.trailing[1].x = -8; // Matching would cross LE inboard.
    invalid = {inner, narrow};
    assert(!matchPanelRootChords(invalid).isEmpty());
    assert(invalid[1].planform.chord(0) == 200);
    WingPanelData conventional;
    auto firstCurve = outer;
    auto lastCurve = outer;
    lastCurve.planform.trailing.front().x = conventional.tipChord + 2;
    std::vector<WingPanelData> chain{firstCurve, conventional, lastCurve};
    assert(matchPanelRootChords(chain).isEmpty());
    assert(chain[1].rootChord == firstCurve.tipChord);
    assert(chain[2].planform.chord(0) == chain[1].tipChord);
  }
  {
    WingPanelData data;
    data.panelSpan = 100;
    data.curveFileName = "elliptical.svg";
    data.planform = {{{0, 0}, {-10, .5}, {0, 1}}, {{200, 0}, {220, .5}, {160, 1}}, 100, 100};
    data.ailerons = true;
    data.aileronHingeParallelY = true;
    auto pending = panelDataToJson(data);
    pending.insert("panelSpan", 200);
    assert(panelDataFromJson(pending).panelSpan == 200);
    const auto restored = panelDataFromJson(panelDataToJson(data));
    assert(restored.curveFileName == data.curveFileName && restored.aileronHingeParallelY);
    assert(restored.planform.chord(.5) == 230 && restored.rootChord == 200 &&
           restored.tipChord == 160);
    WingPanelEditor editor{restored};
    assert(editor.findChild<QPushButton*>("importLeTeCurves"));
    assert(editor.findChild<QLabel*>("curveFileName")->text() == "elliptical.svg");
    assert(!editor.findChild<LengthInput*>("rootChord")->isEnabled());
    assert(!editor.findChild<LengthInput*>("tipChord")->isEnabled());
    assert(!editor.findChild<LengthInput*>("sweep")->isEnabled());
    assert(editor.findChild<LengthInput*>("sweep")->findChild<QLineEdit*>()->text() == "--");
    editor.setGlobalUnit(DisplayUnit::Inches);
    assert(editor.findChild<LengthInput*>("sweep")->findChild<QLineEdit*>()->text() == "--");
    assert(editor.findChild<QCheckBox*>("aileronHingeParallelY")->isChecked());
    QString error;
    editor.findChild<LengthInput*>("panelSpan")->setValueMm(107);
    assert(!editor.validate(error) && error.contains("6.35"));
    editor.findChild<LengthInput*>("panelSpan")->setValueMm(100);
    editor.findChild<QPushButton*>("deleteLeTeCurves")->click();
    assert(editor.data().planform.empty() && editor.data().curveFileName.isEmpty());
    assert(editor.findChild<LengthInput*>("rootChord")->isEnabled());
    assert(editor.findChild<LengthInput*>("sweep")->isEnabled());
    assert(editor.findChild<LengthInput*>("sweep")->findChild<QLineEdit*>()->text() != "--");
    assert(editor.data().rootChord == 200 && editor.data().tipChord == 160);
    assert(editor.findChild<QCheckBox*>("aileronHingeParallelY")->isHidden());
    auto invalid = panelDataToJson(data);
    auto curve = invalid.value("planform").toObject();
    curve.insert("leading", QJsonArray{});
    invalid.insert("planform", curve);
    bool rejected = false;
    try {
      (void)panelDataFromJson(invalid);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    assert(rejected);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTemporaryDir sourceDirectory;
    const auto sourcePath = sourceDirectory.filePath("wing.svg");
    const auto writeSource = [&](const QByteArray& contents) {
      QFile file{sourcePath};
      assert(file.open(QIODevice::WriteOnly));
      assert(file.write(contents) == contents.size());
    };
    editor.findChild<LengthInput*>("panelSpan")->setValueMm(300);
    writeSource("<svg width='250mm' height='300mm' viewBox='0 0 250 300'><path d='M0 0 Q-20 150 0 "
                "300'/><path d='M200 0 Q240 150 160 300'/></svg>");
    int changes = 0, dialogs = 0;
    QObject::connect(&editor, &WingPanelEditor::changed, [&] { ++changes; });
    QTimer dialogDriver;
    QObject::connect(&dialogDriver, &QTimer::timeout, [&] {
      if (auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        file->selectFile(sourcePath);
        QMetaObject::invokeMethod(file, "accept");
      } else if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        ++dialogs;
        message->accept();
      }
    });
    dialogDriver.start(10);
    editor.findChild<QPushButton*>("importLeTeCurves")->click();
    assert(changes == 1 && dialogs == 0 && editor.data().curveFileName == "wing.svg");
    assert(std::abs(editor.data().planform.leadingX(.5) + 10) < .02);
    changes = 0;
    writeSource("<svg width='250px' height='100px'><path d='M0 0 L0 100'/><path d='M200 0 L160 "
                "100'/></svg>");
    editor.findChild<QPushButton*>("importLeTeCurves")->click();
    assert(changes == 0 && dialogs == 1 && editor.data().curveFileName == "wing.svg");
    assert(std::abs(editor.data().planform.leadingX(.5) + 10) < .02);
    for (auto* button : editor.findChildren<QRadioButton*>())
      if (button->text() == "CF Tube LE") button->click();
    assert(dialogs == 2 && editor.data().leadingEdgeType == 3);
    dialogDriver.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);

    designrc::domain::WingParameters parameters;
    parameters.halfSpan = 100;
    parameters.planform = restored.planform;
    parameters.ribCount = 5;
    parameters.dihedralDegrees = 0;
    const auto foil = designrc::domain::AirfoilProfile::nacaSymmetric(.12);
    const auto wing = testsupport::buildStructure(
        designrc::domain::generateRibs(parameters, foil, foil), {});
    const auto plan = buildFlattenedWingPlan({wing}, {3}, {restored}, false, {});
    int curvedLeadingOutlines = 0;
    for (const auto& path : plan.paths)
      if (path.path.elementCount() == 3) {
        const auto a = path.path.elementAt(0), b = path.path.elementAt(1),
                   c = path.path.elementAt(2);
        if (std::abs(b.y - a.y + 10) < 1e-6 && std::abs(c.y - a.y) < 1e-6) ++curvedLeadingOutlines;
      }
    assert(curvedLeadingOutlines == 2);
  }

  for (const auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    const auto defaults = installedDefaultPanelData(unit);
    assert(!defaults.topRibCaps && !defaults.bottomRibCaps);
    assert(defaults.topRibCapThickness == 1.5875 && defaults.bottomRibCapThickness == 1.5875);
    assert(defaults.topRibCapWidth == 6.35 && defaults.bottomRibCapWidth == 6.35);
    // The same editor serves regular panels and Settings -> Defaults.
    for (const bool defaultsMode : {false, true}) {
      WingPanelEditor editor{defaults, unit, defaultsMode};
      auto* tabs = editor.findChild<QTabWidget*>();
      assert(tabs->tabText(2) == "Rib Caps");
      auto* top = editor.findChild<QCheckBox*>("topRibCaps");
      auto* bottom = editor.findChild<QCheckBox*>("bottomRibCaps");
      auto* details = editor.findChild<QWidget*>("topRibCapDetails");
      auto* bottomDetails = editor.findChild<QWidget*>("bottomRibCapDetails");
      assert(top && bottom && !top->isChecked() && !bottom->isChecked());
      assert(details->isHidden() && bottomDetails->isHidden());
      top->setChecked(true);
      assert(!details->isHidden() && bottomDetails->isHidden());
      bottom->setChecked(true);
      assert(!bottomDetails->isHidden());
      auto* width = editor.findChild<LengthInput*>("topRibCapWidth");
      auto* thickness = editor.findChild<LengthInput*>("bottomRibCapThickness");
      assert(width->unitOverride() == UnitOverride::Global);
      if (unit == DisplayUnit::Inches)
        assert(width->findChild<QDoubleSpinBox*>()->text() == "1/4 in");
      auto* spin = width->findChild<QDoubleSpinBox*>();
      spin->findChild<QLineEdit*>()->setText("3/8 in");
      spin->interpretText();
      assert(std::abs(width->valueMm() - 9.525) < 1.0e-8);
      auto* thicknessSpin = thickness->findChild<QDoubleSpinBox*>();
      thicknessSpin->findChild<QLineEdit*>()->setText("2mm");
      thicknessSpin->interpretText();
      const auto saved = panelDataFromJson(panelDataToJson(editor.data()));
      assert(saved.topRibCaps && saved.bottomRibCaps);
      assert(std::abs(saved.topRibCapWidth - 9.525) < 1.0e-8);
      assert(std::abs(saved.bottomRibCapThickness - 2.0) < 1.0e-8);
      assert(saved.unitOverrides.value("topRibCapWidth") == width->unitOverride());
      if (unit == DisplayUnit::Millimeters)
        assert(width->unitOverride() == UnitOverride::Inches);
      WingPanelEditor restored{saved, unit, defaultsMode};
      assert(restored.data().topRibCaps && restored.data().bottomRibCaps);
      width->setValueMm(0.0);
      QString error;
      assert(!editor.validate(error) && error.contains("Rib Caps"));
      top->setChecked(false);
      assert(details->isHidden() && !bottomDetails->isHidden());
    }
  }
  const auto oldProject = panelDataFromJson(QJsonObject{});
  assert(!oldProject.topRibCaps && !oldProject.bottomRibCaps);

  {
    // Saved three-panel glider: TE stock displaces the rear tabs on the
    // smallest outer ribs. Their two supports must not end up at the front.
    using namespace designrc::domain;
    for (const auto fixtureName : {"3PanelGlider.designrc", "3PanelGlider-ag.designrc"}) {
    const auto fixture = std::filesystem::path{__FILE__}.parent_path() /
        "fixtures" / fixtureName;
    QFile file{QString::fromStdString(fixture.string())};
    assert(file.open(QIODevice::ReadOnly));
    const auto panels = QJsonDocument::fromJson(file.readAll()).object().value("panels").toArray();
    assert(panels.size() == 3);
    const auto readPanel = [&](const int index) {
      auto object = panels[index].toObject();
      for (const auto* key : {"rootAirfoilPath", "tipAirfoilPath"}) {
        const auto path = object.value(key).toString();
        if (!path.isEmpty())
          object.insert(key, QString::fromStdString((fixture.parent_path() / path.toStdString()).string()));
      }
      return panelDataFromJson(object);
    };
    for (int panelIndex = 0; panelIndex < panels.size(); ++panelIndex) {
    auto data = readPanel(panelIndex);
    if (panelIndex > 0)
      data.rootAirfoil = readPanel(panelIndex - 1).tipAirfoil;
    std::vector<double> dihedrals, twists;
    for (const auto& panel : panels) {
      dihedrals.push_back(panel.toObject().value("dihedral").toDouble());
      twists.push_back(panel.toObject().value("twist").toDouble());
    }
    const auto angles = calculatePanelAssemblyAngles(dihedrals)[panelIndex];
    const auto twist = calculatePanelTwistRanges(twists)[panelIndex];
    WingParameters parameters;
    parameters.halfSpan = data.panelSpan;
    parameters.rootChord = data.rootChord;
    parameters.tipChord = data.tipChord;
    parameters.sweep = data.sweep;
    parameters.dihedralDegrees = 0.0;
    parameters.rootTwistDegrees = twist.rootTwistDegrees;
    parameters.tipTwistDegrees = twist.tipTwistDegrees;
    parameters.ribCount = data.ribCount;
    auto ribs = generateRibs(parameters, data.rootAirfoil, data.tipAirfoil);
    const double inclination = angles.panelInclinationDegrees * std::numbers::pi / 180.0;
    for (std::size_t i = 0; i < ribs.size(); ++i) {
      const double span = ribs[i].spanPosition;
      ribs[i].spanPosition = 700.0 + std::cos(inclination) * span;
      ribs[i].dihedralHeight = 90.0 + std::sin(inclination) * span;
      ribs[i].ribPlaneAngleDegrees = i == 0 ? angles.rootRibAngleDegrees :
          i + 1 == ribs.size() ? angles.tipRibAngleDegrees : angles.intermediateRibAngleDegrees;
      ribs[i].ribThicknessStartFactor = i == 0 ? 0.0 : i + 1 == ribs.size() ? -1.0 : -0.5;
    }
    StructureParameters structure;
    structure.addFrontBuildTab = data.addFrontBuildTab;
    structure.addRearBuildTab = data.addRearBuildTab;
    structure.frontBuildTabLocationPercent = data.frontBuildTabLocationPercent;
    structure.rearBuildTabLocationPercent = data.rearBuildTabLocationPercent;
    structure.buildTabHeightAboveTable = data.buildTabHeightAboveTable;
    structure.leadingEdgeType = data.leadingEdgeType;
    structure.leadingEdgeTubeOd = data.leadingEdgeTubeOd;
    structure.leadingEdgeTubeId = data.leadingEdgeTubeId;
    structure.trailingEdgeType = data.trailingEdgeType;
    structure.trailingEdgeWidth = data.trailingEdgeWidth;
    structure.trailingEdgeHeight = data.trailingEdgeHeight;
    for (const auto& spar : data.spars)
      structure.spars.push_back({spar.chordLocationPercent, spar.verticalLocation,
          spar.material, spar.type, spar.woodHeight, spar.woodWidth,
          spar.tubeOd, spar.tubeId, spar.rodOd, spar.stripWidth,
          spar.stripThickness, spar.tipChordLocationPercent});
    std::fprintf(stderr, "Saved glider panel %d\n", panelIndex + 1);
    const auto outer = [&] {
      try { return testsupport::buildStructure(ribs, structure); }
      catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        throw;
      }
    }();
    const auto bottomPoint = [](const RibDefinition& rib) {
      const double angle = rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
      const double bottom = untwistedRibBottom(rib);
      return Point2{rib.spanPosition - std::sin(angle) * bottom,
                    rib.dihedralHeight + std::cos(angle) * bottom};
    };
    const auto rootBottom = bottomPoint(ribs.front()), tipBottom = bottomPoint(ribs.back());
    const double planeAngle = std::atan2(tipBottom.y - rootBottom.y, tipBottom.x - rootBottom.x);
    const auto thicknessHeight = [&](const RibDefinition& rib) {
      const double q = std::sin(rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0 - planeAngle);
      return std::min(q * rib.ribThicknessStartFactor * structure.ribThickness,
          q * (rib.ribThicknessStartFactor + 1.0) * structure.ribThickness);
    };
    double plane = std::numeric_limits<double>::max();
    for (const auto& rib : ribs) {
      const auto bottom = bottomPoint(rib);
      plane = std::min(plane, -std::sin(planeAngle) * bottom.x +
          std::cos(planeAngle) * bottom.y + thicknessHeight(rib));
    }
    for (const auto& rib : outer.ribs) {
      int front = 0, rear = 0;
      const auto offset = ribTwistTranslation(rib.rib);
      const double twistAngle = rib.rib.twistDegrees * std::numbers::pi / 180.0;
      const double ribAngle = rib.rib.ribPlaneAngleDegrees * std::numbers::pi / 180.0;
      for (const auto& segment : rib.outlineSegments) {
        if (segment.spline || segment.points.size() != 2) continue;
        const double left = std::min(segment.points[0].x, segment.points[1].x);
        const double right = std::max(segment.points[0].x, segment.points[1].x);
        if (std::abs(right - left - 25.4 * 3.0 / 16.0) > 1.0e-7) continue;
        if (left < rib.rib.chord * 0.5) ++front; else ++rear;
        assert(right < rib.rib.chord - data.trailingEdgeWidth);
        for (const auto point : segment.points) {
          const double z = std::sin(twistAngle) * point.x + std::cos(twistAngle) * point.y + offset.y;
          const double elevation = -std::sin(planeAngle) * rib.rib.spanPosition +
              std::cos(planeAngle) * rib.rib.dihedralHeight + std::cos(ribAngle - planeAngle) * z +
              thicknessHeight(rib.rib);
          assert(std::abs(elevation - plane) < 1.0e-7);
        }
      }
      assert(front == (data.addFrontBuildTab ? 1 : 0) && rear == (data.addRearBuildTab ? 1 : 0));
    }
    // Changing the preceding panels' contribution to the assembly origin
    // must not change this panel's manufacturing contours or build plane.
    for (auto& rib : ribs) {
      rib.spanPosition += 1200.0;
      rib.dihedralHeight += 350.0;
      rib.leadingEdgeOffset += 100.0;
    }
    const auto relocated = testsupport::buildStructure(ribs, structure);
    for (std::size_t i = 0; i < outer.ribs.size(); ++i) {
      const auto& before = outer.ribs[i].outerOutline;
      const auto& after = relocated.ribs[i].outerOutline;
      assert(before.size() == after.size());
      for (std::size_t j = 0; j < before.size(); ++j)
        assert(std::hypot(before[j].x - after[j].x, before[j].y - after[j].y) < 1.0e-7);
    }
  }

    }

    }

  LengthInput thickness{"testThickness", 3.175};
  thickness.setGlobalUnit(DisplayUnit::Inches);
  auto* spin = thickness.findChild<QDoubleSpinBox*>();
  assert(spin != nullptr);
  assert(spin->text() == "1/8 in");
  spin->stepUp();
  assert(spin->text() == "5/32 in");
  assert(std::abs(thickness.valueMm() - 5.0 / 32.0 * 25.4) < 1.0e-8);
  auto* thicknessEdit = spin->findChild<QLineEdit*>();
  assert(thicknessEdit != nullptr);
  thicknessEdit->setText("12mm");
  spin->interpretText();
  assert(std::abs(thickness.valueMm() - 12.0) < 1.0e-8);
  assert(spin->text() == "12.00 mm");
  assert(thickness.unitOverride() == UnitOverride::Millimeters);
  thicknessEdit->setText("1/2 in");
  spin->interpretText();
  assert(std::abs(thickness.valueMm() - 12.7) < 1.0e-8);
  assert(spin->text() == "1/2 in");
  assert(thickness.unitOverride() == UnitOverride::Inches);
  thicknessEdit->setText("0.25\"");
  spin->interpretText();
  assert(std::abs(thickness.valueMm() - 6.35) < 1.0e-8);
  assert(spin->text() == "1/4 in");

  thickness.setValueMm(0.75 * 25.4);
  assert(spin->text() == "3/4 in");
  spin->stepUp();
  assert(spin->text() == "25/32 in");
  thickness.setValueMm(0.76 * 25.4);
  assert(spin->text() == "0.76 in");
  spin->stepUp();
  assert(spin->text() == "25/32 in");
  thickness.setValueMm(1.75 * 25.4);
  assert(spin->text() == "1 3/4 in");
  spin->stepUp();
  assert(spin->text() == "1 25/32 in");
  thickness.setValueMm(1234.0 * 25.4);
  spin->stepUp();
  assert(spin->text() == "1234 1/32 in");

  LengthInput compactMeasurement{"compactMeasurement", 25.4};
  compactMeasurement.setOverrideSelectorVisible(false);
  compactMeasurement.resize(300, compactMeasurement.sizeHint().height());
  compactMeasurement.show();
  QApplication::processEvents();
  auto* compactSpin = compactMeasurement.findChild<QDoubleSpinBox*>();
  assert(compactSpin->width() >=
         QFontMetrics{compactSpin->font()}.horizontalAdvance(QString(9, QChar{'M'})));
  assert(compactSpin->geometry().left() == 0);
  compactMeasurement.hide();

  WingPanelData spacingData;
  spacingData.panelSpan = 700.0;
  spacingData.ribCount = 9;
  WingPanelEditor spacingEditor{spacingData, DisplayUnit::Millimeters};
  spacingEditor.resize(450, 650);
  spacingEditor.show();
  QApplication::processEvents();
  auto* spacing = spacingEditor.findChild<QLabel*>("ribSpacing");
  assert(spacing != nullptr);
  assert(spacing->text() == "Rib Spacing: 87.50 mm");
  assert(spacing->findChild<QDoubleSpinBox*>() == nullptr);
  spacingEditor.findChild<QSpinBox*>("ribCount")->setValue(8);
  assert(spacing->text() == "Rib Spacing: 100.00 mm");
  spacingEditor.setGlobalUnit(DisplayUnit::Inches);
  assert(spacing->text() == "Rib Spacing: 3.93701 in");
  auto* spacingTabs = spacingEditor.findChild<QTabWidget*>();
  assert(spacingTabs != nullptr);
  assert(spacingTabs->tabText(0) == "Specs");
  assert(spacingTabs->tabText(1) == "Ribs");
  assert(spacingTabs->tabText(2) == "Rib Caps");
  assert(spacingTabs->tabText(3) == "Spars");
  assert(spacingTabs->tabText(4) == "Sheeting");
  assert(spacingTabs->tabText(5) == "Joiners");
  const auto belongsTo = [](QWidget* widget, QWidget* ancestor) {
    for (auto* parent = widget; parent; parent = parent->parentWidget())
      if (parent == ancestor) return true;
    return false;
  };
  assert(belongsTo(spacingEditor.findChild<QSpinBox*>("ribCount"),
                   spacingTabs->widget(1)));
  assert(belongsTo(spacingEditor.findChild<LengthInput*>("ribThickness"),
                   spacingTabs->widget(1)));
  assert(belongsTo(spacingEditor.findChild<QCheckBox*>("addRib1a"),
                   spacingTabs->widget(1)));
  auto* ribsContents = spacingEditor.findChild<QWidget*>("ribsContents");
  auto* rib1aOption = spacingEditor.findChild<QCheckBox*>("addRib1a");
  assert(ribsContents != nullptr && rib1aOption != nullptr);
  assert(belongsTo(rib1aOption, ribsContents));
  assert(rib1aOption->text() ==
         "Add Rib 1a (Adds an extra rib between ribs 1 and 2 for extra strength)");
  WingPanelData ribsDefaults = spacingData;
  ribsDefaults.ribThickness = 2.4;
  ribsDefaults.addRib1a = true;
  ribsDefaults.ribLighteningHoles = true;
  ribsDefaults.ribLighteningStartRib = 3;
  ribsDefaults.ribLighteningStopRib = 7;
  ribsDefaults.ribLighteningMinimumWoodMargin = 6.0;
  ribsDefaults.ribLighteningMinimumHoleDistance = 12.0;
  ribsDefaults.leadingEdgeType = 3;
  ribsDefaults.spars = {
      {30, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0, 3.0}};
  ribsDefaults.riblets = true;
  ribsDefaults.ribletStartRib = 2;
  ribsDefaults.ribletEndRib = 8;
  ribsDefaults.ribletsPerBay = 3;
  WingPanelEditor defaultsRibsEditor{
      ribsDefaults, DisplayUnit::Millimeters, true, true, true};
  auto* defaultsTabs = defaultsRibsEditor.findChild<QTabWidget*>();
  auto* defaultsRibsContents =
      defaultsRibsEditor.findChild<QWidget*>("ribsContents");
  assert(defaultsTabs != nullptr && defaultsTabs->tabText(1) == "Ribs");
  assert(defaultsRibsContents != nullptr);
  assert(belongsTo(defaultsRibsEditor.findChild<QSpinBox*>("ribCount"),
                   defaultsTabs->widget(1)));
  assert(belongsTo(defaultsRibsEditor.findChild<LengthInput*>("ribThickness"),
                   defaultsTabs->widget(1)));
  assert(belongsTo(defaultsRibsEditor.findChild<QCheckBox*>("addRib1a"),
                   defaultsTabs->widget(1)));
  auto* ribLighteningOption =
      defaultsRibsEditor.findChild<QCheckBox*>("ribLighteningHoles");
  auto* ribLighteningStart =
      defaultsRibsEditor.findChild<QSpinBox*>("ribLighteningStartRib");
  auto* ribLighteningStop =
      defaultsRibsEditor.findChild<QSpinBox*>("ribLighteningStopRib");
  auto* ribLighteningBorder = defaultsRibsEditor.findChild<LengthInput*>(
      "ribLighteningMinimumWoodMargin");
  auto* ribLighteningDistance = defaultsRibsEditor.findChild<LengthInput*>(
      "ribLighteningMinimumHoleDistance");
  assert(ribLighteningOption && ribLighteningStart && ribLighteningStop);
  assert(ribLighteningBorder && ribLighteningDistance);
  auto* ribletsOption =
      defaultsRibsEditor.findChild<QCheckBox*>("riblets");
  auto* ribletStart =
      defaultsRibsEditor.findChild<QSpinBox*>("ribletStartRib");
  auto* ribletEnd =
      defaultsRibsEditor.findChild<QSpinBox*>("ribletEndRib");
  auto* ribletsPerBay =
      defaultsRibsEditor.findChild<QSpinBox*>("ribletsPerBay");
  assert(ribletsOption && ribletStart && ribletEnd && ribletsPerBay);
  assert(ribletsOption->isEnabled() && ribletsOption->isChecked());
  assert(ribletsOption->text() ==
      "Riblets (Riblets require CF LE and mid Spar)");
  assert(ribletStart->minimum() == 2);
  assert(ribletEnd->maximum() == ribsDefaults.ribCount);
  assert(belongsTo(ribLighteningOption, defaultsRibsContents));
  assert(belongsTo(ribletsOption, defaultsTabs->widget(1)));
  assert(ribLighteningOption->text() ==
      "Lightening Holes (Add this option last due to longer wing generation times)");
  bool foundMinimumHoleDistance = false;
  for (const auto* label : defaultsRibsEditor.findChildren<QLabel*>())
    foundMinimumHoleDistance =
        foundMinimumHoleDistance || label->text() == "Min Hole Distance";
  assert(foundMinimumHoleDistance);
  defaultsTabs->setCurrentIndex(1);
  defaultsRibsEditor.resize(800, 900);
  defaultsRibsEditor.show();
  QApplication::processEvents();
  const auto ribOptionY = [&](const QWidget* widget) {
    return widget->mapTo(&defaultsRibsEditor, QPoint{}).y();
  };
  assert(ribOptionY(ribLighteningStart) < ribOptionY(ribLighteningStop));
  assert(ribOptionY(ribLighteningStop) < ribOptionY(ribLighteningBorder));
  assert(ribOptionY(ribLighteningBorder) <
         ribOptionY(ribLighteningDistance));
  defaultsRibsEditor.hide();
  const auto savedRibDefaults = defaultsRibsEditor.data();
  assert(savedRibDefaults.ribCount == ribsDefaults.ribCount);
  assert(std::abs(savedRibDefaults.ribThickness - ribsDefaults.ribThickness) <
         1.0e-8);
  assert(savedRibDefaults.addRib1a);
  assert(savedRibDefaults.ribLighteningHoles);
  assert(savedRibDefaults.ribLighteningStartRib == 3);
  assert(savedRibDefaults.ribLighteningStopRib == 7);
  assert(std::abs(savedRibDefaults.ribLighteningMinimumWoodMargin - 6.0) <
         1.0e-8);
  assert(std::abs(savedRibDefaults.ribLighteningMinimumHoleDistance - 12.0) <
         1.0e-8);
  assert(savedRibDefaults.riblets);
  assert(savedRibDefaults.ribletStartRib == 2);
  assert(savedRibDefaults.ribletEndRib == 8);
  assert(savedRibDefaults.ribletsPerBay == 3);
  const auto restoredRibletDefaults =
      panelDataFromJson(panelDataToJson(savedRibDefaults));
  assert(restoredRibletDefaults.riblets);
  assert(restoredRibletDefaults.ribletStartRib == 2);
  assert(restoredRibletDefaults.ribletEndRib == 8);
  assert(restoredRibletDefaults.ribletsPerBay == 3);
  const auto installedMetricRibs =
      installedDefaultPanelData(DisplayUnit::Millimeters);
  const auto installedInchRibs =
      installedDefaultPanelData(DisplayUnit::Inches);
  assert(installedMetricRibs.ribLighteningStartRib == 3);
  assert(installedMetricRibs.ribLighteningStopRib ==
         installedMetricRibs.ribCount - 1);
  assert(installedInchRibs.ribLighteningStopRib ==
         installedInchRibs.ribCount - 1);
  assert(installedMetricRibs.ribletStartRib == 2);
  assert(installedMetricRibs.ribletEndRib ==
         installedMetricRibs.ribCount);
  assert(installedInchRibs.ribletEndRib ==
         installedInchRibs.ribCount);
  auto outerRibletDefaults = ribsDefaults;
  outerRibletDefaults.ribletStartRib = 1;
  outerRibletDefaults.ribletEndRib = outerRibletDefaults.ribCount;
  WingPanelEditor outerRibletEditor{
      outerRibletDefaults, DisplayUnit::Millimeters,
      true, true, false};
  auto* outerRibletStart =
      outerRibletEditor.findChild<QSpinBox*>("ribletStartRib");
  auto* outerRibletEnd =
      outerRibletEditor.findChild<QSpinBox*>("ribletEndRib");
  assert(outerRibletStart && outerRibletEnd);
  assert(outerRibletStart->minimum() == 1);
  assert(outerRibletStart->value() == 1);
  assert(outerRibletEnd->value() == outerRibletDefaults.ribCount);
  auto* panelSpanInput = spacingEditor.findChild<LengthInput*>("panelSpan");
  auto* panelSpanSpin = panelSpanInput->findChild<QDoubleSpinBox*>();
  auto* dihedralSpin = spacingEditor.findChild<QDoubleSpinBox*>("dihedral");
  auto* twistSpin = spacingEditor.findChild<QDoubleSpinBox*>("twist");
  assert(dihedralSpin != nullptr && twistSpin != nullptr);
  auto* buildTabs = spacingEditor.findChild<QCheckBox*>("addFrontBuildTab");
  assert(buildTabs && !buildTabs->isChecked() && buildTabs->isEnabled());
  assert(buildTabs->text() == "Add Bottom Front Build Tabs");
  buildTabs->setChecked(true);
  const auto flatTabData = panelDataFromJson(panelDataToJson(spacingEditor.data()));
  assert(flatTabData.twist == 0.0 && flatTabData.addFrontBuildTab);
  WingPanelEditor flatTabEditor{flatTabData, DisplayUnit::Millimeters};
  assert(flatTabEditor.data().addFrontBuildTab);
  twistSpin->setValue(3.0);
  assert(buildTabs->isEnabled());
  buildTabs->setChecked(true);
  assert(panelDataFromJson(panelDataToJson(spacingEditor.data())).addFrontBuildTab);
  twistSpin->setValue(-3.0);
  assert(buildTabs->isEnabled());
  twistSpin->setValue(0.0);
  assert(buildTabs->isEnabled());
  assert(buildTabs->isChecked());
  assert(spacingEditor.data().addFrontBuildTab);
  twistSpin->setValue(3.0);
  assert(buildTabs->isEnabled() && buildTabs->isChecked());
  twistSpin->setValue(0.0);
  auto* defaultBuildTabs = defaultsRibsEditor.findChild<QCheckBox*>("addFrontBuildTab");
  assert(defaultBuildTabs && defaultBuildTabs->isEnabled() && !defaultBuildTabs->isChecked());
  defaultsRibsEditor.findChild<QDoubleSpinBox*>("twist")->setValue(2.0);
  defaultBuildTabs->setChecked(true);
  assert(panelDataFromJson(panelDataToJson(defaultsRibsEditor.data())).addFrontBuildTab);
  defaultsRibsEditor.findChild<QDoubleSpinBox*>("twist")->setValue(0.0);
  assert(defaultBuildTabs->isEnabled() && defaultBuildTabs->isChecked());
  assert(defaultsRibsEditor.data().addFrontBuildTab);
  assert(!installedDefaultPanelData(DisplayUnit::Inches).addFrontBuildTab);
  assert(!installedDefaultPanelData(DisplayUnit::Millimeters).addFrontBuildTab);
  // Old project/default JSON migrates both tabs; explicit new flags win.
  for (const bool legacyEnabled : {false, true}) {
    const auto migrated = panelDataFromJson(QJsonObject{{"addBuildTabs", legacyEnabled}});
    assert(migrated.addFrontBuildTab == legacyEnabled && migrated.addRearBuildTab == legacyEnabled);
    assert(migrated.frontBuildTabLocationPercent == 15.0);
    assert(migrated.rearBuildTabLocationPercent == 75.0);
    assert(migrated.buildTabHeightAboveTable == 0.0);
    assert(migrated.buildTabWidth == 4.7625);
    assert(!migrated.addTopFrontBuildTab && !migrated.addTopRearBuildTab);
    assert(!migrated.addTabsToSheetedRibs);
    const auto restored = panelDataFromJson(panelDataToJson(migrated));
    assert(restored.addFrontBuildTab == legacyEnabled && restored.addRearBuildTab == legacyEnabled);
  }
  const auto mixed = panelDataFromJson(QJsonObject{{"addBuildTabs", true}, {"addFrontBuildTab", false}});
  assert(!mixed.addFrontBuildTab && mixed.addRearBuildTab);
  const auto empty = panelDataFromJson(QJsonObject{});
  assert(!empty.addFrontBuildTab && !empty.addRearBuildTab);
  assert(empty.moldedLeadingEdgeWidth == 15.875 && empty.moldedLeadingEdgeThickness == 1.5875);
  assert(empty.notchedLeadingEdgeWidth == 9.525 && empty.notchedLeadingEdgeHeight == 4.7625);
  for (const auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    WingPanelEditor leEditor{installedDefaultPanelData(unit), unit, true, true, true};
    auto* molded = leEditor.findChild<QRadioButton*>("moldedLe");
    auto* notched = leEditor.findChild<QRadioButton*>("notchedLe");
    auto* moldedWidth = leEditor.findChild<LengthInput*>("moldedLeadingEdgeWidth");
    auto* moldedThickness = leEditor.findChild<LengthInput*>("moldedLeadingEdgeThickness");
    auto* notchedWidth = leEditor.findChild<LengthInput*>("notchedLeadingEdgeWidth");
    auto* notchedHeight = leEditor.findChild<LengthInput*>("notchedLeadingEdgeHeight");
    assert(molded && notched && moldedWidth && moldedThickness && notchedWidth && notchedHeight);
    assert(moldedWidth->valueMm() == 15.875 && moldedThickness->valueMm() == 1.5875);
    assert(notchedWidth->valueMm() == 9.525 && notchedHeight->valueMm() == 4.7625);
    molded->setChecked(true);
    assert(!notched->isChecked() && leEditor.data().leadingEdgeType == 6);
    assert(!leEditor.findChild<QWidget*>("moldedLeDetails")->isHidden());
    assert(leEditor.findChild<QWidget*>("notchedLeDetails")->isHidden());
    for (auto* input : {moldedWidth, moldedThickness, notchedWidth, notchedHeight}) {
      auto* spin = input->findChild<QDoubleSpinBox*>();
      spin->findChild<QLineEdit*>()->setText(unit == DisplayUnit::Millimeters ? "1/4 in" : "6.35mm");
      spin->interpretText();
      assert(std::abs(input->valueMm() - 6.35) < 1.0e-8);
    }
    auto saved = panelDataFromJson(panelDataToJson(leEditor.data()));
    assert(saved.leadingEdgeType == 6 && std::abs(saved.moldedLeadingEdgeWidth - 6.35) < 1.0e-8);
    WingPanelEditor restoredMolded{saved, unit};
    assert(restoredMolded.findChild<QRadioButton*>("moldedLe")->isChecked());
    notched->setChecked(true);
    assert(!molded->isChecked() && leEditor.findChild<QWidget*>("moldedLeDetails")->isHidden());
    assert(!leEditor.findChild<QWidget*>("notchedLeDetails")->isHidden());
    saved = panelDataFromJson(panelDataToJson(leEditor.data()));
    assert(saved.leadingEdgeType == 7 && std::abs(saved.notchedLeadingEdgeHeight - 6.35) < 1.0e-8);
    WingPanelEditor restoredNotched{saved, unit};
    assert(restoredNotched.findChild<QRadioButton*>("notchedLe")->isChecked());
  }
  for (const auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    WingPanelEditor diamondEditor{installedDefaultPanelData(unit), unit, true, true, true};
    auto* diamond = diamondEditor.findChild<QRadioButton*>("diamondLe");
    auto* width = diamondEditor.findChild<LengthInput*>("diamondLeadingEdgeWidth");
    auto* details = diamondEditor.findChild<QWidget*>("diamondLeDetails");
    assert(diamond && width && details && details->isHidden());
    diamond->setChecked(true);
    assert(!details->isHidden() && diamondEditor.data().leadingEdgeType == 5);
    auto* spin = width->findChild<QDoubleSpinBox*>();
    spin->findChild<QLineEdit*>()->setText(unit == DisplayUnit::Millimeters ? "1/2 in" : "12.7mm");
    spin->interpretText();
    const auto saved = panelDataFromJson(panelDataToJson(diamondEditor.data()));
    assert(saved.leadingEdgeType == 5 && std::abs(saved.diamondLeadingEdgeWidth - 12.7) < 1.0e-8);
    WingPanelEditor restored{saved, unit};
    assert(restored.findChild<QRadioButton*>("diamondLe")->isChecked());
    assert(restored.data().unitOverrides.value("diamondLeadingEdgeWidth") ==
        saved.unitOverrides.value("diamondLeadingEdgeWidth"));
    restored.setDiamondLeadingEdgeWidthMm(9.87);
    assert(std::abs(restored.data().diamondLeadingEdgeWidth - 9.87) < 1.0e-8);
    assert(restored.data().unitOverrides.value("diamondLeadingEdgeWidth") ==
        saved.unitOverrides.value("diamondLeadingEdgeWidth"));
  }
  for (const auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    WingPanelEditor tabEditor{installedDefaultPanelData(unit), unit, true, true, true};
    auto* front = tabEditor.findChild<QCheckBox*>("addFrontBuildTab");
    auto* rear = tabEditor.findChild<QCheckBox*>("addRearBuildTab");
    auto* frontDetails = tabEditor.findChild<QWidget*>("frontBuildTabDetails");
    auto* rearDetails = tabEditor.findChild<QWidget*>("rearBuildTabDetails");
    auto* heightDetails = tabEditor.findChild<QWidget*>("buildTabHeightDetails");
    auto* height = tabEditor.findChild<LengthInput*>("buildTabHeightAboveTable");
    auto* width = tabEditor.findChild<LengthInput*>("buildTabWidth");
    auto* widthDetails = tabEditor.findChild<QWidget*>("buildTabWidthDetails");
    assert(width && width->valueMm() == 4.7625 && widthDetails->isHidden());
    assert(width->unitOverride() == UnitOverride::Global);
    auto* separation = tabEditor.findChild<QCheckBox*>("addTabRibSeparationLine");
    auto* separationDetails = tabEditor.findChild<QWidget*>("tabSeparationDetails");
    assert(separation && !separation->isChecked() && separationDetails->isHidden());
    assert(front && rear && height && !front->isChecked() && !rear->isChecked());
    assert(frontDetails->isHidden() && rearDetails->isHidden() && heightDetails->isHidden());
    assert(height->valueMm() == 0.0 && height->unitOverride() == UnitOverride::Global);
    rear->setChecked(true);
    assert(!widthDetails->isHidden());
    assert(!separationDetails->isHidden());
    separation->setChecked(true);
    assert(frontDetails->isHidden() && !rearDetails->isHidden() && !heightDetails->isHidden());
    front->setChecked(true);
    rear->setChecked(false);
    assert(!frontDetails->isHidden() && rearDetails->isHidden() && !heightDetails->isHidden());
    tabEditor.findChild<QDoubleSpinBox*>("frontBuildTabLocationPercent")->setValue(22.25);
    tabEditor.findChild<QDoubleSpinBox*>("rearBuildTabLocationPercent")->setValue(68.5);
    auto* heightSpin = height->findChild<QDoubleSpinBox*>();
    heightSpin->findChild<QLineEdit*>()->setText(unit == DisplayUnit::Millimeters ? "1/2 in" : "12.7mm");
    heightSpin->interpretText();
    auto* widthSpin = width->findChild<QDoubleSpinBox*>();
    widthSpin->findChild<QLineEdit*>()->setText(unit == DisplayUnit::Millimeters ? "1/4 in" : "6.35mm");
    widthSpin->interpretText();
    const auto saved = panelDataFromJson(panelDataToJson(tabEditor.data()));
    assert(saved.addFrontBuildTab && !saved.addRearBuildTab);
    assert(saved.addTabRibSeparationLine);
    assert(std::abs(saved.buildTabWidth - 6.35) < 1.0e-8);
    assert(saved.frontBuildTabLocationPercent == 22.25 && saved.rearBuildTabLocationPercent == 68.5);
    assert(std::abs(saved.buildTabHeightAboveTable - 12.7) < 1.0e-8);
    WingPanelEditor restored{saved, unit};
    assert(std::abs(restored.data().buildTabHeightAboveTable - 12.7) < 1.0e-8);
    assert(restored.data().addTabRibSeparationLine);
    assert(std::abs(restored.data().buildTabWidth - 6.35) < 1.0e-8);
    assert(restored.data().unitOverrides.value("buildTabWidth") == saved.unitOverrides.value("buildTabWidth"));
    assert(restored.data().unitOverrides.value("buildTabHeightAboveTable") ==
           saved.unitOverrides.value("buildTabHeightAboveTable"));
    front->setChecked(false);
    assert(heightDetails->isHidden());
    assert(separationDetails->isHidden());
    assert(widthDetails->isHidden());
    auto* topFront = tabEditor.findChild<QCheckBox*>("addTopFrontBuildTab");
    auto* topRear = tabEditor.findChild<QCheckBox*>("addTopRearBuildTab");
    auto* sheeted = tabEditor.findChild<QCheckBox*>("addTabsToSheetedRibs");
    assert(topFront && topRear && sheeted && sheeted->isHidden());
    assert(sheeted->text() == "Add Tabs to Ribs With Sheeting (Otherwise no tabs where sheeting)");
    assert(topFront->text() == "Add Top Front Build Tabs");
    assert(topRear->text() == "Add Top Rear Build Tabs");
    assert(rear->text() == "Add Bottom Rear Build Tabs");
    topFront->setChecked(true);
    assert(!heightDetails->isHidden() && !widthDetails->isHidden() && !sheeted->isHidden());
    assert(!tabEditor.findChild<QWidget*>("topFrontBuildTabDetails")->isHidden());
    assert(tabEditor.findChild<QWidget*>("topRearBuildTabDetails")->isHidden());
    topRear->setChecked(true);
    sheeted->setChecked(true);
    tabEditor.findChild<QDoubleSpinBox*>("topFrontBuildTabLocationPercent")->setValue(19.5);
    tabEditor.findChild<QDoubleSpinBox*>("topRearBuildTabLocationPercent")->setValue(71.5);
    const auto topSaved = panelDataFromJson(panelDataToJson(tabEditor.data()));
    WingPanelEditor topRestored{topSaved, unit, true, true, true};
    assert(topRestored.data().addTopFrontBuildTab && topRestored.data().addTopRearBuildTab);
    assert(topRestored.data().addTabsToSheetedRibs);
    assert(!topRestored.data().addFrontBuildTab && !topRestored.data().addRearBuildTab);
    assert(topRestored.data().topFrontBuildTabLocationPercent == 19.5);
    assert(topRestored.data().topRearBuildTabLocationPercent == 71.5);
  }
  assert(dihedralSpin->width() == twistSpin->width());
  assert(dihedralSpin->width() == panelSpanSpin->width());
  assert(spacingEditor.findChild<QSpinBox*>("ribCount")->width() ==
         panelSpanSpin->width());
  assert(dihedralSpin->mapTo(&spacingEditor, QPoint{}).x() ==
         panelSpanSpin->mapTo(&spacingEditor, QPoint{}).x());
  spacingEditor.hide();
  WingPanelData spoilerDefaults;
  spoilerDefaults.spoilers = true;
  spoilerDefaults.spoilerChordLocationPercent = 30.25;
  spoilerDefaults.spoilerLighteningHoles = true;
  spoilerDefaults.spoilerImmediatelyBehindSpar = true;
  spoilerDefaults.spars = {
      {25, 0, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  spoilerDefaults.spoilerMinimumWoodMargin = 6.0;
  spoilerDefaults.spoilerMinimumCircleDistance = 12.0;
  const auto restoredSpoilers = panelDataFromJson(panelDataToJson(spoilerDefaults));
  assert(restoredSpoilers.spoilers);
  assert(restoredSpoilers.spoilerLighteningHoles);
  assert(restoredSpoilers.spoilerImmediatelyBehindSpar);
  assert(std::abs(restoredSpoilers.spoilerMinimumWoodMargin - 6.0) <
         1.0e-8);
  assert(std::abs(restoredSpoilers.spoilerMinimumCircleDistance - 12.0) <
         1.0e-8);
  assert(restoredSpoilers.spoilerStartRib == 3 && restoredSpoilers.spoilerEndRib == 7);
  assert(std::abs(restoredSpoilers.spoilerChordLocationPercent - 30.25) <
         1.0e-8);
  WingPanelData dihedralSpoilerData = spoilerDefaults;
  dihedralSpoilerData.dihedral = 5.0;
  WingPanelEditor dihedralSpoilerEditor{dihedralSpoilerData};
  auto* spoilersOption =
      dihedralSpoilerEditor.findChild<QCheckBox*>("spoilers");
  assert(spoilersOption != nullptr);
  assert(spoilersOption->text() ==
         "Spoilers (Center spoiler requires 0 dihedral)");
  auto* lighteningHoles =
      dihedralSpoilerEditor.findChild<QCheckBox*>("spoilerLighteningHoles");
  auto* minimumWoodMargin =
      dihedralSpoilerEditor.findChild<LengthInput*>(
          "spoilerMinimumWoodMargin");
  auto* minimumCircleDistance =
      dihedralSpoilerEditor.findChild<LengthInput*>(
          "spoilerMinimumCircleDistance");
  assert(lighteningHoles != nullptr && lighteningHoles->isChecked());
  assert(minimumWoodMargin != nullptr && minimumCircleDistance != nullptr);
  assert(std::abs(minimumWoodMargin->valueMm() - 6.0) < 1.0e-8);
  assert(std::abs(minimumCircleDistance->valueMm() - 12.0) < 1.0e-8);
  bool foundMinBorderLabel = false;
  bool foundMinCircleLabel = false;
  bool foundSpoilerThicknessLabel = false;
  bool foundOldSpoilerHeightLabel = false;
  for (const auto* label : dihedralSpoilerEditor.findChildren<QLabel*>()) {
    foundMinBorderLabel =
        foundMinBorderLabel || label->text() == "Min Border Distance";
    foundMinCircleLabel =
        foundMinCircleLabel || label->text() == "Min Circle Distance";
    foundSpoilerThicknessLabel =
        foundSpoilerThicknessLabel || label->text() == "Spoiler Thickness";
    foundOldSpoilerHeightLabel = foundOldSpoilerHeightLabel ||
        label->text() == "Spoiler Vertical Height";
  }
  assert(foundMinBorderLabel && foundMinCircleLabel);
  assert(foundSpoilerThicknessLabel && !foundOldSpoilerHeightLabel);
  auto* dihedralSpoilerStart =
      dihedralSpoilerEditor.findChild<QSpinBox*>("spoilerStartRib");
  assert(dihedralSpoilerStart != nullptr);
  auto* spoilerTabs = dihedralSpoilerEditor.findChild<QTabWidget*>();
  assert(spoilerTabs != nullptr);
  int spoilersTabIndex = -1;
  for (int index = 0; index < spoilerTabs->count(); ++index)
    if (spoilerTabs->tabText(index) == "Spoilers")
      spoilersTabIndex = index;
  assert(spoilersTabIndex >= 0);
  spoilerTabs->setCurrentIndex(spoilersTabIndex);
  auto* spoilerEnd =
      dihedralSpoilerEditor.findChild<QSpinBox*>("spoilerEndRib");
  auto* spoilerChordLocationWidget =
      dihedralSpoilerEditor.findChild<QDoubleSpinBox*>(
      "spoilerChordLocationPercent");
  auto* immediatelyBehindSpar =
      dihedralSpoilerEditor.findChild<QCheckBox*>(
          "spoilerImmediatelyBehindSpar");
  auto* spoilerWidth =
      dihedralSpoilerEditor.findChild<LengthInput*>("spoilerWidth");
  auto* spoilerThickness =
      dihedralSpoilerEditor.findChild<LengthInput*>("spoilerThickness");
  auto* spoilerFrameRail =
      dihedralSpoilerEditor.findChild<LengthInput*>("spoilerFrameRailWidth");
  auto* spoilerSupportRail =
      dihedralSpoilerEditor.findChild<QLabel*>("spoilerSupportRailHeight");
  auto* spoilerWiring =
      dihedralSpoilerEditor.findChild<QCheckBox*>("wiringHolesSpoilers");
  auto* spoilerWiringStart =
      dihedralSpoilerEditor.findChild<QSpinBox*>("wiringHoleStartRibSpoilers");
  auto* spoilerWiringEnd =
      dihedralSpoilerEditor.findChild<QSpinBox*>("wiringHoleEndRibSpoilers");
  auto* spoilerWiringChord = dihedralSpoilerEditor.findChild<QDoubleSpinBox*>(
      "wiringHoleChordLocationPercentSpoilers");
  auto* spoilerWiringWidth = dihedralSpoilerEditor.findChild<LengthInput*>(
      "wiringHoleWidthSpoilers");
  auto* spoilerWiringHeight = dihedralSpoilerEditor.findChild<LengthInput*>(
      "wiringHoleHeightSpoilers");
  assert(spoilerEnd && spoilerChordLocationWidget && immediatelyBehindSpar &&
         spoilerWidth && spoilerThickness);
  assert(spoilerFrameRail && spoilerSupportRail && spoilerWiring);
  assert(spoilerWiringStart && spoilerWiringEnd && spoilerWiringChord);
  assert(spoilerWiringWidth && spoilerWiringHeight);
  assert(spoilerChordLocationWidget->decimals() == 2);
  assert(std::abs(spoilerChordLocationWidget->singleStep() - 1.0) < 1.0e-8);
  spoilerChordLocationWidget->setValue(37.25);
  assert(std::abs(dihedralSpoilerEditor.data().spoilerChordLocationPercent -
                  37.25) < 1.0e-8);
  spoilerWiring->setChecked(true);
  dihedralSpoilerEditor.resize(800, 900);
  dihedralSpoilerEditor.show();
  QApplication::processEvents();
  assert(immediatelyBehindSpar->isVisible());
  assert(immediatelyBehindSpar->text() == "Immediately Behind Spar");
  assert(immediatelyBehindSpar->isChecked());
  const auto verticalPosition = [&](const QWidget* widget) {
    return widget->mapTo(&dihedralSpoilerEditor, QPoint{}).y();
  };
  assert(verticalPosition(dihedralSpoilerStart) <
         verticalPosition(spoilerEnd));
  assert(verticalPosition(spoilerEnd) <
         verticalPosition(spoilerChordLocationWidget));
  assert(verticalPosition(spoilerWidth) <
         verticalPosition(spoilerThickness));
  assert(verticalPosition(spoilerFrameRail) <
         verticalPosition(spoilerSupportRail));
  assert(verticalPosition(minimumWoodMargin) <
         verticalPosition(minimumCircleDistance));
  assert(verticalPosition(spoilerWiringStart) <
         verticalPosition(spoilerWiringEnd));
  assert(verticalPosition(spoilerWiringEnd) <
         verticalPosition(spoilerWiringChord));
  assert(verticalPosition(spoilerWiringWidth) <
         verticalPosition(spoilerWiringHeight));
  dihedralSpoilerEditor.hide();
  assert(dihedralSpoilerStart->minimum() == 1);
  dihedralSpoilerStart->setValue(1);
  QString centerSpoilerError;
  assert(!dihedralSpoilerEditor.validate(centerSpoilerError));
  assert(centerSpoilerError ==
         "Dihedral must be 0 degrees for a center spoiler.");
  dihedralSpoilerStart->setValue(2);
  QString dihedralSpoilerError;
  assert(dihedralSpoilerEditor.validate(dihedralSpoilerError));
  WingPanelData flatSpoilerData = spoilerDefaults;
  flatSpoilerData.dihedral = 0.0;
  WingPanelEditor flatSpoilerEditor{flatSpoilerData};
  assert(flatSpoilerEditor.findChild<QSpinBox*>("spoilerStartRib")->minimum() == 1);

  WingPanelData wiringDefaults;
  wiringDefaults.wiringHoles = true;
  wiringDefaults.addRib1a = true;
  wiringDefaults.wiringHoleEndRib = wiringDefaults.ribCount + 1;
  const auto restoredWiring = panelDataFromJson(panelDataToJson(wiringDefaults));
  assert(restoredWiring.wiringHoles);
  assert(restoredWiring.wiringHoleStartRib == 2);
  assert(restoredWiring.wiringHoleEndRib == 10);
  wiringDefaults.wiringHoleChordLocationPercent = 50.5;
  const auto fractionalWiring =
      panelDataFromJson(panelDataToJson(wiringDefaults));
  assert(std::abs(fractionalWiring.wiringHoleChordLocationPercent - 50.5) <
         1.0e-8);
  assert(std::abs(restoredWiring.wiringHoleWidth / 25.4 - 3.0 / 8.0) < 1.0e-8);
  assert(std::abs(restoredWiring.wiringHoleHeight / 25.4 - 1.0 / 4.0) < 1.0e-8);
  WingPanelEditor wiringEditor{wiringDefaults, DisplayUnit::Inches};
  auto* controlsWiring = wiringEditor.findChild<QCheckBox*>("wiringHolesControls");
  auto* spoilersWiring = wiringEditor.findChild<QCheckBox*>("wiringHolesSpoilers");
  auto* controlsStart = wiringEditor.findChild<QSpinBox*>("wiringHoleStartRibControls");
  auto* controlsEnd = wiringEditor.findChild<QSpinBox*>("wiringHoleEndRibControls");
  auto* spoilerChord =
      wiringEditor.findChild<QDoubleSpinBox*>(
          "wiringHoleChordLocationPercentSpoilers");
  assert(controlsWiring && spoilersWiring && controlsWiring->isChecked() &&
         spoilersWiring->isChecked());
  assert(controlsStart && controlsStart->text() == "R1a");
  assert(controlsEnd && controlsEnd->text() == "R9");
  spoilerChord->setValue(63.75);
  assert(std::abs(wiringEditor.findChild<QDoubleSpinBox*>(
             "wiringHoleChordLocationPercentControls")->value() - 63.75) <
         1.0e-8);
  assert(wiringEditor.findChild<LengthInput*>("wiringHoleWidthControls")
             ->findChild<QDoubleSpinBox*>()->text() == "3/8 in");
  assert(wiringEditor.findChild<LengthInput*>("wiringHoleHeightControls")
             ->findChild<QDoubleSpinBox*>()->text() == "1/4 in");

  WingPanelEditor sparEditor{WingPanelData{}, DisplayUnit::Millimeters};
  sparEditor.resize(600, 800);
  auto* sparTabs = sparEditor.findChild<QTabWidget*>();
  assert(sparTabs != nullptr);
  assert(sparTabs->tabText(1) == "Ribs");
  assert(sparTabs->tabText(2) == "Rib Caps");
  assert(sparTabs->tabText(3) == "Spars");
  assert(sparTabs->tabText(4) == "Sheeting");
  sparTabs->setCurrentIndex(3);
  sparEditor.show();
  QApplication::processEvents();
  auto sparRows = sparEditor.findChildren<QWidget*>("sparEditorRow");
  assert(sparRows.size() == 1);
  auto* firstRootChord =
      sparRows[0]->findChild<QDoubleSpinBox*>("sparRootChordLocation");
  auto* firstTipChord =
      sparRows[0]->findChild<QDoubleSpinBox*>("sparTipChordLocation");
  assert(firstRootChord != nullptr && firstTipChord != nullptr &&
         firstRootChord->minimum() == 0 && firstRootChord->maximum() == 90 &&
         firstRootChord->value() == 25 && firstTipChord->value() == 25);
  assert(firstRootChord->decimals() == 2 && firstTipChord->decimals() == 2 &&
         std::abs(firstRootChord->singleStep() - 1.0) < 1.0e-8 &&
         std::abs(firstTipChord->singleStep() - 1.0) < 1.0e-8);
  firstRootChord->setValue(25.5);
  firstTipChord->setValue(30.75);
  assert(std::abs(sparEditor.data().spars.front().chordLocationPercent - 25.5) <
         1.0e-8);
  assert(std::abs(sparEditor.data().spars.front().tipChordLocationPercent -
                  30.75) < 1.0e-8);
  firstRootChord->setValue(25.0);
  firstTipChord->setValue(25.0);
  const auto radioWithText = [](QWidget* parent, const QString& text) {
    for (auto* radio : parent->findChildren<QRadioButton*>())
      if (radio->text() == text) return radio;
    return static_cast<QRadioButton*>(nullptr);
  };
  assert(radioWithText(sparRows[0], "Mid")->isChecked());
  assert(radioWithText(sparRows[0], "CF")->isChecked());
  assert(radioWithText(sparRows[0], "Aluminum") == nullptr);
  assert(radioWithText(sparRows[0], "Tube")->isChecked());
  auto* firstWoodHeight = sparRows[0]->findChild<LengthInput*>("sparEditor1WoodHeight");
  auto* firstTubeOd = sparRows[0]->findChild<LengthInput*>("sparEditor1TubeOd");
  assert(firstWoodHeight != nullptr && !firstWoodHeight->isVisible());
  assert(firstTubeOd != nullptr && firstTubeOd->isVisible());

  auto* addSpar = sparEditor.findChild<QPushButton*>("addSparButton");
  auto* deleteSpar = sparEditor.findChild<QPushButton*>("deleteSparButton");
  assert(addSpar != nullptr && deleteSpar != nullptr);
  addSpar->click();
  QApplication::processEvents();
  sparRows = sparEditor.findChildren<QWidget*>("sparEditorRow");
  assert(sparRows.size() == 2);
  radioWithText(sparRows[0], "Wood")->click();
  radioWithText(sparRows[0], "Top")->click();
  radioWithText(sparRows[1], "Wood")->click();
  radioWithText(sparRows[1], "Bottom")->click();
  QApplication::processEvents();
  auto* shearWebs = sparEditor.findChild<QCheckBox*>("sparShearWebs");
  auto* shearThickness = sparEditor.findChild<LengthInput*>("sparShearWebThickness");
  assert(shearWebs != nullptr && shearWebs->isVisible());
  shearWebs->click();
  assert(shearThickness != nullptr && shearThickness->isVisible());
  sparRows[0]->findChild<QCheckBox*>("sparDeleteSelection")->setChecked(true);
  deleteSpar->click();
  QApplication::processEvents();
  sparRows = sparEditor.findChildren<QWidget*>("sparEditorRow");
  assert(sparRows.size() == 1);
  assert(sparRows[0]->findChild<QCheckBox*>("sparDeleteSelection")->text() == "Spar 1");
  const auto savedPanelSpars = sparEditor.data();
  assert(savedPanelSpars.spars.size() == 1);
  assert(savedPanelSpars.spars.front().material == 0);
  assert(savedPanelSpars.spars.front().verticalLocation == 1);
  const auto restoredPanelSpars = panelDataFromJson(panelDataToJson(savedPanelSpars));
  assert(restoredPanelSpars.spars.size() == 1);
  assert(restoredPanelSpars.spars.front().verticalLocation == 1);
  auto intermediateSparJson = panelDataToJson(savedPanelSpars);
  intermediateSparJson.remove("spars");
  const auto migratedIntermediateSpars = panelDataFromJson(intermediateSparJson);
  assert(migratedIntermediateSpars.spars.size() == 1);
  assert(migratedIntermediateSpars.spars.front().chordLocationPercent ==
         migratedIntermediateSpars.sparDefaults.chordLocationPercent);
  assert(migratedIntermediateSpars.spars.front().tipChordLocationPercent ==
         migratedIntermediateSpars.sparDefaults.tipChordLocationPercent);
  WingPanelEditor migratedIntermediateEditor{migratedIntermediateSpars};
  assert(migratedIntermediateEditor.findChildren<QWidget*>("sparEditorRow").size() == 1);
  assert(shearWebs->isHidden() && !shearWebs->isChecked());
  sparTabs->setCurrentIndex(4);
  QApplication::processEvents();
  auto* leTopSheetCheck = [&sparEditor]() {
    for (auto* check : sparEditor.findChildren<QCheckBox*>())
      if (check->text() == "Front Top Sheeting") return check;
    return static_cast<QCheckBox*>(nullptr);
  }();
  assert(leTopSheetCheck != nullptr && leTopSheetCheck->isVisible());
  leTopSheetCheck->click();
  QApplication::processEvents();
  assert(sparEditor.findChild<LengthInput*>("leTopSheetThickness")->isVisible());
  auto* frontTopStop =
      sparEditor.findChild<QDoubleSpinBox*>("leTopSheetStopChordPercent");
  auto* frontTopUpToSpar =
      sparEditor.findChild<QCheckBox*>("leTopSheetUpToSpar");
  assert(frontTopStop != nullptr && frontTopStop->value() == 30.0 &&
         frontTopStop->decimals() == 2);
  assert(frontTopUpToSpar != nullptr && frontTopUpToSpar->isChecked());
  assert(!frontTopStop->isEnabled());
  frontTopUpToSpar->click();
  assert(frontTopStop->isEnabled());
  frontTopStop->setValue(37.25);
  const auto frontSheetData = sparEditor.data();
  assert(!frontSheetData.leTopSheetUpToSpar);
  assert(std::abs(frontSheetData.leTopSheetStopChordPercent - 37.25) < 1.0e-8);
  const auto frontSheetJson = panelDataToJson(frontSheetData);
  assert(!frontSheetJson.value("leTopSheetUpToSpar").toBool(true));
  assert(std::abs(frontSheetJson.value("leTopSheetStopChordPercent").toDouble() -
                  37.25) < 1.0e-8);
  auto legacyFrontSheetJson = frontSheetJson;
  legacyFrontSheetJson.remove("leTopSheetUpToSpar");
  legacyFrontSheetJson.remove("leTopSheetStopChordPercent");
  const auto migratedFrontSheet = panelDataFromJson(legacyFrontSheetJson);
  assert(migratedFrontSheet.leTopSheetUpToSpar);
  assert(migratedFrontSheet.leTopSheetStopChordPercent == 30.0);
  sparEditor.hide();

  WingPanelEditor sparDefaultsEditor{
      WingPanelData{}, DisplayUnit::Inches, true};
  auto* defaultsAddSpar =
      sparDefaultsEditor.findChild<QPushButton*>("addSparButton");
  auto* defaultsDeleteSpar =
      sparDefaultsEditor.findChild<QPushButton*>("deleteSparButton");
  assert(defaultsAddSpar != nullptr && defaultsAddSpar->text() == "Add Spar");
  assert(defaultsDeleteSpar != nullptr && defaultsDeleteSpar->text() == "Delete Checked");
  assert(sparDefaultsEditor.findChildren<QWidget*>("sparEditorRow").size() == 1);
  assert(radioWithText(&sparDefaultsEditor, "Aluminum") == nullptr);
  assert(sparDefaultsEditor.findChild<QLabel*>("sparDefaultsLabel") == nullptr);
  assert(!sparDefaultsEditor.findChild<QCheckBox*>("sparDeleteSelection")->isHidden());
  auto* defaultShearWebs = sparDefaultsEditor.findChild<QCheckBox*>("sparShearWebs");
  assert(defaultShearWebs != nullptr && !defaultShearWebs->isHidden());
  auto* defaultShearThickness =
      sparDefaultsEditor.findChild<LengthInput*>("sparShearWebThickness");
  assert(defaultShearThickness->parentWidget()->isHidden());
  defaultShearWebs->click();
  assert(!defaultShearThickness->parentWidget()->isHidden());
  for (const auto* name : {"sparEditor1TubeOd", "sparEditor1TubeId",
                           "sparEditor1RodOd", "sparEditor1StripWidth",
                           "sparEditor1StripThickness"}) {
    const auto* input = sparDefaultsEditor.findChild<LengthInput*>(name);
    assert(input != nullptr && input->unitOverride() == UnitOverride::Millimeters);
  }
  assert(sparDefaultsEditor.findChild<LengthInput*>("sparEditor1WoodHeight")
             ->unitOverride() == UnitOverride::Global);
  assert(sparDefaultsEditor.findChild<LengthInput*>("sparEditor1WoodWidth")
             ->unitOverride() == UnitOverride::Global);
  assert(sparDefaultsEditor.findChild<LengthInput*>("sparShearWebThickness")
             ->unitOverride() == UnitOverride::Global);
  auto* defaultTubeOd = sparDefaultsEditor.findChild<LengthInput*>("sparEditor1TubeOd");
  defaultTubeOd->setValueMm(8.0);
  defaultTubeOd->setUnitOverride(UnitOverride::Inches);
  auto* defaultRodOd = sparDefaultsEditor.findChild<LengthInput*>("sparEditor1RodOd");
  defaultRodOd->setValueMm(7.0);
  defaultRodOd->setUnitOverride(UnitOverride::Millimeters);
  defaultsAddSpar->click();
  auto defaultSparRows = sparDefaultsEditor.findChildren<QWidget*>("sparEditorRow");
  assert(defaultSparRows.size() == 2);
  auto* secondRootChord = defaultSparRows[1]->findChild<QDoubleSpinBox*>(
      "sparRootChordLocation");
  auto* secondTipChord = defaultSparRows[1]->findChild<QDoubleSpinBox*>(
      "sparTipChordLocation");
  secondRootChord->setValue(55.25);
  secondTipChord->setValue(60.75);
  const auto savedSparDefaults = sparDefaultsEditor.data();
  assert(savedSparDefaults.spars.size() == 2);
  assert(std::abs(savedSparDefaults.spars[1].chordLocationPercent - 55.25) <
         1.0e-8);
  assert(std::abs(savedSparDefaults.spars[1].tipChordLocationPercent - 60.75) <
         1.0e-8);
  assert(std::abs(savedSparDefaults.sparDefaults.tubeOd - 8.0) < 1.0e-8);
  assert(std::abs(savedSparDefaults.sparDefaults.rodOd - 7.0) < 1.0e-8);
  assert(savedSparDefaults.unitOverrides.value("sparTubeOd") == UnitOverride::Inches);
  assert(savedSparDefaults.unitOverrides.value("sparRodOd") == UnitOverride::Millimeters);
  const auto restoredSparDefaults = panelDataFromJson(panelDataToJson(savedSparDefaults));
  assert(restoredSparDefaults.spars.size() == 2);
  assert(std::abs(restoredSparDefaults.spars[1].chordLocationPercent - 55.25) <
         1.0e-8);
  assert(std::abs(restoredSparDefaults.spars[1].tipChordLocationPercent -
                  60.75) < 1.0e-8);
  assert(std::abs(restoredSparDefaults.sparDefaults.tubeOd - 8.0) < 1.0e-8);
  assert(restoredSparDefaults.unitOverrides.value("sparTubeOd") == UnitOverride::Inches);

  WingPanelEditor seededSparEditor{restoredSparDefaults, DisplayUnit::Inches};
  auto* seededAdd = seededSparEditor.findChild<QPushButton*>("addSparButton");
  assert(seededAdd != nullptr);
  auto seededRows = seededSparEditor.findChildren<QWidget*>("sparEditorRow");
  assert(seededRows.size() == 2);
  auto* seededTubeOd = seededRows[0]->findChild<LengthInput*>("sparEditor1TubeOd");
  assert(std::abs(seededTubeOd->valueMm() - 8.0) < 1.0e-8);
  assert(seededTubeOd->unitOverride() == UnitOverride::Inches);
  seededAdd->click();
  seededRows = seededSparEditor.findChildren<QWidget*>("sparEditorRow");
  assert(seededRows.size() == 3);
  auto* addedTubeOd = seededRows[2]->findChild<LengthInput*>("sparEditor3TubeOd");
  assert(std::abs(addedTubeOd->valueMm() - 8.0) < 1.0e-8);
  assert(addedTubeOd->unitOverride() == UnitOverride::Inches);

  auto* firstDefaultSelection =
      defaultSparRows[0]->findChild<QCheckBox*>("sparDeleteSelection");
  firstDefaultSelection->setChecked(true);
  defaultsDeleteSpar->click();
  defaultSparRows = sparDefaultsEditor.findChildren<QWidget*>("sparEditorRow");
  assert(defaultSparRows.size() == 1);
  const auto defaultsAfterDelete = sparDefaultsEditor.data();
  assert(defaultsAfterDelete.spars.size() == 1);
  assert(std::abs(defaultsAfterDelete.spars.front().chordLocationPercent -
                  55.25) < 1.0e-8);
  assert(std::abs(defaultsAfterDelete.spars.front().tipChordLocationPercent -
                  60.75) < 1.0e-8);
  assert(std::abs(defaultsAfterDelete.sparDefaults.chordLocationPercent -
                  55.25) < 1.0e-8);
  assert(std::abs(defaultsAfterDelete.sparDefaults.tipChordLocationPercent -
                  60.75) < 1.0e-8);

  const auto inchDefaults = roundedInchPanelData(WingPanelData{});
  assert(std::abs(inchDefaults.panelSpan / 25.4 - 27.5625) < 1.0e-8);
  assert(std::abs(inchDefaults.rootChord / 25.4 - 9.4375) < 1.0e-8);
  assert(std::abs(inchDefaults.tipChord / 25.4 - 5.90625) < 1.0e-8);
  assert(std::abs(inchDefaults.ribThickness / 25.4 - 0.125) < 1.0e-8);
  assert(std::abs(inchDefaults.leadingEdgeTubeId / 25.4 - 1.0 / 32.0) < 1.0e-8);
  assert(installedDefaultDisplayUnit() == DisplayUnit::Inches);
  const auto installedMetric = installedDefaultPanelData(DisplayUnit::Millimeters);
  assert(installedMetric.leadingEdgeType == 2);
  assert(installedMetric.trailingEdgeType == 2);
  assert(installedMetric.panelSpan == 700.0 && installedMetric.ribCount == 9);
  assert(std::abs(installedMetric.spoilerMinimumWoodMargin - 6.0) <
         1.0e-8);
  assert(std::abs(installedMetric.spoilerMinimumCircleDistance - 12.0) <
         1.0e-8);
  assert(installedMetric.spars.size() == 1);
  assert(installedMetric.fixedJoiners.empty() && installedMetric.removableJoiners.empty());
  assert(std::abs(installedMetric.topTeSheetingWidth - 25.4) < 1.0e-8);
  assert(std::abs(installedMetric.bottomTeSheetingWidth - 25.4) < 1.0e-8);
  assert(std::abs(installedMetric.topTeSheetingThickness - 1.5875) < 1.0e-8);
  assert(std::abs(installedMetric.bottomTeSheetingThickness - 1.5875) < 1.0e-8);
  WingPanelEditor teSheetingEditor{installedMetric};
  auto* topTeSheeting =
      teSheetingEditor.findChild<QCheckBox*>("topTeSheeting");
  auto* bottomTeSheeting =
      teSheetingEditor.findChild<QCheckBox*>("bottomTeSheeting");
  auto* topTeWidth =
      teSheetingEditor.findChild<LengthInput*>("topTeSheetingWidth");
  auto* topTeTaperStart = teSheetingEditor.findChild<QDoubleSpinBox*>(
      "topTeSheetingTaperStartLocationPercent");
  auto* topTeThickness =
      teSheetingEditor.findChild<LengthInput*>("topTeSheetingThickness");
  assert(topTeSheeting && bottomTeSheeting && topTeWidth &&
         topTeTaperStart && topTeThickness);
  assert(topTeTaperStart->decimals() == 2 &&
         topTeTaperStart->singleStep() == 1.0);
  QRadioButton* sheetTeStock = nullptr;
  for (auto* radio : teSheetingEditor.findChildren<QRadioButton*>())
    if (radio->text() == "Sheet TE Stock") sheetTeStock = radio;
  assert(sheetTeStock != nullptr && sheetTeStock->isChecked());
  topTeSheeting->setChecked(true);
  assert(!sheetTeStock->isChecked());
  auto* topTaper = teSheetingEditor.findChild<QCheckBox*>("topTeSheetingTaper");
  auto* bottomTaper = teSheetingEditor.findChild<QCheckBox*>("bottomTeSheetingTaper");
  bool hasRearTopLabel = false;
  bool hasRearBottomLabel = false;
  for (auto* check : teSheetingEditor.findChildren<QCheckBox*>()) {
    hasRearTopLabel = hasRearTopLabel || check->text() == "Rear Top Sheeting";
    hasRearBottomLabel = hasRearBottomLabel || check->text() == "Rear Bottom Sheeting";
  }
  assert(topTaper && bottomTaper && hasRearTopLabel && hasRearBottomLabel);
  bottomTeSheeting->setChecked(true);
  assert(topTaper->isChecked() && bottomTaper->isChecked());
  assert(!topTaper->isEnabled() && !bottomTaper->isEnabled());
  bottomTeSheeting->setChecked(false);
  assert(topTaper->isEnabled());
  topTeWidth->setValueMm(31.75);
  topTeTaperStart->setValue(62.25);
  topTeThickness->setValueMm(1.5875);
  const auto savedTeSheeting = teSheetingEditor.data();
  const auto restoredTeSheeting =
      panelDataFromJson(panelDataToJson(savedTeSheeting));
  assert(restoredTeSheeting.topTeSheeting);
  assert(std::abs(restoredTeSheeting.topTeSheetingWidth - 31.75) <
         1.0e-8);
  assert(std::abs(
      restoredTeSheeting.topTeSheetingTaperStartLocationPercent - 62.25) <
         1.0e-8);
  assert(std::abs(restoredTeSheeting.topTeSheetingThickness - 1.5875) <
         1.0e-8);
  QJsonObject legacyTeSheeting;
  legacyTeSheeting.insert("rootChord", 254.0);
  legacyTeSheeting.insert("topTeSheeting", true);
  legacyTeSheeting.insert("topTeSheetingStartPercent", 70.0);
  legacyTeSheeting.insert("topTeSheetingTaper", true);
  legacyTeSheeting.insert("topTeSheetingTaperStartPercent", 85.0);
  const auto migratedTeSheeting = panelDataFromJson(legacyTeSheeting);
  assert(std::abs(migratedTeSheeting.topTeSheetingWidth - 76.2) < 1.0e-8);
  assert(std::abs(
      migratedTeSheeting.topTeSheetingTaperStartLocationPercent - 50.0) <
      1.0e-8);
  QJsonObject legacyShapedStock;
  legacyShapedStock.insert("leadingEdgeType", 1);
  legacyShapedStock.insert("trailingEdgeType", 1);
  const auto migratedStock = panelDataFromJson(legacyShapedStock);
  assert(migratedStock.leadingEdgeType == 2);
  assert(migratedStock.trailingEdgeType == 2);
  assert(std::abs(installedMetric.leadingEdgeHeight - 7.0) < 1.0e-8);
  assert(std::abs(installedMetric.trailingEdgeHeight - 3.0) < 1.0e-8);
  assert(installedMetric.unitOverrides.value("leadingEdgeWidth") == UnitOverride::Global);
  WingPanelEditor metricLeTeDefaults{
      installedMetric, DisplayUnit::Inches, true};
  auto* inchTeWidth =
      metricLeTeDefaults.findChild<LengthInput*>("topTeSheetingWidth");
  auto* inchTeThickness =
      metricLeTeDefaults.findChild<LengthInput*>("topTeSheetingThickness");
  assert(inchTeWidth && inchTeThickness);
  assert(inchTeWidth->findChild<QDoubleSpinBox*>()->text() == "1 in");
  assert(inchTeThickness->findChild<QDoubleSpinBox*>()->text() == "1/16 in");
  auto* metricLeStockWidth =
      metricLeTeDefaults.findChild<LengthInput*>("leadingEdgeWidth");
  assert(metricLeStockWidth != nullptr);
  assert(metricLeStockWidth->unitOverride() == UnitOverride::Global);
  auto* metricLeStockWidthSpin =
      metricLeStockWidth->findChild<QDoubleSpinBox*>();
  assert(metricLeStockWidthSpin != nullptr);
  assert(metricLeStockWidthSpin->suffix() == " in");
  metricLeTeDefaults.setGlobalUnit(DisplayUnit::Millimeters);
  assert(metricLeStockWidth->unitOverride() == UnitOverride::Global);
  assert(metricLeStockWidthSpin->suffix() == " mm");
  assert(std::abs(metricLeStockWidthSpin->value() -
                  installedMetric.leadingEdgeWidth) < 1.0e-8);
  auto* metricLeStockHeight =
      metricLeTeDefaults.findChild<LengthInput*>("leadingEdgeHeight");
  assert(metricLeStockHeight != nullptr);
  metricLeStockHeight->setUnitOverride(UnitOverride::Global);
  metricLeTeDefaults.setLeadingEdgeHeightMm(12.345);
  assert(std::abs(metricLeTeDefaults.data().leadingEdgeHeight - 12.345) <
         1.0e-8);
  assert(metricLeStockHeight->unitOverride() == UnitOverride::Global);
  auto* metricTeStockHeight =
      metricLeTeDefaults.findChild<LengthInput*>("trailingEdgeHeight");
  assert(metricTeStockHeight != nullptr);
  metricTeStockHeight->setUnitOverride(UnitOverride::Global);
  metricLeTeDefaults.setTrailingEdgeHeightMm(6.789);
  assert(std::abs(metricLeTeDefaults.data().trailingEdgeHeight - 6.789) <
         1.0e-8);
  assert(metricTeStockHeight->unitOverride() == UnitOverride::Global);
  const auto installedInches = installedDefaultPanelData(DisplayUnit::Inches);
  assert(installedInches.unitOverrides.value("leadingEdgeWidth") ==
         UnitOverride::Global);
  assert(std::abs(installedInches.panelSpan / 25.4 - 27.5) < 1.0e-8);
  assert(std::abs(installedInches.rootChord / 25.4 - 10.0) < 1.0e-8);
  assert(std::abs(installedInches.tipChord / 25.4 - 6.0) < 1.0e-8);
  assert(std::abs(installedInches.sweep / 25.4 - 1.0) < 1.0e-8);
  assert(installedInches.ribCount == 11);
  assert(installedInches.spars.size() == 2);
  assert(installedInches.spars[0].material == 0 &&
         installedInches.spars[0].verticalLocation == 0);
  assert(installedInches.spars[1].material == 0 &&
         installedInches.spars[1].verticalLocation == 1);
  assert(installedInches.topSpar && installedInches.bottomSpar);
  assert(!installedInches.centerSparWoodJoiner);
  assert(woodJoinerSparAlignmentError({installedInches}).isEmpty());
  assert(installedInches.aileronStartRib == 2 &&
         installedInches.aileronStopRib == 8);
  assert(installedInches.flapStartRib == 2 &&
         installedInches.flapStopRib == 5);
  assert(installedInches.wiringHoleEndRib == 8);
  assert(std::abs(
             installedInches.spoilerMinimumWoodMargin / 25.4 - 5.0 / 16.0) <
         1.0e-8);
  assert(std::abs(
             installedInches.spoilerMinimumCircleDistance / 25.4 - 0.5) <
         1.0e-8);
  assert(installedInches.leadingEdgeType == 2 && installedInches.trailingEdgeType == 2);
  assert(std::abs(installedInches.topTeSheetingThickness / 25.4 - 1.0 / 16.0) <
         1.0e-8);
  assert(std::abs(installedInches.bottomTeSheetingThickness / 25.4 - 1.0 / 16.0) <
         1.0e-8);
  assert(std::abs(installedInches.leadingEdgeHeight / 25.4 - 0.625) < 1.0e-8);
  assert(std::abs(installedInches.trailingEdgeHeight / 25.4 - 0.375) < 1.0e-8);
  assert(installedInches.behindSparJoinerType == 3);
  assert(installedInches.joinerPanelMode == 1);
  assert(installedInches.fixedJoiners.size() == 1);
  assert(installedInches.fixedJoiners.front().material == 0);
  assert(installedInches.fixedJoiners.front().chordLocationPercent == 25);
  assert(installedInches.fixedJoiners.front().carbonTubeOdUnit ==
         UnitOverride::Millimeters);
  assert(installedInches.removableJoiners.size() == 2);
  assert(installedInches.removableJoiners[0].kind == 0 &&
         installedInches.removableJoiners[0].chordLocationPercent == 35);
  assert(installedInches.removableJoiners[0].thisPanelPart == 1 &&
         installedInches.removableJoiners[0].thisRodMaterial == 0);
  assert(std::abs(installedInches.removableJoiners[0].thisRodOd - 5.0) <
         1.0e-8);
  assert(std::abs(installedInches.removableJoiners[0].thisSleeveOd - 6.0) <
         1.0e-8);
  assert(installedInches.removableJoiners[0].adjoiningRodMaterial == 0);
  assert(installedInches.removableJoiners[0].adjoiningSleeveMaterial == 0);
  assert(std::abs(
             installedInches.removableJoiners[0].adjoiningSleeveOd - 6.0) <
         1.0e-8);
  assert(installedInches.removableJoiners[1].kind == 1 &&
         installedInches.removableJoiners[1].chordLocationPercent == 70);
  assert(installedInches.removableJoiners[1].alignmentMode == 0 &&
         installedInches.removableJoiners[1].pinHoleThisPart == 1 &&
         installedInches.removableJoiners[1].pinMaterial == 1);
  assert(installedInches.removableJoiners[1].pinOdUnit ==
         UnitOverride::Millimeters);
  assert(installedInches.unitOverrides.value("wiringHoleWidth") ==
         UnitOverride::Global);
  WingPanelData controls;
  controls.ailerons = true;
  controls.flaps = true;
  controls.aileronHingePostWidth = 7.0;
  controls.flapHingePostHeight = 11.0;
  controls.addRib1a = true;
  controls.centerSparWoodJoiner = true;
  controls.behindSparJoiner = true;
  controls.behindSparJoinerType = 3;
  controls.behindSparJoinerOd = 7.0;
  controls.joinerPanelMode = 1;
  controls.fixedJoiners = {{42.25, 2, 4.0, 1, 7.0, 5.5, 6.5, 8.0}};
  const auto restored = panelDataFromJson(panelDataToJson(controls));
  assert(std::abs(restored.aileronHingePostWidth - 7.0) < 1.0e-8);
  assert(std::abs(restored.flapHingePostHeight - 11.0) < 1.0e-8);
  assert(restored.addRib1a && !restored.centerSparWoodJoiner);
  assert(!restored.behindSparJoiner && restored.behindSparJoinerType == 3);
  assert(std::abs(restored.behindSparJoinerOd - 7.0) < 1.0e-8);
  assert(restored.joinerPanelMode == 1 && restored.fixedJoiners.size() == 1);
  assert(std::abs(restored.fixedJoiners.front().chordLocationPercent - 42.25) <
         1.0e-8);
  assert(restored.fixedJoiners.front().material == 2);
  assert(std::abs(restored.fixedJoiners.front().steelRodOd - 8.0) < 1.0e-8);
  WingPanelEditor controlsLayoutEditor{controls};
  auto* controlsTabs = controlsLayoutEditor.findChild<QTabWidget*>();
  assert(controlsTabs != nullptr);
  int controlsTabIndex = -1;
  for (int index = 0; index < controlsTabs->count(); ++index)
    if (controlsTabs->tabText(index) == "Ailerons/Flaps")
      controlsTabIndex = index;
  assert(controlsTabIndex >= 0);
  controlsTabs->setCurrentIndex(controlsTabIndex);
  auto* aileronHingeWidth = controlsLayoutEditor.findChild<LengthInput*>(
      "aileronHingePostWidth");
  auto* aileronHingeHeight = controlsLayoutEditor.findChild<LengthInput*>(
      "aileronHingePostHeight");
  auto* flapHingeWidth = controlsLayoutEditor.findChild<LengthInput*>(
      "flapHingePostWidth");
  auto* flapHingeHeight = controlsLayoutEditor.findChild<LengthInput*>(
      "flapHingePostHeight");
  assert(aileronHingeWidth && aileronHingeHeight);
  assert(flapHingeWidth && flapHingeHeight);
  controlsLayoutEditor.resize(800, 900);
  controlsLayoutEditor.show();
  QApplication::processEvents();
  const auto controlsVerticalPosition = [&](const QWidget* widget) {
    return widget->mapTo(&controlsLayoutEditor, QPoint{}).y();
  };
  assert(controlsVerticalPosition(aileronHingeWidth) <
         controlsVerticalPosition(aileronHingeHeight));
  assert(controlsVerticalPosition(flapHingeWidth) <
         controlsVerticalPosition(flapHingeHeight));
  controlsLayoutEditor.hide();
  QJsonObject legacyCarbonJoiners;
  legacyCarbonJoiners.insert("behindSparJoiner", true);
  legacyCarbonJoiners.insert("behindSparJoinerType", 1);
  legacyCarbonJoiners.insert("behindSparJoinerOd", 6.0);
  legacyCarbonJoiners.insert("behindSparJoinerId", 7.0);
  legacyCarbonJoiners.insert("fiftyPercentJoiner", true);
  legacyCarbonJoiners.insert("fiftyPercentJoinerType", 2);
  legacyCarbonJoiners.insert("fiftyPercentJoinerOd", 8.0);
  legacyCarbonJoiners.insert("fiftyPercentJoinerId", 6.0);
  const auto migratedCarbonJoiners = panelDataFromJson(legacyCarbonJoiners);
  assert(migratedCarbonJoiners.joinerPanelMode == 1);
  assert(migratedCarbonJoiners.fixedJoiners.size() == 2);
  assert(migratedCarbonJoiners.fixedJoiners[0].chordLocationPercent == 30);
  assert(migratedCarbonJoiners.fixedJoiners[0].carbonType == 1);
  assert(std::abs(migratedCarbonJoiners.fixedJoiners[0].carbonRodOd - 6.0) < 1.0e-8);
  assert(migratedCarbonJoiners.fixedJoiners[1].chordLocationPercent == 60);
  assert(migratedCarbonJoiners.fixedJoiners[1].carbonType == 0);
  assert(std::abs(migratedCarbonJoiners.fixedJoiners[1].carbonTubeOd - 8.0) < 1.0e-8);
  assert(std::abs(migratedCarbonJoiners.fixedJoiners[1].carbonTubeId - 6.0) < 1.0e-8);
  WingPanelEditor migratedCarbonJoinerEditor{migratedCarbonJoiners};
  const auto migratedCarbonJoinerUiData = migratedCarbonJoinerEditor.data();
  assert(migratedCarbonJoinerUiData.joinerPanelMode == 1);
  assert(migratedCarbonJoinerUiData.fixedJoiners.size() == 2);
  QJsonObject legacyCarbonSpar;
  legacyCarbonSpar.insert("carbonSpar", 1);
  legacyCarbonSpar.insert("cfTubeOd", 6.0);
  legacyCarbonSpar.insert("cfTubeId", 5.0);
  const auto migratedCarbonSpar = panelDataFromJson(legacyCarbonSpar);
  assert(migratedCarbonSpar.spars.size() == 1);
  assert(migratedCarbonSpar.spars.front().chordLocationPercent == 25);
  assert(migratedCarbonSpar.spars.front().verticalLocation == 2);
  assert(migratedCarbonSpar.spars.front().material == 1);
  assert(migratedCarbonSpar.spars.front().type == 0);
  assert(std::abs(migratedCarbonSpar.spars.front().tubeOd - 6.0) < 1.0e-8);
  assert(std::abs(migratedCarbonSpar.spars.front().tubeId - 5.0) < 1.0e-8);
  WingPanelEditor migratedCarbonSparEditor{migratedCarbonSpar};
  assert(migratedCarbonSparEditor.findChildren<QWidget*>("sparEditorRow").size() == 1);
  const auto migratedCarbonSparUiData = migratedCarbonSparEditor.data();
  assert(migratedCarbonSparUiData.spars.size() == 1);
  assert(migratedCarbonSparUiData.spars.front().type == 0);

  QJsonObject legacyWoodSpars;
  legacyWoodSpars.insert("topSpar", true);
  legacyWoodSpars.insert("topSparHeight", 4.0);
  legacyWoodSpars.insert("topSparWidth", 8.0);
  legacyWoodSpars.insert("bottomSpar", true);
  legacyWoodSpars.insert("bottomSparHeight", 5.0);
  legacyWoodSpars.insert("bottomSparWidth", 9.0);
  legacyWoodSpars.insert("topRearSpar", true);
  legacyWoodSpars.insert("topRearSparHeight", 3.0);
  legacyWoodSpars.insert("topRearSparWidth", 4.0);
  legacyWoodSpars.insert("shearWebs", true);
  legacyWoodSpars.insert("shearWebWidth", 2.5);
  const auto migratedWoodSpars = panelDataFromJson(legacyWoodSpars);
  assert(migratedWoodSpars.spars.size() == 3);
  assert(migratedWoodSpars.spars[0].chordLocationPercent == 25 &&
         migratedWoodSpars.spars[0].verticalLocation == 0);
  assert(migratedWoodSpars.spars[1].chordLocationPercent == 25 &&
         migratedWoodSpars.spars[1].verticalLocation == 1);
  assert(migratedWoodSpars.spars[2].chordLocationPercent == 60 &&
         migratedWoodSpars.spars[2].verticalLocation == 0);
  assert(migratedWoodSpars.sparShearWebs);
  assert(std::abs(migratedWoodSpars.sparDefaults.shearWebThickness - 2.5) < 1.0e-8);
  QJsonObject unsupportedLegacyAluminum;
  unsupportedLegacyAluminum.insert("behindSparJoiner", true);
  unsupportedLegacyAluminum.insert("behindSparJoinerType", 3);
  const auto preservedLegacyAluminum = panelDataFromJson(unsupportedLegacyAluminum);
  assert(preservedLegacyAluminum.joinerPanelMode == -1);
  assert(preservedLegacyAluminum.fixedJoiners.empty());
  QJsonObject staleLegacyWoodJoiner;
  staleLegacyWoodJoiner.insert("centerSparWoodJoiner", true);
  staleLegacyWoodJoiner.insert("joinerPanelMode", 1);
  staleLegacyWoodJoiner.insert("fixedJoiners", QJsonArray{});
  staleLegacyWoodJoiner.insert("removableJoiners", QJsonArray{});
  const auto cleanedCurrentJoiners = panelDataFromJson(staleLegacyWoodJoiner);
  assert(!cleanedCurrentJoiners.centerSparWoodJoiner);
  assert(cleanedCurrentJoiners.joinerPanelMode == 1);
  WingPanelEditor outerPanel{WingPanelData{}, DisplayUnit::Millimeters, false, true};
  for (const auto* button : outerPanel.findChildren<QRadioButton*>()) {
    assert(button->text() != "Shaped LE Stock");
    assert(button->text() != "Shaped TE Stock");
  }
  auto* tabs = outerPanel.findChild<QTabWidget*>();
  assert(tabs != nullptr);
  bool foundJoinerTab = false;
  for (int i = 0; i < tabs->count(); ++i)
    if (tabs->tabText(i) == "Joiners") foundJoinerTab = true;
  assert(foundJoinerTab);
  auto* fixedPanelButton = outerPanel.findChild<QPushButton*>("fixedPanelButton");
  auto* addFixedJoinerButton = outerPanel.findChild<QPushButton*>("addFixedJoinerButton");
  assert(fixedPanelButton != nullptr && addFixedJoinerButton != nullptr);
  fixedPanelButton->click(); addFixedJoinerButton->click();
  const auto defaultFixedJoiner = outerPanel.data();
  assert(defaultFixedJoiner.joinerPanelMode == 1);
  assert(defaultFixedJoiner.fixedJoiners.size() == 1);
  assert(defaultFixedJoiner.fixedJoiners.front().chordLocationPercent == 35);
  assert(defaultFixedJoiner.fixedJoiners.front().material == 1);
  WingPanelData metricFixedProject;
  metricFixedProject.joinerPanelMode = 1;
  FixedJoinerData metricFixedSeed;
  metricFixedSeed.material = 1;
  metricFixedSeed.carbonTubeOdUnit = UnitOverride::Millimeters;
  metricFixedSeed.carbonTubeIdUnit = UnitOverride::Millimeters;
  metricFixedSeed.carbonRodOdUnit = UnitOverride::Millimeters;
  metricFixedProject.fixedJoiners = {metricFixedSeed};
  WingPanelEditor metricFixedProjectEditor{
      metricFixedProject, DisplayUnit::Inches, false, true};
  auto* metricProjectAddFixed =
      metricFixedProjectEditor.findChild<QPushButton*>(
          "addFixedJoinerButton");
  assert(metricProjectAddFixed != nullptr);
  metricProjectAddFixed->click();
  const auto addedMetricFixedProject = metricFixedProjectEditor.data();
  assert(addedMetricFixedProject.fixedJoiners.size() == 2);
  assert(addedMetricFixedProject.fixedJoiners[1].carbonTubeOdUnit ==
         UnitOverride::Millimeters);
  assert(addedMetricFixedProject.fixedJoiners[1].carbonTubeIdUnit ==
         UnitOverride::Millimeters);
  assert(addedMetricFixedProject.fixedJoiners[1].carbonRodOdUnit ==
         UnitOverride::Millimeters);
  auto* unavailableWood = outerPanel.findChild<QRadioButton*>("fixedJoinerWoodOption");
  assert(unavailableWood != nullptr && unavailableWood->isHidden());
  WingPanelData woodJoinerEligible;
  woodJoinerEligible.spars = {
      {35, 0, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0},
      {35, 1, 0, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  woodJoinerEligible.joinerPanelMode = 1;
  woodJoinerEligible.fixedJoiners = {FixedJoinerData{}};
  WingPanelEditor eligibleWoodJoinerPanel{woodJoinerEligible};
  auto* availableWood = eligibleWoodJoinerPanel.findChild<QRadioButton*>("fixedJoinerWoodOption");
  assert(availableWood != nullptr && !availableWood->isHidden());
  availableWood->click();
  assert(eligibleWoodJoinerPanel.data().fixedJoiners.front().chordLocationPercent == 25);
  auto* fixedJoinerChord = eligibleWoodJoinerPanel.findChild<QDoubleSpinBox*>(
      "fixedJoinerChordLocationPercent");
  assert(fixedJoinerChord && fixedJoinerChord->decimals() == 2 &&
         std::abs(fixedJoinerChord->singleStep() - 1.0) < 1.0e-8);
  fixedJoinerChord->setValue(25.75);
  assert(std::abs(eligibleWoodJoinerPanel.data().fixedJoiners.front()
                      .chordLocationPercent - 25.75) < 1.0e-8);
  WingPanelEditor removableJoinerPanel{WingPanelData{}, DisplayUnit::Millimeters, false, true};
  auto* removablePanelButton = removableJoinerPanel.findChild<QPushButton*>("removablePanelButton");
  auto* addSleeveRodButton = removableJoinerPanel.findChild<QPushButton*>("addSleeveRodJoinerButton");
  auto* addAlignmentPinButton = removableJoinerPanel.findChild<QPushButton*>("addAlignmentPinButton");
  assert(removablePanelButton && addSleeveRodButton && addAlignmentPinButton);
  removablePanelButton->click(); addSleeveRodButton->click(); addAlignmentPinButton->click();
  const auto defaultRemovableJoiners = removableJoinerPanel.data();
  assert(defaultRemovableJoiners.joinerPanelMode == 0);
  assert(defaultRemovableJoiners.removableJoiners.size() == 2);
  assert(defaultRemovableJoiners.removableJoiners[0].kind == 0);
  assert(defaultRemovableJoiners.removableJoiners[0].chordLocationPercent == 35);
  assert(defaultRemovableJoiners.removableJoiners[0].thisPanelPart == 1);
  assert(defaultRemovableJoiners.removableJoiners[0].adjoiningPanelPart == 0);
  assert(defaultRemovableJoiners.removableJoiners[0].thisSleeveMaterial == 1);
  assert(defaultRemovableJoiners.removableJoiners[1].kind == 1);
  assert(defaultRemovableJoiners.removableJoiners[1].chordLocationPercent == 70);
  assert(defaultRemovableJoiners.removableJoiners[1].alignmentMode == 1);
  assert(defaultRemovableJoiners.removableJoiners[1].thisPanelPart == 0);
  assert(defaultRemovableJoiners.removableJoiners[1].pinHoleThisPart == 0);
  assert(defaultRemovableJoiners.removableJoiners[1].pinMaterial == 0);
  auto* removableChord = removableJoinerPanel.findChild<QDoubleSpinBox*>(
      "removableJoinerChordLocationPercent");
  auto* alignmentChord = removableJoinerPanel.findChild<QDoubleSpinBox*>(
      "alignmentPinChordLocationPercent");
  assert(removableChord && alignmentChord);
  assert(removableChord->decimals() == 2 && alignmentChord->decimals() == 2);
  assert(std::abs(removableChord->singleStep() - 1.0) < 1.0e-8 &&
         std::abs(alignmentChord->singleStep() - 1.0) < 1.0e-8);
  removableChord->setValue(35.25);
  alignmentChord->setValue(70.75);
  const auto fractionalJoiners = removableJoinerPanel.data().removableJoiners;
  assert(std::abs(fractionalJoiners[0].chordLocationPercent - 35.25) < 1.0e-8);
  assert(std::abs(fractionalJoiners[1].chordLocationPercent - 70.75) < 1.0e-8);
  auto* adjoiningRod = removableJoinerPanel.findChild<QRadioButton*>("removableAdjoiningPanelRod");
  assert(adjoiningRod != nullptr);
  adjoiningRod->click();
  const auto twoRodJoiner = removableJoinerPanel.data().removableJoiners.front();
  assert(twoRodJoiner.thisPanelPart == 1 && twoRodJoiner.adjoiningPanelPart == 1);
  const auto twoRodRestored = panelDataFromJson(panelDataToJson(removableJoinerPanel.data()));
  assert(twoRodRestored.removableJoiners.front().adjoiningPanelPart == 1);
  assert(std::abs(twoRodRestored.removableJoiners[0].chordLocationPercent -
                  35.25) < 1.0e-8);
  assert(std::abs(twoRodRestored.removableJoiners[1].chordLocationPercent -
                  70.75) < 1.0e-8);
  WingPanelEditor joinerDefaults{WingPanelData{}, DisplayUnit::Millimeters, true, true};
  auto* defaultRemovableButton = joinerDefaults.findChild<QPushButton*>("removablePanelButton");
  auto* defaultFixedButton = joinerDefaults.findChild<QPushButton*>("fixedPanelButton");
  auto* fixedButtonRow = joinerDefaults.findChild<QWidget*>("fixedJoinerButtonRow");
  auto* removableButtonRow = joinerDefaults.findChild<QWidget*>("removableJoinerButtonRow");
  auto* defaultAddFixed = joinerDefaults.findChild<QPushButton*>("addFixedJoinerButton");
  auto* defaultAddSleeveRod =
      joinerDefaults.findChild<QPushButton*>("addSleeveRodJoinerButton");
  auto* defaultAddAlignment =
      joinerDefaults.findChild<QPushButton*>("addAlignmentPinButton");
  auto* defaultDeleteFixed =
      joinerDefaults.findChild<QPushButton*>("deleteFixedJoinersButton");
  auto* defaultDeleteRemovable =
      joinerDefaults.findChild<QPushButton*>("deleteRemovableJoinersButton");
  assert(defaultRemovableButton && defaultFixedButton && fixedButtonRow && removableButtonRow);
  assert(defaultAddFixed && defaultAddSleeveRod && defaultAddAlignment);
  assert(defaultDeleteFixed && defaultDeleteRemovable);
  defaultRemovableButton->click();
  auto removableDefaults = joinerDefaults.data();
  assert(removableDefaults.removableJoiners.size() == 2);
  assert(fixedButtonRow->isHidden() && !removableButtonRow->isHidden());
  auto* rememberedRodOd = joinerDefaults.findChild<LengthInput*>("removableThisRodOd");
  assert(rememberedRodOd != nullptr);
  joinerDefaults.setGlobalUnit(DisplayUnit::Inches);
  rememberedRodOd->setUnitOverride(UnitOverride::Millimeters);
  rememberedRodOd->setValueMm(8.0);
  for (const auto* key : {
           "removableThisSleeveOd", "removableAdjoiningRodOd",
           "removableAdjoiningSleeveOd", "alignmentThisRodOd",
           "alignmentThisSleeveOd", "alignmentAdjoiningRodOd",
           "alignmentAdjoiningSleeveOd", "alignmentPinOd"}) {
    auto* input = joinerDefaults.findChild<LengthInput*>(key);
    assert(input != nullptr);
    input->setUnitOverride(UnitOverride::Millimeters);
  }
  joinerDefaults.findChild<LengthInput*>("alignmentThisRodOd")
      ->setValueMm(4.1);
  joinerDefaults.findChild<LengthInput*>("alignmentThisSleeveOd")
      ->setValueMm(5.2);
  joinerDefaults.findChild<LengthInput*>("alignmentAdjoiningRodOd")
      ->setValueMm(4.3);
  joinerDefaults.findChild<LengthInput*>("alignmentAdjoiningSleeveOd")
      ->setValueMm(5.4);
  joinerDefaults.findChild<LengthInput*>("alignmentPinOd")
      ->setValueMm(2.7);
  defaultAddSleeveRod->click();
  defaultAddAlignment->click();
  removableDefaults = joinerDefaults.data();
  assert(removableDefaults.removableJoiners.size() == 4);
  assert(std::count_if(removableDefaults.removableJoiners.begin(),
                       removableDefaults.removableJoiners.end(),
                       [](const auto& joiner) { return joiner.kind == 0; }) == 2);
  assert(std::count_if(removableDefaults.removableJoiners.begin(),
                       removableDefaults.removableJoiners.end(),
                       [](const auto& joiner) { return joiner.kind == 1; }) == 2);
  for (const std::size_t index : {std::size_t{2}, std::size_t{3}}) {
    const auto& added = removableDefaults.removableJoiners[index];
    assert(added.thisRodOdUnit == UnitOverride::Millimeters);
    assert(added.thisSleeveOdUnit == UnitOverride::Millimeters);
    assert(added.adjoiningRodOdUnit == UnitOverride::Millimeters);
    assert(added.adjoiningSleeveOdUnit == UnitOverride::Millimeters);
    if (added.kind == 1)
      assert(added.pinOdUnit == UnitOverride::Millimeters);
  }
  assert(std::abs(removableDefaults.removableJoiners[2].thisRodOd - 8.0) <
         1.0e-8);
  const auto& addedAlignment = removableDefaults.removableJoiners[3];
  assert(std::abs(addedAlignment.thisRodOd - 4.1) < 1.0e-8);
  assert(std::abs(addedAlignment.thisSleeveOd - 5.2) < 1.0e-8);
  assert(std::abs(addedAlignment.adjoiningRodOd - 4.3) < 1.0e-8);
  assert(std::abs(addedAlignment.adjoiningSleeveOd - 5.4) < 1.0e-8);
  assert(std::abs(addedAlignment.pinOd - 2.7) < 1.0e-8);
  defaultFixedButton->click();
  assert(!fixedButtonRow->isHidden() && removableButtonRow->isHidden());
  auto* rememberedFixedTubeOd =
      joinerDefaults.findChild<LengthInput*>("fixedJoinerTubeOd");
  auto* rememberedFixedTubeId =
      joinerDefaults.findChild<LengthInput*>("fixedJoinerTubeId");
  auto* rememberedFixedRodOd =
      joinerDefaults.findChild<LengthInput*>("fixedJoinerRodOd");
  assert(rememberedFixedTubeOd && rememberedFixedTubeId &&
         rememberedFixedRodOd);
  rememberedFixedTubeOd->setUnitOverride(UnitOverride::Millimeters);
  rememberedFixedTubeId->setUnitOverride(UnitOverride::Millimeters);
  rememberedFixedRodOd->setUnitOverride(UnitOverride::Millimeters);
  defaultAddFixed->click();
  auto fixedDefaults = joinerDefaults.data();
  assert(fixedDefaults.fixedJoiners.size() == 2);
  assert(fixedDefaults.fixedJoiners[1].carbonTubeOdUnit ==
         UnitOverride::Millimeters);
  assert(fixedDefaults.fixedJoiners[1].carbonTubeIdUnit ==
         UnitOverride::Millimeters);
  assert(fixedDefaults.fixedJoiners[1].carbonRodOdUnit ==
         UnitOverride::Millimeters);
  assert(fixedDefaults.removableJoiners.size() == 4);
  assert(std::abs(fixedDefaults.removableJoiners[0].thisRodOd - 8.0) < 1.0e-8);
  auto removableProjectDefaults = fixedDefaults;
  removableProjectDefaults.joinerPanelMode = 0;
  WingPanelEditor removableProjectEditor{
      removableProjectDefaults, DisplayUnit::Inches, false, true};
  auto* projectAddSleeveRod =
      removableProjectEditor.findChild<QPushButton*>(
          "addSleeveRodJoinerButton");
  auto* projectAddAlignment =
      removableProjectEditor.findChild<QPushButton*>(
          "addAlignmentPinButton");
  assert(projectAddSleeveRod && projectAddAlignment);
  projectAddSleeveRod->click();
  projectAddAlignment->click();
  const auto removableProjectData = removableProjectEditor.data();
  assert(removableProjectData.removableJoiners.size() == 6);
  assert(removableProjectData.removableJoiners[4].thisRodOdUnit ==
         UnitOverride::Millimeters);
  assert(removableProjectData.removableJoiners[5].pinOdUnit ==
         UnitOverride::Millimeters);
  WingPanelData conflictingProjectDefaults;
  conflictingProjectDefaults.joinerPanelMode = 0;
  RemovableJoinerData projectAlignment;
  projectAlignment.kind = 1;
  projectAlignment.thisSleeveOd = 9.9;
  projectAlignment.thisSleeveOdUnit = UnitOverride::Global;
  conflictingProjectDefaults.removableJoiners = {projectAlignment};
  WingPanelEditor savedTemplateProjectEditor{
      conflictingProjectDefaults, DisplayUnit::Inches, false, true};
  savedTemplateProjectEditor.setJoinerAddDefaults(fixedDefaults);
  auto* savedTemplateAddAlignment =
      savedTemplateProjectEditor.findChild<QPushButton*>(
          "addAlignmentPinButton");
  assert(savedTemplateAddAlignment);
  savedTemplateAddAlignment->click();
  const auto savedTemplateProjectData = savedTemplateProjectEditor.data();
  assert(savedTemplateProjectData.removableJoiners.size() == 2);
  assert(std::abs(
             savedTemplateProjectData.removableJoiners[1].thisSleeveOd -
             5.2) < 1.0e-8);
  assert(savedTemplateProjectData.removableJoiners[1].thisSleeveOdUnit ==
         UnitOverride::Millimeters);
  assert(savedTemplateProjectData.removableJoiners[1].pinOdUnit ==
         UnitOverride::Millimeters);
  const auto restoredJoinerDefaults =
      panelDataFromJson(panelDataToJson(fixedDefaults));
  assert(restoredJoinerDefaults.fixedJoiners.size() == 2);
  assert(restoredJoinerDefaults.removableJoiners.size() == 4);
  assert(restoredJoinerDefaults.removableJoiners[0].thisRodOdUnit ==
         UnitOverride::Millimeters);
  assert(std::abs(
      restoredJoinerDefaults.removableJoiners[3].thisSleeveOd - 5.2) <
      1.0e-8);
  WingPanelEditor reopenedJoinerDefaults{
      restoredJoinerDefaults, DisplayUnit::Millimeters, true, true};
  assert(reopenedJoinerDefaults.data().fixedJoiners.size() == 2);
  assert(reopenedJoinerDefaults.data().removableJoiners.size() == 4);
  assert(std::abs(
      reopenedJoinerDefaults.data().removableJoiners[3].thisSleeveOd - 5.2) <
      1.0e-8);
  assert(reopenedJoinerDefaults.data().fixedJoiners[1].carbonTubeOdUnit ==
         UnitOverride::Millimeters);
  QCheckBox* fixedJoinerToDelete = nullptr;
  for (auto* check : joinerDefaults.findChildren<QCheckBox*>()) {
    if (check->text() == "Joiner 2" && !check->parentWidget()->isHidden()) {
      fixedJoinerToDelete = check;
      break;
    }
  }
  assert(fixedJoinerToDelete != nullptr);
  fixedJoinerToDelete->setChecked(true);
  defaultDeleteFixed->click();
  assert(joinerDefaults.data().fixedJoiners.size() == 1);
  assert(joinerDefaults.data().removableJoiners.size() == 4);
  defaultRemovableButton->click();
  assert(std::abs(joinerDefaults.data().removableJoiners[0].thisRodOd - 8.0) < 1.0e-8);
  const auto visibleRemovableDefaults = joinerDefaults.data();
  WingPanelEditor newProjectJoiners{
      visibleRemovableDefaults, DisplayUnit::Inches, false, true};
  const auto newProjectData = newProjectJoiners.data();
  assert(newProjectData.joinerPanelMode == 0);
  assert(newProjectData.removableJoiners.size() ==
         visibleRemovableDefaults.removableJoiners.size());
  auto* newProjectRodOd =
      newProjectJoiners.findChild<LengthInput*>("removableThisRodOd");
  assert(newProjectRodOd != nullptr);
  assert(newProjectRodOd->unitOverride() == UnitOverride::Millimeters);
  assert(newProjectRodOd->findChild<QDoubleSpinBox*>()->text().contains("mm"));
  QCheckBox* removableJoinerToDelete = nullptr;
  for (auto* check : joinerDefaults.findChildren<QCheckBox*>()) {
    if (check->text() == "Joiner 2" && !check->parentWidget()->isHidden()) {
      removableJoinerToDelete = check;
      break;
    }
  }
  assert(removableJoinerToDelete != nullptr);
  removableJoinerToDelete->setChecked(true);
  defaultDeleteRemovable->click();
  assert(joinerDefaults.data().removableJoiners.size() == 3);
  WingPanelData inheritedPanelData;
  inheritedPanelData.addRib1a = true;
  WingPanelEditor inheritedChordPanel{
      inheritedPanelData, DisplayUnit::Millimeters, false, true, false};
  auto* hiddenRootChord = inheritedChordPanel.findChild<LengthInput*>("rootChord");
  assert(hiddenRootChord != nullptr && hiddenRootChord->isHidden());
  auto* addRib1a = inheritedChordPanel.findChild<QCheckBox*>("addRib1a");
  assert(addRib1a != nullptr && addRib1a->isHidden());
  assert(!inheritedChordPanel.data().addRib1a);
  WingPanelData legacyFullSpanControls;
  legacyFullSpanControls.ribCount = 7;
  legacyFullSpanControls.ailerons = true;
  legacyFullSpanControls.aileronStartRib = 1;
  legacyFullSpanControls.aileronStopRib = 7;
  legacyFullSpanControls.flaps = true;
  legacyFullSpanControls.flapStartRib = 1;
  legacyFullSpanControls.flapStopRib = 7;
  WingPanelEditor internalControls{legacyFullSpanControls};
  const auto internalControlData = internalControls.data();
  assert(internalControlData.aileronStartRib == 2 && internalControlData.aileronStopRib == 7);
  assert(internalControlData.flapStartRib == 2 && internalControlData.flapStopRib == 6);
  for (const auto* name : {"aileronStartRib", "flapStartRib", "flapStopRib"}) {
    const auto* control = internalControls.findChild<QSpinBox*>(name);
    assert(control != nullptr && control->minimum() == 2 && control->maximum() == 6);
  }
  const auto* aileronStop = internalControls.findChild<QSpinBox*>("aileronStopRib");
  assert(aileronStop != nullptr && aileronStop->minimum() == 2 &&
         aileronStop->maximum() == 7);
  WingPanelData overlappingControls = legacyFullSpanControls;
  overlappingControls.flapStartRib = 2;
  overlappingControls.flapStopRib = 5;
  overlappingControls.aileronStartRib = 3;
  overlappingControls.aileronStopRib = 4;
  WingPanelEditor correctedControls{overlappingControls};
  QString controlError;
  assert(!correctedControls.validate(controlError));
  const auto correctedControlData = correctedControls.data();
  assert(controlError.contains("corrected"));
  assert(correctedControlData.aileronStartRib == 5);
  assert(correctedControlData.aileronStopRib == 6);
  WingPanelData unequalSharedWidths;
  unequalSharedWidths.ribCount = 7;
  unequalSharedWidths.flaps = true;
  unequalSharedWidths.flapStartRib = 2;
  unequalSharedWidths.flapStopRib = 4;
  unequalSharedWidths.flapWidth = 40.0;
  unequalSharedWidths.ailerons = true;
  unequalSharedWidths.aileronStartRib = 4;
  unequalSharedWidths.aileronStopRib = 7;
  unequalSharedWidths.aileronWidth = 35.0;
  WingPanelEditor sharedWidthEditor{unequalSharedWidths};
  QString sharedWidthError;
  assert(!sharedWidthEditor.validate(sharedWidthError));
  assert(sharedWidthError.contains("must match"));
  assert(std::abs(sharedWidthEditor.data().flapWidth -
                  sharedWidthEditor.data().aileronWidth) < 1.0e-8);
  WingPanelData innerSparPanel;
  innerSparPanel.panelSpan = 700.0;
  innerSparPanel.rootChord = 240.0;
  innerSparPanel.tipChord = 200.0;
  innerSparPanel.sweep = 0.0;
  innerSparPanel.topSpar = true;
  WingPanelData outerSparPanel;
  outerSparPanel.panelSpan = 350.0;
  outerSparPanel.rootChord = 200.0;
  outerSparPanel.tipChord = 180.0;
  outerSparPanel.sweep = 0.0;
  outerSparPanel.topSpar = true;
  outerSparPanel.centerSparWoodJoiner = true;
  assert(woodJoinerSparAlignmentError({innerSparPanel, outerSparPanel}).isEmpty());
  outerSparPanel.sweep = 20.0;
  assert(!woodJoinerSparAlignmentError({innerSparPanel, outerSparPanel}).isEmpty());
  outerSparPanel.joinerPanelMode = 0;
  assert(woodJoinerSparAlignmentError({innerSparPanel, outerSparPanel}).isEmpty());
  outerSparPanel.joinerPanelMode = -1;
  WingPanelData sweptCenterPanel = innerSparPanel;
  sweptCenterPanel.centerSparWoodJoiner = true;
  // A swept leading edge is valid when taper places the 25%-chord spar
  // perpendicular to the centerline, so its mirror is the same straight line.
  sweptCenterPanel.sweep = 0.25 *
      (sweptCenterPanel.rootChord - sweptCenterPanel.tipChord);
  assert(woodJoinerSparAlignmentError({sweptCenterPanel}).isEmpty());
  sweptCenterPanel.sweep += 1.0;
  assert(!woodJoinerSparAlignmentError({sweptCenterPanel}).isEmpty());
  WingPanelData nineInchCenter;
  nineInchCenter.panelSpan = 20.0 * 25.4;
  nineInchCenter.rootChord = nineInchCenter.tipChord = 9.0 * 25.4;
  nineInchCenter.sweep = 0.0;
  nineInchCenter.topSpar = true;
  WingPanelData eightInchTip = nineInchCenter;
  eightInchTip.rootChord = 9.0 * 25.4;
  eightInchTip.tipChord = 8.0 * 25.4;
  eightInchTip.centerSparWoodJoiner = true;
  eightInchTip.sweep = 0.25 * 25.4;
  assert(woodJoinerSparAlignmentError({nineInchCenter, eightInchTip}).isEmpty());
  eightInchTip.sweep = 1.0 * 25.4;
  assert(!woodJoinerSparAlignmentError({nineInchCenter, eightInchTip}).isEmpty());
  WingPanelData legacySheetStops;
  legacySheetStops.leTopSheetStopRib = legacySheetStops.leBottomSheetStopRib = 1;
  legacySheetStops.teTopSheetStopRib = legacySheetStops.teBottomSheetStopRib = 1;
  WingPanelEditor clampedStops{legacySheetStops};
  const auto clampedData = clampedStops.data();
  assert(clampedData.leTopSheetStopRib == 2 && clampedData.leBottomSheetStopRib == 2);
  assert(clampedData.teTopSheetStopRib == 2 && clampedData.teBottomSheetStopRib == 2);
  WingPanelData combinedSheetingAndTurbulators;
  combinedSheetingAndTurbulators.leTopSheet = true;
  combinedSheetingAndTurbulators.turbulators = true;
  WingPanelEditor combinedSheetingEditor{combinedSheetingAndTurbulators};
  assert(combinedSheetingEditor.data().leTopSheet);
  assert(combinedSheetingEditor.data().turbulators);

  designrc::domain::StructuredWing planWing;
  designrc::domain::WingParameters planParameters;
  planParameters.halfSpan = 700.0;
  planParameters.rootChord = 250.0;
  planParameters.tipChord = 150.0;
  planParameters.sweep = 50.0;
  planParameters.dihedralDegrees = 5.7;
  planParameters.ribCount = 2;
  const auto planRibs = designrc::domain::generateRibs(
      planParameters, designrc::domain::AirfoilProfile::nacaSymmetric(0.12),
      designrc::domain::AirfoilProfile::nacaSymmetric(0.10));
  for (const auto& rib : planRibs)
    planWing.ribs.push_back({rib, {}, {}, {}, {}, {}});
  planWing.ribs[0].name = "R1";
  planWing.ribs[1].name = "R2";
  const auto planDocument = buildFlattenedWingPlan(
      {planWing}, {3.0}, {WingPanelData{}}, false, "testproject.designrc");
  assert(!planDocument.empty());
  assert(planDocument.pageBoundsMm.width() > 700.0);
  assert(planDocument.pageBoundsMm.height() > 500.0);
  assert(planDocument.texts.size() >= 5);
  bool hasDihedral = false;
  bool hasTipTwist = false;
  bool hasRootCutaway = false;
  bool hasPanelSpanBetweenHalves = false;
  bool hasReducedDimensionText = false;
  bool hasReducedRibText = false;
  bool hasUnchangedTitleText = false;
  for (const auto& text : planDocument.texts) {
    hasDihedral = hasDihedral || text.text.contains("DIHEDRAL 1:");
    hasTipTwist = hasTipTwist || text.text.contains("TIP TWIST 1:");
    hasRootCutaway = hasRootCutaway || text.text == "R1 CUTAWAY";
    hasPanelSpanBetweenHalves = hasPanelSpanBetweenHalves ||
        (text.text.startsWith("PANEL 1 SPAN:") &&
         text.position.y() > 262.0 && text.position.y() < 312.0);
    hasReducedDimensionText = hasReducedDimensionText ||
        (text.text.startsWith("FLATTENED HALF-SPAN:") &&
         std::abs(text.heightMm - 2.625) < 1.0e-8);
    hasReducedRibText = hasReducedRibText ||
        (text.text == "R1" && std::abs(text.heightMm - 2.25) < 1.0e-8);
    hasUnchangedTitleText = hasUnchangedTitleText ||
        (text.text.startsWith("PROJECT:") &&
         std::abs(text.heightMm - 3.5) < 1.0e-8);
    assert(!text.text.contains("LEFT WING - FLATTENED"));
    assert(!text.text.contains("RIGHT WING - FLATTENED"));
    assert(!text.text.contains("Thickness"));
  }
  assert(hasDihedral);
  assert(hasTipTwist);
  assert(hasRootCutaway);
  assert(hasPanelSpanBetweenHalves);
  assert(hasReducedDimensionText);
  assert(hasReducedRibText);
  assert(hasUnchangedTitleText);

  auto sparLeaderWing = planWing;
  designrc::domain::SpanMember planSpar;
  planSpar.name = "Spar 1";
  planSpar.kind = designrc::domain::SpanMemberKind::Rectangular;
  planSpar.width = 8.0;
  planSpar.height = 6.0;
  planSpar.centers = {{62.5, 0.0}, {37.5, 0.0}};
  sparLeaderWing.members.push_back(planSpar);
  const auto sparLeaderPlan = buildFlattenedWingPlan(
      {sparLeaderWing}, {3.0}, {WingPanelData{}}, false,
      "spar-leader.designrc");
  const double flattenedBayCenter = 0.5 * std::hypot(
      planRibs.back().spanPosition - planRibs.front().spanPosition,
      planRibs.back().dihedralHeight - planRibs.front().dihedralHeight);
  bool sparLeaderTargetsBay = false;
  for (const auto& path : sparLeaderPlan.paths) {
    if (path.fill != QColor{62, 52, 45} || path.path.elementCount() < 3)
      continue;
    sparLeaderTargetsBay = sparLeaderTargetsBay ||
        std::abs(path.path.elementAt(0).x - flattenedBayCenter) < 1.0e-8;
  }
  assert(sparLeaderTargetsBay);

  designrc::domain::StructureParameters ribletPlanStructure;
  ribletPlanStructure.leadingEdgeType = 3;
  ribletPlanStructure.leadingEdgeTubeOd = 4.0;
  ribletPlanStructure.leadingEdgeTubeId = 3.0;
  ribletPlanStructure.spars = {
      {30, 2, 1, 0, 5.0, 9.0, 6.0, 5.0, 6.0, 6.0, 1.0}};
  ribletPlanStructure.riblets = true;
  ribletPlanStructure.ribletStartRib = 2;
  ribletPlanStructure.ribletEndRib = 5;
  ribletPlanStructure.ribletsPerBay = 2;
  auto ribletPlanParameters = planParameters;
  ribletPlanParameters.ribCount = 6;
  const auto ribletPlanRibs = designrc::domain::generateRibs(
      ribletPlanParameters,
      designrc::domain::AirfoilProfile::nacaSymmetric(0.12),
      designrc::domain::AirfoilProfile::nacaSymmetric(0.10));
  auto ribletPlanWing = testsupport::buildStructure(
      ribletPlanRibs, ribletPlanStructure);
  for (std::size_t index = 0; index < ribletPlanWing.ribs.size(); ++index)
    ribletPlanWing.ribs[index].name = "R" + std::to_string(index + 1);
  designrc::domain::addRiblets(
      ribletPlanWing, ribletPlanStructure);
  const auto ribletPlan = buildFlattenedWingPlan(
      {ribletPlanWing}, {3.0}, {WingPanelData{}}, false,
      "riblets.designrc");
  assert(std::any_of(
      ribletPlan.texts.begin(), ribletPlan.texts.end(),
      [](const auto& text) { return text.text == "R2a"; }));
  assert(std::count_if(
      ribletPlan.paths.begin(), ribletPlan.paths.end(),
      [](const auto& path) {
        return path.fill == QColor{212, 189, 165, 105};
      }) >= static_cast<std::ptrdiff_t>(
          2 * (ribletPlanWing.ribs.size() +
               ribletPlanWing.riblets.size())));

  auto spoilerPlanParameters = planParameters;
  spoilerPlanParameters.dihedralDegrees = 0.0;
  spoilerPlanParameters.ribCount = 7;
  auto spoilerPlanRibs = designrc::domain::generateRibs(
      spoilerPlanParameters,
      designrc::domain::AirfoilProfile::nacaSymmetric(0.12),
      designrc::domain::AirfoilProfile::nacaSymmetric(0.10));
  spoilerPlanRibs.front().ribThicknessStartFactor = 0.0;
  spoilerPlanRibs.back().ribThicknessStartFactor = -1.0;
  designrc::domain::StructureParameters spoilerPlanStructure;
  spoilerPlanStructure.spoilers = true;
  spoilerPlanStructure.spoilerStartRib = 1;
  spoilerPlanStructure.spoilerEndRib = 5;
  spoilerPlanStructure.spoilerLighteningHoles = true;
  spoilerPlanStructure.spoilerMinimumWoodMargin = 6.0;
  spoilerPlanStructure.spoilerMinimumCircleDistance = 12.0;
  const auto spoilerPlanWing = testsupport::buildStructure(
      spoilerPlanRibs, spoilerPlanStructure);
  WingPanelData spoilerPanelData;
  spoilerPanelData.spoilers = true;
  spoilerPanelData.spoilerStartRib = 1;
  spoilerPanelData.spoilerEndRib = 5;
  const auto spoilerPlan = buildFlattenedWingPlan(
      {spoilerPlanWing}, {spoilerPlanParameters.ribThickness},
      {spoilerPanelData}, false, "center-spoiler.designrc");
  std::size_t lowerCenterEnds = 0;
  std::size_t upperCenterEnds = 0;
  for (const auto& path : spoilerPlan.paths) {
    if (path.fill != QColor{212, 189, 165, 105}) continue;
    const auto bounds = path.path.boundingRect();
    if (std::abs(bounds.left()) < 1.0e-8) ++lowerCenterEnds;
    if (std::abs(bounds.right() - spoilerPlanParameters.halfSpan) < 1.0e-8)
      ++upperCenterEnds;
  }
  assert(lowerCenterEnds >= 3);
  assert(upperCenterEnds >= 3);
  assert(std::any_of(spoilerPlan.paths.begin(), spoilerPlan.paths.end(),
      [](const auto& path) {
        return path.fill == QColor{255, 255, 255, 255} &&
               path.path.elementCount() > 20;
      }));

  PlanViewport pdfViewport;
  pdfViewport.setDocument(planDocument);
  const QString requestedPdfPath = qEnvironmentVariable("DESIGNRC_PDF_TEST_OUTPUT");
  const bool preservePdf = !requestedPdfPath.isEmpty();
  const QString pdfPath = preservePdf ? requestedPdfPath
      : QDir::temp().filePath("designrc_plan_export_test.pdf");
  QString pdfError;
  assert(pdfViewport.exportPdf(pdfPath, pdfError));
  QFile pdfFile{pdfPath};
  assert(pdfFile.open(QIODevice::ReadOnly));
  assert(pdfFile.read(5) == "%PDF-");
  pdfFile.close();
  if (!preservePdf) QFile::remove(pdfPath);

  {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString dxfPath = directory.filePath("Full plan.dxf");
    QString error;
    assert(pdfViewport.exportDxf(dxfPath, error));
    QFile file{dxfPath};
    assert(file.open(QIODevice::ReadOnly));
    const auto contents = file.readAll().replace("\r\n", "\n");
    assert(contents.contains("$INSUNITS\n 70\n4\n"));
    assert(contents.contains("PLAN_GEOMETRY") && contents.contains("PLAN_ANNOTATION"));
    assert(contents.endsWith("EOF\n"));
    assert(contents.contains("AC1018"));
    // Autodesk's DXF translator rejects R2004 classes missing instance counts.
    const auto classStart = contents.indexOf("  2\nCLASSES\n");
    assert(classStart >= 0);
    const auto classEnd = contents.indexOf("  0\nENDSEC\n", classStart);
    assert(classEnd > classStart);
    const auto classes = contents.mid(classStart, classEnd - classStart);
    assert(classes.count("  0\nCLASS\n") > 0);
    assert(classes.count(" 91\n") == classes.count("  0\nCLASS\n"));
    assert(classes.contains("VISUALSTYLE\n  2\nAcDbVisualStyle\n  3\nObjectDBX Classes\n"
                            " 90\n4095\n 91\n25\n"));
    assert(contents.contains("420\n4076589\n")); // plan outline RGB(62,52,45)
    assert(contents.contains("420\n3618615\n")); // label RGB(55,55,55)
    file.close();
    const auto exportedPlanPath = qEnvironmentVariable("DESIGNRC_PLAN_DXF_TEST_OUTPUT");
    if (!exportedPlanPath.isEmpty()) assert(QFile::copy(dxfPath, exportedPlanPath));

    // Known CAD coordinates: preserve physical size and independent open
    // subpaths, including a leader; do not close or connect them together.
    TechnicalDrawingDocument fixture;
    fixture.pageBoundsMm = QRectF{-20, -30, 200, 100};
    QPainterPath border;
    border.addRect(QRectF{-10, -20, 180, 80});
    fixture.paths.push_back({border, QColor{200, 100, 50}, QColor{212, 189, 165, 105}});
    QPainterPath leaders;
    leaders.moveTo(0, 0); leaders.lineTo(25.4, 0);
    leaders.moveTo(50, 10); leaders.lineTo(60, 20);
    fixture.paths.push_back({leaders, QColor{0, 0, 255}});
    QPainterPath curve;
    curve.moveTo(0, 20); curve.cubicTo(10, 0, 20, 40, 30, 20);
    fixture.paths.push_back({curve, Qt::black});
    fixture.texts.push_back({{80, 20}, "AR1\n1.5 mm", 3.0, -90});
    PlanViewport viewport;
    viewport.setDocument(fixture);
    assert(viewport.exportDxf(dxfPath, error));
    assert(file.open(QIODevice::ReadOnly));
    const auto fixtureDxf = file.readAll().replace("\r\n", "\n");
    file.close();
    // The border is 180 x 80 mm, with CAD Y pointing up from the page bottom.
    assert(fixtureDxf.contains("90\n4\n70\n1\n10\n10.000000\n20\n90.000000\n"
                               "10\n190.000000\n20\n90.000000\n"
                               "10\n190.000000\n20\n10.000000\n"
                               "10\n10.000000\n20\n10.000000\n"));
    assert(fixtureDxf.contains("10\n20.000000\n20\n70.000000\n30\n0.0\n"
                               "11\n45.400000\n21\n70.000000\n31\n0.0\n"));
    assert(fixtureDxf.contains("10\n70.000000\n20\n60.000000\n30\n0.0\n"
                               "11\n80.000000\n21\n50.000000\n31\n0.0\n"));
    // Curves and rotated multiline text are CAD geometry, never font entities.
    assert(fixtureDxf.contains("8\nPLAN_GEOMETRY\n420\n13132850\n100\nAcDbPolyline\n"));
    assert(fixtureDxf.count("8\nPLAN_GEOMETRY\n420\n255\n100\nAcDbLine\n") == 2);
    assert(fixtureDxf.contains("8\nPLAN_ANNOTATION\n420\n3618615\n100\nAcDbPolyline\n"));
    assert(fixtureDxf.count("\nLINE\n") > 10);
    assert(fixtureDxf.count("\nLWPOLYLINE\n") > 4);
    assert(!fixtureDxf.contains("\nTEXT\n") && !fixtureDxf.contains("\nMTEXT\n"));
    assert(!viewport.exportDxf(directory.filePath("missing/plan.dxf"), error));
    assert(!error.isEmpty());
    const QString svgPath = directory.filePath("Full plan.svg");
    assert(pdfViewport.exportSvg(svgPath, error));
    QFile svgFile{svgPath};
    assert(svgFile.open(QIODevice::ReadOnly));
    const auto planSvg = svgFile.readAll();
    svgFile.close();
    const auto requestedSvgPath = qEnvironmentVariable("DESIGNRC_PLAN_SVG_TEST_OUTPUT");
    if (!requestedSvgPath.isEmpty()) assert(QFile::copy(svgPath, requestedSvgPath));
    assert(planSvg.contains("#3e342d") && planSvg.contains("#373737"));
    assert(viewport.exportSvg(svgPath, error));
    assert(svgFile.open(QIODevice::ReadOnly));
    QXmlStreamReader svg{svgFile.readAll()};
    svgFile.close();
    bool sawSize = false, sawBorder = false, sawLeader = false;
    bool sawCurve = false, sawLabel = false;
    int pathCount = 0;
    while (!svg.atEnd()) {
      svg.readNext();
      if (!svg.isStartElement()) continue;
      const auto attributes = svg.attributes();
      if (svg.name() == QLatin1StringView{"svg"}) {
        assert(svg.namespaceUri() == QLatin1StringView{"http://www.w3.org/2000/svg"});
        assert(attributes.value("width") == QLatin1StringView{"200mm"});
        assert(attributes.value("height") == QLatin1StringView{"100mm"});
        assert(attributes.value("viewBox") == QLatin1StringView{"-20 -30 200 100"});
        sawSize = true;
      }
      assert(svg.name() != QLatin1StringView{"text"}); // outlined, font-independent labels
      if (svg.name() != QLatin1StringView{"path"}) continue;
      ++pathCount;
      const auto data = attributes.value("d").toString();
      if (attributes.value("stroke") == QLatin1StringView{"#c86432"}) {
        assert(data == "M-10 -20 L170 -20 L170 60 L-10 60 Z");
        assert(attributes.value("fill") == QLatin1StringView{"#d4bda5"});
        assert(std::abs(attributes.value("fill-opacity").toDouble() - 105.0 / 255.0) < 1e-6);
        sawBorder = true;
      }
      if (attributes.value("stroke") == QLatin1StringView{"#0000ff"}) {
        assert(data == "M0 0 L25.4 0 M50 10 L60 20");
        assert(attributes.value("fill") == QLatin1StringView{"none"});
        sawLeader = true;
      }
      if (data == "M0 20 C10 0 20 40 30 20") sawCurve = true;
      if (attributes.value("fill") == QLatin1StringView{"#373737"}) {
        assert(!data.isEmpty());
        sawLabel = true;
      }
    }
    assert(!svg.hasError());
    assert(pathCount == 4 && sawSize && sawBorder && sawLeader && sawCurve && sawLabel);
    assert(!viewport.exportSvg(directory.filePath("missing/plan.svg"), error));
    viewport.clearPlan();
    assert(!viewport.exportDxf(dxfPath, error));
    assert(!viewport.exportSvg(svgPath, error));
  }

  auto turbulatorCutawayWing = planWing;
  designrc::domain::SpanMember cutawayTurbulator;
  cutawayTurbulator.name = "Turbulator 1";
  cutawayTurbulator.kind = designrc::domain::SpanMemberKind::Turbulator;
  cutawayTurbulator.width = 3.0;
  cutawayTurbulator.height = 2.0;
  cutawayTurbulator.centers = {{35.0, 8.0}, {22.0, 5.0}};
  turbulatorCutawayWing.members.push_back(cutawayTurbulator);
  const auto turbulatorCutawayPlan = buildFlattenedWingPlan(
      {turbulatorCutawayWing}, {3.0}, {WingPanelData{}}, false,
      "testproject.designrc");
  // Two plan-view bands plus one closed four-sided R1 cutaway profile.
  assert(turbulatorCutawayPlan.paths.size() >= planDocument.paths.size() + 3);
  bool hasSquareCutawayTurbulator = false;
  for (const auto& path : turbulatorCutawayPlan.paths) {
    if (path.fill == Qt::transparent &&
        path.path.boundingRect().left() > planParameters.halfSpan &&
        path.path.elementCount() == 5) {
      hasSquareCutawayTurbulator = true;
      break;
    }
  }
  assert(hasSquareCutawayTurbulator);

  auto truncatedTeParameters = planParameters;
  truncatedTeParameters.ribCount = 3;
  const auto truncatedTeRibs = designrc::domain::generateRibs(
      truncatedTeParameters, designrc::domain::AirfoilProfile::nacaSymmetric(0.12),
      designrc::domain::AirfoilProfile::nacaSymmetric(0.10));
  designrc::domain::StructuredWing truncatedTeWing;
  for (const auto& rib : truncatedTeRibs)
    truncatedTeWing.ribs.push_back({rib, {}, {}, {}, {}, {}});
  designrc::domain::ProfiledSpanMember truncatedTe;
  truncatedTe.name = "TE1";
  truncatedTe.profiles = {
      {{230.0, -2.0}, {250.0, -2.0}, {250.0, 2.0}, {230.0, 2.0}},
      {{180.0, -2.0}, {200.0, -2.0}, {200.0, 2.0}, {180.0, 2.0}},
      {{130.0, -2.0}, {150.0, -2.0}, {150.0, 2.0}, {130.0, 2.0}}};
  truncatedTe.activeRanges = {{0, 1}};
  truncatedTeWing.profiledMembers.push_back(truncatedTe);
  const auto truncatedTePlan = buildFlattenedWingPlan(
      {truncatedTeWing}, {3.0}, {WingPanelData{}}, false,
      "testproject.designrc");
  const double truncatedBoundary = std::hypot(
      truncatedTeRibs[1].spanPosition - truncatedTeRibs[0].spanPosition,
      truncatedTeRibs[1].dihedralHeight - truncatedTeRibs[0].dihedralHeight);
  bool teReachesBoundaryRibFace = false;
  for (const auto& path : truncatedTePlan.paths) {
    const auto bounds = path.path.boundingRect();
    if (path.fill == QColor{212, 189, 165, 105} &&
        bounds.right() > truncatedBoundary + 1.0 &&
        bounds.center().x() < truncatedBoundary) {
      teReachesBoundaryRibFace = true;
      break;
    }
  }
  assert(teReachesBoundaryRibFace);

  auto shearWebPlanWing = planWing;
  designrc::domain::ShearWebPart planWeb;
  planWeb.name = "SW1";
  planWeb.bayIndex = 1; // The bay bounded by rib indices 0 and 1.
  planWeb.thickness = 4.0;
  planWeb.stationCorners = {
      {62.5, -10.0}, {37.5, -8.0}, {37.5, 8.0}, {62.5, 10.0}};
  shearWebPlanWing.shearWebs.push_back(planWeb);
  const auto shearWebPlanDocument = buildFlattenedWingPlan(
      {shearWebPlanWing}, {3.0}, {WingPanelData{}}, false,
      "testproject.designrc");
  // A single web adds one centered, face-to-face outline to each wing half.
  assert(shearWebPlanDocument.paths.size() >= planDocument.paths.size() + 2);

  auto sheetingPlanWing = planWing;
  designrc::domain::SheetingPart planSheeting;
  planSheeting.name = "TE top sheeting";
  planSheeting.stopRibIndex = 1;
  planSheeting.profiles = {
      {{40.0, 0.0}, {80.0, 0.0}, {80.0, 2.0}, {40.0, 2.0}},
      {{-10.0, 0.0}, {30.0, 0.0}, {30.0, 2.0}, {-10.0, 2.0}}};
  sheetingPlanWing.sheeting.push_back(planSheeting);
  const auto sheetingPlanDocument = buildFlattenedWingPlan(
      {sheetingPlanWing}, {3.0}, {WingPanelData{}}, false,
      "testproject.designrc");
  const double sheetingTipStation = std::hypot(
      planRibs.back().spanPosition - planRibs.front().spanPosition,
      planRibs.back().dihedralHeight - planRibs.front().dihedralHeight);
  bool sheetingReachesTipFace = false;
  for (const auto& path : sheetingPlanDocument.paths) {
    if (path.fill == QColor{212, 189, 165, 65} &&
        path.path.boundingRect().right() > sheetingTipStation + 1.0) {
      sheetingReachesTipFace = true;
      break;
    }
  }
  assert(sheetingReachesTipFace);
  bool sheetingLeaderTargetsBay = false;
  for (const auto& path : sheetingPlanDocument.paths) {
    if (path.fill != QColor{62, 52, 45} || path.path.elementCount() < 3)
      continue;
    sheetingLeaderTargetsBay = sheetingLeaderTargetsBay ||
        std::abs(path.path.elementAt(0).x - flattenedBayCenter) < 1.0e-8;
  }
  assert(sheetingLeaderTargetsBay);

  auto sheetingAndSparPlanWing = sheetingPlanWing;
  sheetingAndSparPlanWing.members.push_back(planSpar);
  const auto sheetingAndSparPlan = buildFlattenedWingPlan(
      {sheetingAndSparPlanWing}, {3.0}, {WingPanelData{}}, false,
      "sheeting-leader-clearance.designrc");
  double lowerSheetingTargetY = std::numeric_limits<double>::lowest();
  for (const auto& path : sheetingAndSparPlan.paths) {
    if (path.fill == QColor{62, 52, 45} && path.path.elementCount() >= 3 &&
        std::abs(path.path.elementAt(0).x - flattenedBayCenter) < 1.0e-8)
      lowerSheetingTargetY = std::max(
          lowerSheetingTargetY, path.path.elementAt(0).y);
  }
  assert(lowerSheetingTargetY > std::numeric_limits<double>::lowest());
  bool targetClearOfLowerSpar = false;
  for (const auto& path : sheetingAndSparPlan.paths) {
    if (path.fill != QColor{212, 189, 165, 105}) continue;
    const auto sparBounds = path.path.boundingRect();
    if (!sparBounds.contains({flattenedBayCenter, sparBounds.center().y()}))
      continue;
    if (lowerSheetingTargetY < sparBounds.top() - 1.0 ||
        lowerSheetingTargetY > sparBounds.bottom() + 1.0)
      targetClearOfLowerSpar = true;
  }
  assert(targetClearOfLowerSpar);

  auto outerPlanWing = planWing;
  const double jointY = planRibs.back().spanPosition;
  const double jointZ = planRibs.back().dihedralHeight;
  const double jointX = planRibs.back().leadingEdgeOffset;
  for (auto& rib : outerPlanWing.ribs) {
    rib.rib.spanPosition += jointY;
    rib.rib.dihedralHeight += jointZ;
    rib.rib.leadingEdgeOffset += jointX;
  }
  designrc::domain::JoinerPart panelJointWood;
  panelJointWood.name = "J2";
  panelJointWood.kind = designrc::domain::SpanMemberKind::Rectangular;
  panelJointWood.rectangularProfiles = {
      {{{48.0, -8.0}, {52.0, -8.0}, {52.0, 8.0}, {48.0, 8.0}}},
      {{{38.0, -7.0}, {42.0, -7.0}, {42.0, 7.0}, {38.0, 7.0}}}};
  const double innerY = planRibs.front().spanPosition +
      0.8 * (planRibs.back().spanPosition - planRibs.front().spanPosition);
  const double innerZ = planRibs.front().dihedralHeight +
      0.8 * (planRibs.back().dihedralHeight - planRibs.front().dihedralHeight);
  panelJointWood.innerRectangularProfiles = {
      {{{68.0, innerY, innerZ}, {72.0, innerY, innerZ},
        {72.0, innerY, innerZ}, {68.0, innerY, innerZ}}},
      {{{68.0, jointY, jointZ}, {72.0, jointY, jointZ},
        {72.0, jointY, jointZ}, {68.0, jointY, jointZ}}}};
  outerPlanWing.joiners.push_back(panelJointWood);
  const auto twoPanelBaseline = buildFlattenedWingPlan(
      {planWing, outerPlanWing}, {3.0, 3.0},
      {WingPanelData{}, WingPanelData{}}, false, "testproject.designrc");
  outerPlanWing.joiners.clear();
  const auto twoPanelWithoutJoiner = buildFlattenedWingPlan(
      {planWing, outerPlanWing}, {3.0, 3.0},
      {WingPanelData{}, WingPanelData{}}, false, "testproject.designrc");
  // Each plan half gains both the inner-panel and outer-panel wood-joiner band.
  assert(twoPanelBaseline.paths.size() >= twoPanelWithoutJoiner.paths.size() + 4);
  assert(std::any_of(twoPanelBaseline.texts.begin(), twoPanelBaseline.texts.end(),
      [](const auto& text) { return text.text.contains("J2 joiner"); }));
  assert(std::none_of(twoPanelBaseline.texts.begin(), twoPanelBaseline.texts.end(),
      [](const auto& text) { return text.text.contains("(Panel "); }));

  auto centerJoinerWing = planWing;
  designrc::domain::JoinerPart centerJoiner;
  centerJoiner.name = "CF tube joiner behind mid spar";
  centerJoiner.kind = designrc::domain::SpanMemberKind::Tube;
  centerJoiner.outerDiameter = 7.0;
  centerJoiner.innerDiameter = 6.0;
  centerJoiner.hasExplicitEndpoints = true;
  centerJoiner.annotateOnBothPlanHalves = true;
  centerJoiner.innerEndpoint = {75.0, -18.0, 0.0};
  centerJoiner.outerEndpoint = {75.0, 18.0, 0.0};
  centerJoinerWing.joiners.push_back(centerJoiner);
  const auto centerJoinerPlan = buildFlattenedWingPlan(
      {centerJoinerWing}, {3.0}, {WingPanelData{}}, false,
      "testproject.designrc");
  std::vector<TechnicalDrawingText> centerJoinerAnnotations;
  for (const auto& text : centerJoinerPlan.texts) {
    if (text.text.startsWith("CF tube joiner\n"))
      centerJoinerAnnotations.push_back(text);
    assert(!text.text.contains("behind mid spar", Qt::CaseInsensitive));
  }
  assert(centerJoinerAnnotations.size() == 2);
  assert(std::all_of(centerJoinerAnnotations.begin(),
                     centerJoinerAnnotations.end(),
      [](const auto& text) {
        return std::abs(text.heightMm - 2.625) < 1.0e-8;
      }));
  assert(centerJoinerAnnotations[0].position.y() !=
         centerJoinerAnnotations[1].position.y());

  auto removableAnnotationWing = planWing;
  designrc::domain::JoinerPart removablePart = centerJoiner;
  removablePart.name = "Removable Joiner 1 This Panel Steel";
  removablePart.annotationName = "Joiner 1\nSteel Rod";
  removablePart.kind = designrc::domain::SpanMemberKind::Rod;
  removablePart.innerDiameter = 0.0;
  removableAnnotationWing.joiners.push_back(removablePart);
  designrc::domain::JoinerPart pinHolePart = removablePart;
  pinHolePart.name = "Alignment Pin 1 CF";
  pinHolePart.annotationName = "Alignment Pin 1\nCF Pin";
  removableAnnotationWing.joiners.push_back(pinHolePart);
  designrc::domain::JoinerPart panelJointPinPart = pinHolePart;
  panelJointPinPart.name = "Alignment Pin 2 Steel";
  panelJointPinPart.annotationName = "Alignment Pin 2\nSteel Pin";
  panelJointPinPart.annotateOnBothPlanHalves = false;
  removableAnnotationWing.joiners.push_back(panelJointPinPart);
  const auto removableAnnotationPlan = buildFlattenedWingPlan(
      {removableAnnotationWing}, {3.0}, {WingPanelData{}}, false,
      "testproject.designrc");
  bool hasRodRole = false;
  bool hasPinMaterial = false;
  std::size_t centerPinAnnotationCount = 0;
  std::size_t panelJointPinAnnotationCount = 0;
  for (const auto& text : removableAnnotationPlan.texts) {
    assert(!text.text.contains("Removable", Qt::CaseInsensitive));
    assert(!text.text.contains("This Panel", Qt::CaseInsensitive));
    assert(!text.text.contains("Adjoining Panel", Qt::CaseInsensitive));
    hasRodRole = hasRodRole ||
        (text.text.contains("Joiner 1\nSteel Rod") &&
         text.text.count('\n') == 2);
    hasPinMaterial = hasPinMaterial ||
        (text.text.contains("Alignment Pin 1\nCF Pin") &&
         text.text.count('\n') == 2);
    if (text.text.startsWith("Alignment Pin 1\n"))
      ++centerPinAnnotationCount;
    if (text.text.startsWith("Alignment Pin 2\n"))
      ++panelJointPinAnnotationCount;
  }
  assert(hasRodRole && hasPinMaterial);
  assert(centerPinAnnotationCount == 2);
  assert(panelJointPinAnnotationCount == 1);

  designrc::domain::ControlSurfacePart planAileron;
  planAileron.name = "Aileron";
  planAileron.startRibIndex = 0;
  planAileron.stopRibIndex = 1;
  planAileron.hingePostWidth = 6.0;
  planAileron.hingePostHeight = 10.0;
  planAileron.profiles = {
      {{200.0, -5.0}, {235.0, -5.0}, {235.0, 5.0}, {200.0, 5.0}},
      {{120.0, -5.0}, {155.0, -5.0}, {155.0, 5.0}, {120.0, 5.0}}};
  planAileron.hingePostCenters = {{197.0, 0.0}, {117.0, 0.0}};
  planWing.controlSurfaces.push_back(planAileron);
  designrc::domain::SpanMember carbonLeadingEdge;
  carbonLeadingEdge.name = "LE1";
  carbonLeadingEdge.kind = designrc::domain::SpanMemberKind::Tube;
  carbonLeadingEdge.width = carbonLeadingEdge.height = 6.0;
  carbonLeadingEdge.innerDiameter = 5.0;
  carbonLeadingEdge.centers = {{3.0, 0.0}, {3.0, 0.0}};
  planWing.members.push_back(carbonLeadingEdge);
  WingPanelData mixedUnitPlanParameters;
  mixedUnitPlanParameters.ailerons = true;
  mixedUnitPlanParameters.leadingEdgeType = 3;
  mixedUnitPlanParameters.unitOverrides.insert(
      "rootChord", UnitOverride::Millimeters);
  mixedUnitPlanParameters.unitOverrides.insert(
      "aileronWidth", UnitOverride::Millimeters);
  mixedUnitPlanParameters.unitOverrides.insert(
      "aileronHingePostWidth", UnitOverride::Millimeters);
  mixedUnitPlanParameters.unitOverrides.insert(
      "leadingEdgeTubeOd", UnitOverride::Millimeters);
  mixedUnitPlanParameters.unitOverrides.insert(
      "panelSpan", UnitOverride::Millimeters);
  const auto controlPlanDocument = buildFlattenedWingPlan(
      {planWing}, {3.0}, {mixedUnitPlanParameters}, true,
      "testproject.designrc");
  // Each half gains a CF LE, an aileron outline, and a hinge-post outline.
  assert(controlPlanDocument.paths.size() >= planDocument.paths.size() + 6);
  bool hasMetricRootChord = false;
  bool hasMixedUnitAileron = false;
  bool hasCarbonLeadingEdge = false;
  bool hasMetricPanelSpan = false;
  bool hasTipRibLabelInside = false;
  const double flatTipStation = std::hypot(
      planRibs.back().spanPosition - planRibs.front().spanPosition,
      planRibs.back().dihedralHeight - planRibs.front().dihedralHeight);
  const double tipRibInnerEdge = flatTipStation +
      planRibs.back().ribThicknessStartFactor * 3.0;
  for (const auto& text : controlPlanDocument.texts) {
    hasMetricRootChord = hasMetricRootChord ||
        (text.text.contains("ROOT CHORD:") && text.text.contains("mm"));
    hasMixedUnitAileron = hasMixedUnitAileron ||
        (text.text.contains("Aileron") && text.text.contains("35 mm x") &&
         text.text.contains("Hinge Post: 6 mm x") && text.text.contains("in"));
    hasCarbonLeadingEdge = hasCarbonLeadingEdge ||
        (text.text.contains("LE1") && text.text.contains("CF Tube") &&
         text.text.contains("OD: 6 mm"));
    hasMetricPanelSpan = hasMetricPanelSpan ||
        (text.text.contains("PANEL 1 SPAN:") && text.text.contains("mm"));
    hasTipRibLabelInside = hasTipRibLabelInside ||
        (text.text == "R2" &&
         text.position.x() < tipRibInnerEdge - 2.0);
  }
  assert(hasMetricRootChord);
  assert(hasMixedUnitAileron);
  assert(hasCarbonLeadingEdge);
  assert(hasMetricPanelSpan);
  assert(hasTipRibLabelInside);
  designrc::domain::PartDrawing pdfPartOne{
      "PDF Rib", {{{{0.0, 0.0}, {600.0, 0.0}, {600.0, 20.0}, {0.0, 20.0}},
                    "RIB_OUTLINE"}},
      {{0.0, 0.0}, {600.0, 0.0}, {600.0, 20.0}, {0.0, 20.0}}, {}};
  designrc::domain::PartDrawing pdfPartTwo{
      "PDF Web", {{{{0.0, 0.0}, {45.0, 0.0}, {45.0, 12.0}, {0.0, 12.0}},
                    "SHEAR_WEB_OUTLINE"}},
      {{0.0, 0.0}, {45.0, 0.0}, {45.0, 12.0}, {0.0, 12.0}}, {}};
  const auto allPartsPdfPath = std::filesystem::temp_directory_path() /
      "designrc_all_parts_test.pdf";
  exportPartsPdf({pdfPartOne, pdfPartTwo}, allPartsPdfPath);
  std::ifstream pdf{allPartsPdfPath, std::ios::binary};
  std::string pdfHeader(5, '\0');
  pdf.read(pdfHeader.data(), static_cast<std::streamsize>(pdfHeader.size()));
  assert(pdfHeader == "%PDF-");
  pdf.close();
  assert(std::filesystem::file_size(allPartsPdfPath) > 500);
  std::ifstream fullPdf{allPartsPdfPath, std::ios::binary};
  const std::string pdfContents{
      std::istreambuf_iterator<char>{fullPdf}, {}};
  std::size_t pageObjectCount = 0;
  std::size_t pagePosition = 0;
  while ((pagePosition = pdfContents.find("/Type /Page", pagePosition)) !=
         std::string::npos) {
    ++pageObjectCount;
    pagePosition += 10;
  }
  // Composite PDF export uses one custom poster page, plus the /Pages tree.
  assert(pageObjectCount == 2);
  fullPdf.close();
  if (!qEnvironmentVariableIsSet("DESIGNRC_KEEP_TEST_PDF"))
    std::filesystem::remove(allPartsPdfPath);
  return 0;
}
