#include "gui/WingPanelEditor.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QTabWidget>
#include <cassert>

void testSheetingBoundaryControls() {
  using namespace designrc::gui;
  for (auto unit : {DisplayUnit::Millimeters, DisplayUnit::Inches}) {
    WingPanelEditor editor{installedDefaultPanelData(unit), unit};
    auto* tabs = editor.findChild<QTabWidget*>();
    for (int i = 0; i < tabs->count(); ++i) if (tabs->tabText(i) == "Sheeting") tabs->setCurrentIndex(i);
    editor.resize(800, 1200); editor.show(); QApplication::processEvents();
    for (const QString prefix : {"leTopSheet", "leBottomSheet", "teTopSheet", "teBottomSheet"}) {
      auto* enabled = editor.findChild<QCheckBox*>(prefix);
      auto* chord = editor.findChild<QRadioButton*>(prefix + "StopChordLocationMode");
      auto* spar = editor.findChild<QRadioButton*>(prefix + "UpToSpar");
      auto* mid = editor.findChild<QRadioButton*>(prefix + "MidSpar");
      auto* cover = editor.findChild<QCheckBox*>(prefix + "CoverRibs");
      auto* percent = editor.findChild<QDoubleSpinBox*>(prefix + "StopChordPercent");
      assert(enabled && chord && spar && mid && cover && percent);
      enabled->setChecked(true); QApplication::processEvents();
      assert(spar->isChecked() && !percent->isEnabled());
      chord->click();
      assert(chord->isChecked() && !spar->isChecked() && !mid->isChecked() && percent->isEnabled());
      percent->setValue(37.25);
      mid->click(); cover->setChecked(true);
      assert(mid->isChecked() && !chord->isChecked() && !spar->isChecked() && !percent->isEnabled());
      assert(chord->mapTo(&editor, QPoint{}).y() < spar->mapTo(&editor, QPoint{}).y());
      assert(spar->mapTo(&editor, QPoint{}).y() < mid->mapTo(&editor, QPoint{}).y());
      assert(mid->mapTo(&editor, QPoint{}).y() < cover->mapTo(&editor, QPoint{}).y());
      assert(chord->mapTo(&editor, QPoint{}).x() == spar->mapTo(&editor, QPoint{}).x());
      assert(chord->mapTo(&editor, QPoint{}).x() > enabled->mapTo(&editor, QPoint{}).x());
    }
    const auto json = panelDataToJson(editor.data());
    WingPanelEditor reopened{panelDataFromJson(json), unit};
    WingPanelEditor defaults{panelDataFromJson(json), unit, true};
    for (auto* view : {&reopened, &defaults}) for (const QString prefix : {"leTopSheet", "leBottomSheet", "teTopSheet", "teBottomSheet"}) {
      assert(view->findChild<QRadioButton*>(prefix + "MidSpar")->isChecked());
      assert(view->findChild<QCheckBox*>(prefix + "CoverRibs")->isChecked());
      assert(view->findChild<QDoubleSpinBox*>(prefix + "StopChordPercent")->value() == 37.25);
    }
    QJsonObject legacy;
    legacy.insert("leTopSheetUpToSpar", false);
    legacy.insert("leTopSheetStopChordPercent", 42.5);
    WingPanelEditor old{panelDataFromJson(legacy), unit};
    assert(old.findChild<QRadioButton*>("leTopSheetStopChordLocationMode")->isChecked());
    assert(old.findChild<QCheckBox*>("leTopSheetCoverRibs")->isChecked());
    assert(old.data().leTopSheetStopChordPercent == 42.5);
  }
}
