#pragma once

#include "domain/AirfoilProfile.h"

#include <QJsonObject>
#include <QHash>
#include <QString>
#include <QWidget>
#include <vector>

class QCheckBox;
class QButtonGroup;
class QDoubleSpinBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QRadioButton;
class QPushButton;
class QResizeEvent;
class QSpinBox;
class QVBoxLayout;

namespace designrc::gui {

class FractionalSpinBox;

enum class DisplayUnit { Millimeters = 0, Inches = 1 };
enum class UnitOverride { Global = 0, Millimeters = 1, Inches = 2 };

[[nodiscard]] constexpr DisplayUnit installedDefaultDisplayUnit() {
  return DisplayUnit::Inches;
}

class LengthInput final : public QWidget {
  Q_OBJECT
public:
  explicit LengthInput(const QString& key, double valueMm, QWidget* parent = nullptr);
  [[nodiscard]] double valueMm() const;
  void setValueMm(double value);
  void setGlobalUnit(DisplayUnit unit);
  void setUnitOverride(UnitOverride unit);
  [[nodiscard]] UnitOverride unitOverride() const;
  void setOverrideSelectorVisible(bool visible);
  [[nodiscard]] int measurementFieldWidth() const;
signals:
  void valueChanged();
private:
  void refreshDisplay();
  QString key_;
  double valueMm_{};
  DisplayUnit globalUnit_{DisplayUnit::Millimeters};
  FractionalSpinBox* spin_{};
  QComboBox* unit_{};
  bool refreshing_{false};
};

struct SparDefaults {
  int chordLocationPercent{25};
  int verticalLocation{2}; // 0 = top, 1 = bottom, 2 = mid
  int material{1}; // 0 = wood, 1 = CF
  int type{0}; // 0 = tube, 1 = rod, 2 = strip
  double woodHeight{5.0};
  double woodWidth{9.0};
  double tubeOd{6.0};
  double tubeId{5.0};
  double rodOd{6.0};
  double stripWidth{6.0};
  double stripThickness{1.0};
  double shearWebThickness{3.0};
};

struct FixedJoinerData {
  int chordLocationPercent{35};
  int material{1}; // 0 wood, 1 CF, 2 steel rod
  double woodThickness{3.175};
  int carbonType{0}; // 0 tube, 1 rod
  double carbonTubeOd{6.0};
  double carbonTubeId{5.0};
  double carbonRodOd{6.0};
  double steelRodOd{6.0};
  UnitOverride woodThicknessUnit{UnitOverride::Global};
  UnitOverride carbonTubeOdUnit{UnitOverride::Global};
  UnitOverride carbonTubeIdUnit{UnitOverride::Global};
  UnitOverride carbonRodOdUnit{UnitOverride::Global};
  UnitOverride steelRodOdUnit{UnitOverride::Global};
};

struct RemovableJoinerData {
  int kind{0}; // 0 sleeve/rod joiner, 1 alignment pin
  int chordLocationPercent{35};
  int thisPanelPart{1}; // 0 sleeve, 1 rod
  int adjoiningPanelPart{0}; // 0 sleeve, 1 rod
  int thisRodMaterial{0}; // 0 CF, 1 steel
  double thisRodOd{6.0};
  int thisSleeveMaterial{1}; // 0 CF, 1 aluminum, 2 steel, 3 fiberglass
  double thisSleeveOd{7.0};
  int adjoiningRodMaterial{1};
  double adjoiningRodOd{6.0};
  int adjoiningSleeveMaterial{1};
  double adjoiningSleeveOd{7.0};
  int alignmentMode{1}; // 0 sleeve/pin, 1 pin/hole
  int pinHoleThisPart{0}; // 0 pin, 1 hole
  int pinMaterial{0};
  double pinOd{2.0};
  UnitOverride thisRodOdUnit{UnitOverride::Global};
  UnitOverride thisSleeveOdUnit{UnitOverride::Global};
  UnitOverride adjoiningRodOdUnit{UnitOverride::Global};
  UnitOverride adjoiningSleeveOdUnit{UnitOverride::Global};
  UnitOverride pinOdUnit{UnitOverride::Global};
};

struct WingPanelData {
  double panelSpan{700.0};
  double rootChord{240.0};
  double tipChord{150.0};
  double sweep{70.0};
  double dihedral{4.0};
  double twist{0.0};
  double ribThickness{3.0};
  int ribCount{9};
  bool ribLighteningHoles{false};
  int ribLighteningStartRib{3};
  int ribLighteningStopRib{0};
  double ribLighteningMinimumWoodMargin{6.0};
  double ribLighteningMinimumHoleDistance{12.0};
  bool riblets{false};
  int ribletStartRib{2};
  int ribletEndRib{0};
  int ribletsPerBay{2};
  QString rootAirfoilPath;
  QString tipAirfoilPath;
  domain::AirfoilProfile rootAirfoil{domain::AirfoilProfile::nacaSymmetric(0.15)};
  domain::AirfoilProfile tipAirfoil{domain::AirfoilProfile::nacaSymmetric(0.10)};

  bool topSpar{false}; double topSparHeight{5.0}; double topSparWidth{10.0};
  bool bottomSpar{false}; double bottomSparHeight{5.0}; double bottomSparWidth{10.0};
  bool shearWebs{false}; double shearWebWidth{3.0};
  int carbonSpar{0}; double cfTubeOd{6.0}; double cfTubeId{5.0}; double cfRodOd{6.0};
  bool leTopSheet{false}; double leTopSheetThickness{2.0}; int leTopSheetStopRib{2};
  bool leBottomSheet{false}; double leBottomSheetThickness{2.0}; int leBottomSheetStopRib{2};
  bool teTopSheet{false}; double teTopSheetThickness{2.0}; int teTopSheetStopRib{2};
  bool teBottomSheet{false}; double teBottomSheetThickness{2.0}; int teBottomSheetStopRib{2};
  bool turbulators{false}; int turbulatorCount{1}; double turbulatorHeight{2.0}; double turbulatorWidth{2.0};
  bool topRearSpar{false}; double topRearSparHeight{4.0}; double topRearSparWidth{4.0};
  bool bottomRearSpar{false}; double bottomRearSparHeight{4.0}; double bottomRearSparWidth{4.0};
  SparDefaults sparDefaults;
  std::vector<SparDefaults> spars{SparDefaults{}};
  bool sparShearWebs{false};

  int leadingEdgeType{0}; double leadingEdgeWidth{5.0}; double leadingEdgeHeight{7.0};
  double leadingEdgeTubeOd{2.0}; double leadingEdgeTubeId{1.0}; double leadingEdgeRodOd{2.0};
  int trailingEdgeType{0}; double trailingEdgeWidth{20.0}; double trailingEdgeHeight{3.0};
  bool slottedForRibs{false};

  bool ailerons{false}; double aileronWidth{35.0}; double aileronHeight{10.0};
  int aileronStartRib{2}; int aileronStopRib{8};
  double aileronHingePostWidth{6.0}; double aileronHingePostHeight{10.0};
  bool flaps{false}; double flapWidth{40.0}; double flapHeight{10.0};
  int flapStartRib{2}; int flapStopRib{5};
  double flapHingePostWidth{6.0}; double flapHingePostHeight{10.0};

  bool spoilers{false};
  int spoilerStartRib{3}; int spoilerEndRib{7};
  int spoilerChordLocationPercent{30};
  double spoilerWidth{25.4}; double spoilerThickness{3.0};
  double spoilerFrameRailWidth{6.0}; double spoilerSupportRailHeight{3.0};
  bool spoilerLighteningHoles{false};
  double spoilerMinimumWoodMargin{6.0};
  double spoilerMinimumCircleDistance{12.0};

  bool wiringHoles{false};
  int wiringHoleStartRib{2}; int wiringHoleEndRib{0};
  int wiringHoleChordLocationPercent{50};
  double wiringHoleWidth{9.525}; double wiringHoleHeight{6.35};

  bool addRib1a{false};
  bool centerSparWoodJoiner{false};
  bool behindSparJoiner{false}; int behindSparJoinerType{0};
  double behindSparJoinerOd{6.0}; double behindSparJoinerId{5.0};
  bool fiftyPercentJoiner{false}; int fiftyPercentJoinerType{0};
  double fiftyPercentJoinerOd{6.0}; double fiftyPercentJoinerId{5.0};
  int joinerPanelMode{-1}; // -1 unselected, 0 removable, 1 fixed
  std::vector<FixedJoinerData> fixedJoiners;
  std::vector<RemovableJoinerData> removableJoiners;
  QHash<QString, UnitOverride> unitOverrides;
};

QJsonObject panelDataToJson(const WingPanelData& data);
WingPanelData panelDataFromJson(const QJsonObject& object);
[[nodiscard]] WingPanelData roundedInchPanelData(const WingPanelData& metricData);
[[nodiscard]] WingPanelData installedDefaultPanelData(DisplayUnit unit);
[[nodiscard]] QString woodJoinerSparAlignmentError(
    const std::vector<WingPanelData>& panels);

class WingPanelEditor final : public QWidget {
  Q_OBJECT
public:
  explicit WingPanelEditor(const WingPanelData& data = {}, DisplayUnit globalUnit = DisplayUnit::Millimeters,
                           bool showUnitOverrides = false, bool showJoinerPage = true,
                           bool showRootChord = true,
                           QWidget* parent = nullptr);

  [[nodiscard]] WingPanelData data() const;
  void setData(const WingPanelData& data);
  void setJoinerAddDefaults(const WingPanelData& defaults);
  void setGlobalUnit(DisplayUnit unit);
  [[nodiscard]] bool validate(QString& error);

signals:
  void changed();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  QWidget* makeSpecsPage();
  QWidget* makeRibsPage();
  QWidget* makeSparsPage();
  QWidget* makeSheetingPage();
  QWidget* makeLeadingTrailingPage();
  QWidget* makeControlsPage();
  QWidget* makeSpoilersPage();
  QWidget* makeWiringHolesSection(const QString& suffix);
  QWidget* makeJoinerPage();
  void importAirfoil(bool root);
  void updateAngleInputWidths();
  void updateRibSpacing();
  void updateConditionalControls();
  void synchronizeWiringHoleControls(std::size_t sourceIndex);
  void updateWiringHoleRibRanges(bool preserveLastRib = false);
  void addSparEditor();
  void deleteSelectedSparEditors();
  void renumberSparEditors();
  void updateSparEditorControls();
  [[nodiscard]] bool ribletsAvailable() const;
  void selectJoinerMode(int mode, bool confirmDeletion);
  void addFixedJoinerEditor(const FixedJoinerData& data = {});
  void addRemovableJoinerEditor(int kind, const RemovableJoinerData& data = {});
  void deleteSelectedJoinerEditors();
  void clearJoinerEditors();
  void renumberJoinerEditors();
  void updateJoinerEditorControls();
  void emitChanged();

  struct SparEditorWidgets {
    QWidget* container{};
    QCheckBox* deleteSelection{};
    QSpinBox* chordLocation{};
    QRadioButton *top{}, *bottom{}, *mid{};
    QRadioButton *wood{}, *carbonFiber{};
    QRadioButton *tube{}, *rod{}, *strip{};
    QWidget *woodDetails{}, *typeDetails{}, *tubeDetails{}, *rodDetails{}, *stripDetails{};
    LengthInput *woodHeight{}, *woodWidth{};
    LengthInput *tubeOd{}, *tubeId{}, *rodOd{}, *stripWidth{}, *stripThickness{};
  };
  void applySparDefaults(SparEditorWidgets& row);
  void applySparData(SparEditorWidgets& row, const SparDefaults& spar);

  struct FixedJoinerWidgets {
    QWidget* container{};
    QCheckBox* deleteSelection{};
    QSpinBox* chordLocation{};
    QRadioButton *wood{}, *carbonFiber{}, *steelRod{}, *tube{}, *rod{};
    QWidget *woodDetails{}, *carbonDetails{}, *tubeDetails{}, *rodDetails{}, *steelDetails{};
    LengthInput *woodThickness{}, *tubeOd{}, *tubeId{}, *rodOd{}, *steelOd{};
  };
  struct RemovableJoinerWidgets {
    QWidget* container{};
    QCheckBox* deleteSelection{};
    int kind{};
    QSpinBox* chordLocation{};
    QRadioButton *sleeve{}, *rod{}, *adjoiningSleeve{}, *adjoiningRod{};
    QRadioButton *alignmentSleevePin{}, *alignmentPinHole{};
    QRadioButton *pinHolePin{}, *pinHoleHole{};
    QWidget *sleeveRodDetails{}, *alignmentSleevePinDetails{}, *alignmentPinHoleDetails{};
    QWidget *thisSleeveDetails{}, *thisRodDetails{}, *adjoiningSleeveDetails{},
        *adjoiningRodDetails{}, *pinDetails{};
    QButtonGroup *thisSleeveMaterial{}, *thisRodMaterial{}, *adjoiningSleeveMaterial{},
        *adjoiningRodMaterial{}, *pinMaterial{};
    LengthInput *thisSleeveOd{}, *thisRodOd{}, *adjoiningSleeveOd{},
        *adjoiningRodOd{}, *pinOd{};
  };

  WingPanelData airfoilData_;
  DisplayUnit globalUnit_{DisplayUnit::Millimeters};
  bool showUnitOverrides_{false};
  bool showJoinerPage_{true};
  bool showRootChord_{true};
  QHash<QString, LengthInput*> lengths_;
  QLabel* rootName_{};
  QLabel* tipName_{};
  QLabel* rootChordLabel_{};
  LengthInput* span_{};
  LengthInput* rootChord_{};
  LengthInput* tipChord_{};
  LengthInput* sweep_{};
  QDoubleSpinBox* dihedral_{};
  QDoubleSpinBox* twist_{};
  LengthInput* ribThickness_{};
  QSpinBox* ribCount_{};
  QLabel* ribSpacing_{};
  QCheckBox* ribLighteningHoles_{};
  QWidget* ribLighteningHoleDetails_{};
  QSpinBox *ribLighteningStartRib_{}, *ribLighteningStopRib_{};
  LengthInput *ribLighteningMinimumWoodMargin_{},
      *ribLighteningMinimumHoleDistance_{};
  QCheckBox* riblets_{};
  QWidget* ribletDetails_{};
  QSpinBox *ribletStartRib_{}, *ribletEndRib_{}, *ribletsPerBay_{};

  QCheckBox *topSpar_{}, *bottomSpar_{}, *shearWebs_{}, *leTopSheet_{}, *leBottomSheet_{},
      *teTopSheet_{}, *teBottomSheet_{}, *turbulators_{}, *topRearSpar_{}, *bottomRearSpar_{};
  QWidget *topSparDetails_{}, *bottomSparDetails_{}, *shearDetails_{}, *cfTubeDetails_{}, *cfRodDetails_{},
      *leTopSheetDetails_{}, *leBottomSheetDetails_{}, *teTopSheetDetails_{}, *teBottomSheetDetails_{},
      *turbulatorDetails_{}, *topRearDetails_{}, *bottomRearDetails_{};
  QRadioButton *cfTube_{}, *cfRod_{};
  LengthInput *topSparHeight_{}, *topSparWidth_{}, *bottomSparHeight_{}, *bottomSparWidth_{},
      *shearWebWidth_{}, *cfTubeOd_{}, *cfTubeId_{}, *cfRodOd_{}, *leTopSheetThickness_{},
      *leBottomSheetThickness_{}, *teTopSheetThickness_{}, *teBottomSheetThickness_{},
      *turbulatorHeight_{}, *turbulatorWidth_{}, *topRearHeight_{}, *topRearWidth_{},
      *bottomRearHeight_{}, *bottomRearWidth_{};
  QSpinBox *leTopSheetStopRib_{}, *leBottomSheetStopRib_{}, *teTopSheetStopRib_{},
      *teBottomSheetStopRib_{}, *turbulatorCount_{};

  QVBoxLayout* sparEditorsLayout_{};
  std::vector<SparEditorWidgets> sparEditors_;
  QCheckBox* sparShearWebs_{};
  QWidget* sparShearWebDetails_{};
  LengthInput* sparShearWebThickness_{};
  int nextSparEditorId_{1};

  QRadioButton *blockLe_{}, *tubeLe_{}, *rodLe_{}, *sheetTe_{};
  QWidget *stockLeDetails_{}, *tubeLeDetails_{}, *rodLeDetails_{}, *stockTeDetails_{}, *slottedDetails_{};
  LengthInput *leWidth_{}, *leHeight_{}, *leTubeOd_{}, *leTubeId_{}, *leRodOd_{}, *teWidth_{}, *teHeight_{};
  QCheckBox* slottedForRibs_{};

  QCheckBox *ailerons_{}, *flaps_{};
  QWidget *aileronDetails_{}, *flapDetails_{};
  LengthInput *aileronWidth_{}, *aileronHeight_{}, *aileronHingePostWidth_{},
      *aileronHingePostHeight_{}, *flapWidth_{}, *flapHeight_{},
      *flapHingePostWidth_{}, *flapHingePostHeight_{};
  LengthInput* lastControlWidthEdited_{};
  QSpinBox *aileronStart_{}, *aileronStop_{}, *flapStart_{}, *flapStop_{};

  QCheckBox* spoilers_{};
  QCheckBox* spoilerLighteningHoles_{};
  QWidget* spoilerDetails_{};
  QWidget* spoilerMinimumWoodMarginDetails_{};
  QWidget* spoilerMinimumCircleDistanceDetails_{};
  QSpinBox *spoilerStartRib_{}, *spoilerEndRib_{}, *spoilerChordLocation_{};
  LengthInput *spoilerWidth_{}, *spoilerThickness_{}, *spoilerFrameRailWidth_{},
      *spoilerMinimumWoodMargin_{}, *spoilerMinimumCircleDistance_{};
  QLabel* spoilerSupportRailHeight_{};

  struct WiringHoleWidgets {
    QCheckBox* enabled{};
    QWidget* details{};
    QSpinBox *startRib{}, *endRib{}, *chordLocation{};
    LengthInput *width{}, *height{};
  };
  std::vector<WiringHoleWidgets> wiringHoleWidgets_;

  QCheckBox* addRib1a_{};
  QPushButton *removablePanel_{}, *fixedPanel_{}, *addFixedJoiner_{},
      *addSleeveRodJoiner_{}, *addAlignmentPin_{}, *deleteJoiners_{};
  QWidget *fixedJoinerButtons_{}, *removableJoinerButtons_{};
  QVBoxLayout* joinerEditorsLayout_{};
  std::vector<FixedJoinerWidgets> fixedJoinerEditors_;
  std::vector<RemovableJoinerWidgets> removableJoinerEditors_;
  int joinerMode_{-1};
  bool updatingJoinerMode_{false};
  bool hasFixedJoinerAddDefault_{false};
  bool hasSleeveRodAddDefault_{false};
  bool hasAlignmentPinAddDefault_{false};
  FixedJoinerData fixedJoinerAddDefault_;
  RemovableJoinerData sleeveRodAddDefault_;
  RemovableJoinerData alignmentPinAddDefault_;
};

} // namespace designrc::gui
