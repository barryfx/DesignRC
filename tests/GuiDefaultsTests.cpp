#include "gui/TechnicalDrawing.h"
#include "gui/PlanViewport.h"
#include "gui/WingPanelEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontMetrics>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>

#include <cassert>
#include <cmath>

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
  assert(spacing->text() == "87.50 mm");
  assert(spacing->findChild<QDoubleSpinBox*>() == nullptr);
  spacingEditor.findChild<QSpinBox*>("ribCount")->setValue(8);
  assert(spacing->text() == "100.00 mm");
  spacingEditor.setGlobalUnit(DisplayUnit::Inches);
  assert(spacing->text() == "3.93701 in");
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
  QJsonObject legacyShapedStock;
  legacyShapedStock.insert("leadingEdgeType", 1);
  legacyShapedStock.insert("trailingEdgeType", 1);
  const auto migratedStock = panelDataFromJson(legacyShapedStock);
  assert(migratedStock.leadingEdgeType == 2);
  assert(migratedStock.trailingEdgeType == 2);
  assert(std::abs(installedMetric.leadingEdgeHeight - 15.875) < 1.0e-8);
  assert(std::abs(installedMetric.trailingEdgeHeight - 9.525) < 1.0e-8);
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
  assert(std::abs(installedInches.leadingEdgeHeight / 25.4 - 0.625) < 1.0e-8);
  assert(std::abs(installedInches.trailingEdgeHeight / 25.4 - 0.375) < 1.0e-8);
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
  for (const auto* button : outerPanel.findChildren<QRadioButton*>()) {
    assert(button->text() != "Shaped LE Stock");
    assert(button->text() != "Shaped TE Stock");
  }
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
  for (const auto& text : planDocument.texts) {
    hasDihedral = hasDihedral || text.text.contains("DIHEDRAL 1:");
    hasTipTwist = hasTipTwist || text.text.contains("TIP TWIST 1:");
    hasRootCutaway = hasRootCutaway || text.text == "R1 CUTAWAY";
    hasPanelSpanBetweenHalves = hasPanelSpanBetweenHalves ||
        (text.text.startsWith("PANEL 1 SPAN:") &&
         text.position.y() > 262.0 && text.position.y() < 312.0);
    assert(!text.text.contains("LEFT WING - FLATTENED"));
    assert(!text.text.contains("RIGHT WING - FLATTENED"));
    assert(!text.text.contains("Thickness"));
  }
  assert(hasDihedral);
  assert(hasTipTwist);
  assert(hasRootCutaway);
  assert(hasPanelSpanBetweenHalves);

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
      {{50.0, 0.0}, {100.0, 0.0}, {100.0, 2.0}, {50.0, 2.0}},
      {{40.0, 0.0}, {80.0, 0.0}, {80.0, 2.0}, {40.0, 2.0}}};
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
  return 0;
}
