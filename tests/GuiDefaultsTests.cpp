#include "gui/WingPanelEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>

#include <cassert>
#include <cmath>

int main(int argc, char* argv[]) {
  QApplication application{argc, argv};
  using namespace designrc::gui;

  LengthInput thickness{"testThickness", 3.0};
  thickness.setGlobalUnit(DisplayUnit::Inches);
  auto* spin = thickness.findChild<QDoubleSpinBox*>();
  assert(spin != nullptr);
  assert(spin->text() == "1/8 in");
  spin->stepUp();
  assert(spin->text() == "5/32 in");
  assert(std::abs(thickness.valueMm() - 5.0 / 32.0 * 25.4) < 1.0e-8);

  thickness.setValueMm(0.75 * 25.4);
  assert(spin->text() == "0.75 in");
  spin->stepUp();
  assert(spin->text() == "0.76 in");
  thickness.setValueMm(1.75 * 25.4);
  assert(spin->text() == "1.75 in");

  const auto inchDefaults = roundedInchPanelData(WingPanelData{});
  assert(std::abs(inchDefaults.panelSpan / 25.4 - 27.5) < 1.0e-8);
  assert(std::abs(inchDefaults.rootChord / 25.4 - 9.5) < 1.0e-8);
  assert(std::abs(inchDefaults.tipChord / 25.4 - 6.0) < 1.0e-8);
  assert(std::abs(inchDefaults.ribThickness / 25.4 - 0.125) < 1.0e-8);
  assert(std::abs(inchDefaults.leadingEdgeTubeId / 25.4 - 1.0 / 32.0) < 1.0e-8);
  assert(installedDefaultDisplayUnit() == DisplayUnit::Inches);
  const auto installedMetric = installedDefaultPanelData(DisplayUnit::Millimeters);
  assert(installedMetric.leadingEdgeType == 1);
  assert(installedMetric.trailingEdgeType == 2);
  assert(installedMetric.unitOverrides.value("leadingEdgeWidth") == UnitOverride::Inches);
  const auto installedInches = installedDefaultPanelData(DisplayUnit::Inches);
  assert(std::abs(installedInches.panelSpan / 25.4 - 27.0) < 1.0e-8);
  assert(std::abs(installedInches.rootChord / 25.4 - 10.0) < 1.0e-8);
  assert(std::abs(installedInches.tipChord / 25.4 - 6.0) < 1.0e-8);
  assert(std::abs(installedInches.sweep / 25.4 - 1.0) < 1.0e-8);
  assert(installedInches.topSpar && installedInches.bottomSpar);
  assert(installedInches.centerSparWoodJoiner);
  assert(woodJoinerSparAlignmentError({installedInches}).isEmpty());
  assert(installedInches.leadingEdgeType == 2 && installedInches.trailingEdgeType == 2);
  assert(installedInches.behindSparJoinerType == 3);
  WingPanelData controls;
  controls.aileronHingePostWidth = 7.0;
  controls.flapHingePostHeight = 11.0;
  controls.addRib1a = true;
  controls.centerSparWoodJoiner = true;
  controls.behindSparJoiner = true;
  controls.behindSparJoinerType = 3;
  controls.behindSparJoinerOd = 7.0;
  const auto restored = panelDataFromJson(panelDataToJson(controls));
  assert(std::abs(restored.aileronHingePostWidth - 7.0) < 1.0e-8);
  assert(std::abs(restored.flapHingePostHeight - 11.0) < 1.0e-8);
  assert(restored.addRib1a && restored.centerSparWoodJoiner);
  assert(restored.behindSparJoiner && restored.behindSparJoinerType == 3);
  assert(std::abs(restored.behindSparJoinerOd - 7.0) < 1.0e-8);
  WingPanelEditor outerPanel{WingPanelData{}, DisplayUnit::Millimeters, false, true};
  auto* tabs = outerPanel.findChild<QTabWidget*>();
  assert(tabs != nullptr);
  bool foundJoinerTab = false;
  for (int i = 0; i < tabs->count(); ++i)
    if (tabs->tabText(i) == "Joiner") foundJoinerTab = true;
  assert(foundJoinerTab);
  WingPanelData inheritedPanelData;
  inheritedPanelData.addRib1a = true;
  WingPanelEditor inheritedChordPanel{
      inheritedPanelData, DisplayUnit::Millimeters, false, true, false};
  auto* hiddenRootChord = inheritedChordPanel.findChild<LengthInput*>("rootChord");
  assert(hiddenRootChord != nullptr && hiddenRootChord->isHidden());
  QCheckBox* addRib1a = nullptr;
  for (auto* box : inheritedChordPanel.findChildren<QCheckBox*>())
    if (box->text() == "Add Rib 1a") addRib1a = box;
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
  assert(internalControlData.aileronStartRib == 2 && internalControlData.aileronStopRib == 6);
  assert(internalControlData.flapStartRib == 2 && internalControlData.flapStopRib == 6);
  for (const auto* name : {"aileronStartRib", "aileronStopRib", "flapStartRib", "flapStopRib"}) {
    const auto* control = internalControls.findChild<QSpinBox*>(name);
    assert(control != nullptr && control->minimum() == 2 && control->maximum() == 6);
  }
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
  return 0;
}
