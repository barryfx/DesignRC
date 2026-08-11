#include "gui/TechnicalDrawing.h"
#include "gui/PlanViewport.h"
#include "gui/PartPdfExporter.h"
#include "gui/WingPanelEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontMetrics>
#include <QLabel>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

int main(int argc, char* argv[]) {
  QApplication application{argc, argv};
  using namespace designrc::gui;

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
  assert(spacingTabs->tabText(2) == "Spars");
  assert(spacingTabs->tabText(3) == "Sheeting");
  assert(spacingTabs->tabText(4) == "Joiners");
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
  assert(sparTabs->tabText(2) == "Spars");
  assert(sparTabs->tabText(3) == "Sheeting");
  sparTabs->setCurrentIndex(2);
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
  sparTabs->setCurrentIndex(3);
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
  QCheckBox* topTaper = nullptr;
  QCheckBox* bottomTaper = nullptr;
  bool hasRearTopLabel = false;
  bool hasRearBottomLabel = false;
  for (auto* check : teSheetingEditor.findChildren<QCheckBox*>()) {
    if (check->text() == "Taper Sheeting") {
      if (!topTaper) topTaper = check;
      else if (!bottomTaper) bottomTaper = check;
    }
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
             installedInches.removableJoiners[0].adjoiningSleeveOd - 7.0) <
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
  auto ribletPlanWing = designrc::domain::applyWingStructure(
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
  const auto spoilerPlanWing = designrc::domain::applyWingStructure(
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
