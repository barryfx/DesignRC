#pragma once

#include "domain/AirfoilProfile.h"

#include <QJsonObject>
#include <QHash>
#include <QString>
#include <QWidget>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QResizeEvent;
class QSpinBox;

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

struct WingPanelData {
  double panelSpan{700.0};
  double rootChord{240.0};
  double tipChord{150.0};
  double sweep{70.0};
  double dihedral{4.0};
  double twist{0.0};
  double ribThickness{3.0};
  int ribCount{9};
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

  bool addRib1a{false};
  bool centerSparWoodJoiner{false};
  bool behindSparJoiner{false}; int behindSparJoinerType{0};
  double behindSparJoinerOd{6.0}; double behindSparJoinerId{5.0};
  bool fiftyPercentJoiner{false}; int fiftyPercentJoinerType{0};
  double fiftyPercentJoinerOd{6.0}; double fiftyPercentJoinerId{5.0};
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
  void setGlobalUnit(DisplayUnit unit);
  [[nodiscard]] bool validate(QString& error);

signals:
  void changed();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  QWidget* makeSpecsPage();
  QWidget* makeSparsPage();
  QWidget* makeLeadingTrailingPage();
  QWidget* makeControlsPage();
  QWidget* makeJoinerPage();
  void importAirfoil(bool root);
  void updateAngleInputWidths();
  void updateRibSpacing();
  void updateConditionalControls();
  void emitChanged();

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

  QCheckBox *addRib1a_{}, *centerSparWoodJoiner_{}, *behindSparJoiner_{}, *fiftyPercentJoiner_{};
  QRadioButton *behindJoinerRod_{}, *behindJoinerCfTube_{},
      *behindJoinerAlTube_{}, *fiftyJoinerRod_{}, *fiftyJoinerCfTube_{}, *fiftyJoinerAlTube_{};
  QLabel* centerSparJoinerLabel_{};
  QWidget *behindJoinerTypeDetails_{}, *behindJoinerRodDetails_{}, *behindJoinerTubeDetails_{},
      *fiftyJoinerTypeDetails_{}, *fiftyJoinerRodDetails_{}, *fiftyJoinerTubeDetails_{};
  LengthInput *behindJoinerOd_{}, *behindJoinerId_{}, *fiftyJoinerOd_{}, *fiftyJoinerId_{};
};

} // namespace designrc::gui
