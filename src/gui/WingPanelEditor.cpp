#include "gui/WingPanelEditor.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <numbers>

namespace designrc::gui {
namespace {

constexpr double kMmPerInch = 25.4;
constexpr std::array<int, 11> kAllowedFractionThirtySeconds{
    0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16};

double snapToAllowedInches(const double value) {
  const double magnitude = std::abs(value);
  const double base = std::floor(magnitude);
  double closest = magnitude;
  double distance = std::numeric_limits<double>::max();
  for (int wholeOffset = 0; wholeOffset <= 1; ++wholeOffset) {
    const double whole = base + wholeOffset;
    for (const int numerator : kAllowedFractionThirtySeconds) {
      const double candidate = whole + static_cast<double>(numerator) / 32.0;
      const double candidateDistance = std::abs(candidate - magnitude);
      if (candidateDistance < distance) {
        closest = candidate;
        distance = candidateDistance;
      }
    }
  }
  return value < 0.0 ? -closest : closest;
}

double adjacentAllowedInches(const double value, const bool increasing) {
  double result = increasing ? std::numeric_limits<double>::max()
                             : std::numeric_limits<double>::lowest();
  const int maximumWhole = static_cast<int>(std::ceil(std::abs(value))) + 2;
  for (int whole = 0; whole <= maximumWhole; ++whole) {
    for (const int numerator : kAllowedFractionThirtySeconds) {
      const double magnitude = whole + static_cast<double>(numerator) / 32.0;
      for (const double candidate : {magnitude, -magnitude}) {
        if (increasing && candidate > value + 1.0e-8) result = std::min(result, candidate);
        if (!increasing && candidate < value - 1.0e-8) result = std::max(result, candidate);
      }
    }
  }
  return result;
}

bool isAllowedFractionalInches(const double value) {
  return std::abs(snapToAllowedInches(value) - value) < 1.0e-8;
}

QString decimalInchText(const double value) {
  QString result = QString::number(value, 'f', 5);
  while (result.contains('.') && result.endsWith('0')) result.chop(1);
  if (result.endsWith('.')) result.chop(1);
  return result;
}

QString fractionalInchText(const double input) {
  const double value = snapToAllowedInches(input);
  const bool negative = value < -1.0e-8;
  const double magnitude = std::abs(value);
  long long whole = static_cast<long long>(std::floor(magnitude + 1.0e-8));
  int numerator = static_cast<int>(std::llround((magnitude - whole) * 32.0));
  if (numerator == 32) { ++whole; numerator = 0; }
  QString result = negative ? "-" : "";
  if (numerator == 0) return result + QString::number(whole);
  const int divisor = std::gcd(numerator, 32);
  if (whole > 0) result += QString::number(whole) + " ";
  return result + QString{"%1/%2"}.arg(numerator / divisor).arg(32 / divisor);
}

bool parseInchText(QString text, double& result) {
  text.remove("in", Qt::CaseInsensitive);
  text = text.trimmed();
  if (text.isEmpty()) return false;
  bool decimalOk = false;
  const double decimal = text.toDouble(&decimalOk);
  if (decimalOk) { result = decimal; return true; }
  const auto pieces = text.split(' ', Qt::SkipEmptyParts);
  if (pieces.size() > 2) return false;
  QString fraction = pieces.back();
  const auto fractionParts = fraction.split('/');
  if (fractionParts.size() != 2) return false;
  bool numeratorOk = false;
  bool denominatorOk = false;
  const double numerator = fractionParts[0].toDouble(&numeratorOk);
  const double denominator = fractionParts[1].toDouble(&denominatorOk);
  if (!numeratorOk || !denominatorOk || denominator == 0.0) return false;
  double whole = 0.0;
  if (pieces.size() == 2) {
    bool wholeOk = false;
    whole = pieces[0].toDouble(&wholeOk);
    if (!wholeOk) return false;
  }
  const bool negative = whole < 0.0 || (pieces.size() == 1 && numerator < 0.0);
  result = std::abs(whole) + std::abs(numerator / denominator);
  if (negative) result = -result;
  return true;
}

QDoubleSpinBox* angleInput(double value) {
  auto* input = new QDoubleSpinBox;
  input->setRange(-45.0, 45.0);
  input->setDecimals(1);
  input->setSuffix("°");
  input->setValue(value);
  return input;
}

QWidget* detailRow(std::initializer_list<std::pair<QString, QWidget*>> fields) {
  auto* widget = new QWidget;
  auto* layout = new QHBoxLayout{widget};
  layout->setContentsMargins(20, 2, 0, 4);
  layout->setSpacing(5);
  for (const auto& [label, field] : fields) {
    layout->addWidget(new QLabel{label});
    layout->addWidget(field);
  }
  layout->addStretch();
  return widget;
}

QWidget* scrollPage(QWidget* content) {
  auto* scroll = new QScrollArea;
  scroll->setWidget(content);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  return scroll;
}

QJsonObject unitOverridesToJson(const QHash<QString, UnitOverride>& values) {
  QJsonObject object;
  for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    object.insert(it.key(), static_cast<int>(it.value()));
  return object;
}

void readUnitOverrides(const QJsonObject& object, WingPanelData& data) {
  const auto units = object.value("units").toObject();
  for (auto it = units.begin(); it != units.end(); ++it)
    data.unitOverrides.insert(it.key(), static_cast<UnitOverride>(it.value().toInt()));
}

} // namespace

class FractionalSpinBox final : public QDoubleSpinBox {
public:
  using QDoubleSpinBox::QDoubleSpinBox;

  void setFractionalInches(const bool enabled) {
    fractionalInches_ = enabled;
    setDecimals(enabled ? 5 : 2);
    setSingleStep(enabled ? 1.0 / 32.0 : 1.0);
    setSuffix(enabled ? " in" : " mm");
  }

protected:
  QString textFromValue(const double value) const override {
    if (!fractionalInches_) return QDoubleSpinBox::textFromValue(value);
    return std::abs(value) > 0.5 + 1.0e-8 && !isAllowedFractionalInches(value)
        ? decimalInchText(value) : fractionalInchText(value);
  }

  double valueFromText(const QString& text) const override {
    if (!fractionalInches_) return QDoubleSpinBox::valueFromText(text);
    double parsed = value();
    if (!parseInchText(text, parsed)) return value();
    return std::abs(parsed) > 0.5 + 1.0e-8 ? parsed : snapToAllowedInches(parsed);
  }

  QValidator::State validate(QString& text, int& position) const override {
    if (!fractionalInches_) return QDoubleSpinBox::validate(text, position);
    QString candidate = text;
    candidate.remove("in", Qt::CaseInsensitive);
    candidate = candidate.trimmed();
    if (candidate.isEmpty() || candidate == "-" || candidate.endsWith('/'))
      return QValidator::Intermediate;
    double parsed{};
    return parseInchText(candidate, parsed) ? QValidator::Acceptable : QValidator::Invalid;
  }

  void stepBy(int steps) override {
    if (!fractionalInches_) { QDoubleSpinBox::stepBy(steps); return; }
    double stepped = value();
    if (std::abs(stepped) > 0.5 + 1.0e-8 ||
        (std::abs(stepped - 0.5) < 1.0e-8 && steps > 0)) {
      setValue(stepped + static_cast<double>(steps) * 0.01);
      return;
    }
    while (steps > 0) { stepped = adjacentAllowedInches(stepped, true); --steps; }
    while (steps < 0) { stepped = adjacentAllowedInches(stepped, false); ++steps; }
    setValue(stepped);
  }

private:
  bool fractionalInches_{false};
};

LengthInput::LengthInput(const QString& key, const double valueMm, QWidget* parent)
    : QWidget{parent}, key_{key}, valueMm_{valueMm} {
  setObjectName(key);
  auto* layout = new QHBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(3);
  spin_ = new FractionalSpinBox;
  spin_->setDecimals(2);
  spin_->setRange(-100000.0, 100000.0);
  unit_ = new QComboBox;
  unit_->addItems({"Global", "mm", "in"});
  unit_->setToolTip("Use the global unit or override this length");
  layout->addWidget(spin_, 1);
  layout->addWidget(unit_);
  connect(spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (refreshing_) return;
    const auto effective = unitOverride() == UnitOverride::Global
        ? globalUnit_ : unitOverride() == UnitOverride::Inches
            ? DisplayUnit::Inches : DisplayUnit::Millimeters;
    valueMm_ = effective == DisplayUnit::Inches ? value * kMmPerInch : value;
    emit valueChanged();
  });
  connect(unit_, &QComboBox::currentIndexChanged, this, [this] {
    refreshDisplay();
    emit valueChanged();
  });
  refreshDisplay();
}

double LengthInput::valueMm() const { return valueMm_; }
void LengthInput::setValueMm(const double value) { valueMm_ = value; refreshDisplay(); }
void LengthInput::setGlobalUnit(const DisplayUnit unit) { globalUnit_ = unit; refreshDisplay(); }
void LengthInput::setUnitOverride(const UnitOverride unit) {
  unit_->setCurrentIndex(static_cast<int>(unit));
  refreshDisplay();
}
UnitOverride LengthInput::unitOverride() const {
  return static_cast<UnitOverride>(unit_->currentIndex());
}
void LengthInput::setOverrideSelectorVisible(const bool visible) { unit_->setVisible(visible); }
void LengthInput::refreshDisplay() {
  refreshing_ = true;
  const auto effective = unitOverride() == UnitOverride::Global
      ? globalUnit_ : unitOverride() == UnitOverride::Inches
          ? DisplayUnit::Inches : DisplayUnit::Millimeters;
  spin_->setFractionalInches(effective == DisplayUnit::Inches);
  const double inches = valueMm_ / kMmPerInch;
  spin_->setValue(effective == DisplayUnit::Inches
      ? (std::abs(inches) > 0.5 + 1.0e-8 ? inches : snapToAllowedInches(inches))
      : valueMm_);
  refreshing_ = false;
}

QJsonObject panelDataToJson(const WingPanelData& d) {
  QJsonObject o;
#define PUT(name) o.insert(#name, d.name)
  PUT(panelSpan); PUT(rootChord); PUT(tipChord); PUT(sweep); PUT(dihedral); PUT(twist);
  PUT(ribThickness); PUT(ribCount); PUT(rootAirfoilPath); PUT(tipAirfoilPath);
  PUT(topSpar); PUT(topSparHeight); PUT(topSparWidth); PUT(bottomSpar); PUT(bottomSparHeight);
  PUT(bottomSparWidth); PUT(shearWebs); PUT(shearWebWidth); PUT(carbonSpar); PUT(cfTubeOd);
  PUT(cfTubeId); PUT(cfRodOd); PUT(leTopSheet); PUT(leTopSheetThickness); PUT(leTopSheetStopRib); PUT(leBottomSheet);
  PUT(leBottomSheetThickness); PUT(leBottomSheetStopRib); PUT(teTopSheet); PUT(teTopSheetThickness); PUT(teTopSheetStopRib); PUT(teBottomSheet);
  PUT(teBottomSheetThickness); PUT(teBottomSheetStopRib); PUT(turbulators); PUT(turbulatorCount); PUT(turbulatorHeight);
  PUT(turbulatorWidth); PUT(topRearSpar); PUT(topRearSparHeight); PUT(topRearSparWidth);
  PUT(bottomRearSpar); PUT(bottomRearSparHeight); PUT(bottomRearSparWidth); PUT(leadingEdgeType);
  PUT(leadingEdgeWidth); PUT(leadingEdgeHeight); PUT(leadingEdgeTubeOd); PUT(leadingEdgeTubeId);
  PUT(leadingEdgeRodOd); PUT(trailingEdgeType); PUT(trailingEdgeWidth); PUT(trailingEdgeHeight);
  PUT(slottedForRibs); PUT(ailerons); PUT(aileronWidth); PUT(aileronHeight); PUT(aileronStartRib);
  PUT(aileronStopRib); PUT(aileronHingePostWidth); PUT(aileronHingePostHeight);
  PUT(flaps); PUT(flapWidth); PUT(flapHeight); PUT(flapStartRib); PUT(flapStopRib);
  PUT(flapHingePostWidth); PUT(flapHingePostHeight);
  PUT(addRib1a); PUT(centerSparWoodJoiner); PUT(behindSparJoiner); PUT(behindSparJoinerType);
  PUT(behindSparJoinerOd); PUT(behindSparJoinerId); PUT(fiftyPercentJoiner);
  PUT(fiftyPercentJoinerType); PUT(fiftyPercentJoinerOd); PUT(fiftyPercentJoinerId);
#undef PUT
  o.insert("units", unitOverridesToJson(d.unitOverrides));
  return o;
}

WingPanelData panelDataFromJson(const QJsonObject& o) {
  WingPanelData d;
#define READ_D(name) if (o.contains(#name)) d.name = o.value(#name).toDouble(d.name)
#define READ_I(name) if (o.contains(#name)) d.name = o.value(#name).toInt(d.name)
#define READ_B(name) if (o.contains(#name)) d.name = o.value(#name).toBool(d.name)
  READ_D(panelSpan); READ_D(rootChord); READ_D(tipChord); READ_D(sweep); READ_D(dihedral); READ_D(twist);
  READ_D(ribThickness); READ_I(ribCount);
  d.rootAirfoilPath = o.value("rootAirfoilPath").toString();
  d.tipAirfoilPath = o.value("tipAirfoilPath").toString();
  READ_B(topSpar); READ_D(topSparHeight); READ_D(topSparWidth); READ_B(bottomSpar);
  READ_D(bottomSparHeight); READ_D(bottomSparWidth); READ_B(shearWebs); READ_D(shearWebWidth);
  READ_I(carbonSpar); READ_D(cfTubeOd); READ_D(cfTubeId); READ_D(cfRodOd); READ_B(leTopSheet);
  READ_D(leTopSheetThickness); READ_I(leTopSheetStopRib); READ_B(leBottomSheet); READ_D(leBottomSheetThickness); READ_I(leBottomSheetStopRib); READ_B(teTopSheet);
  READ_D(teTopSheetThickness); READ_I(teTopSheetStopRib); READ_B(teBottomSheet); READ_D(teBottomSheetThickness); READ_I(teBottomSheetStopRib); READ_B(turbulators);
  READ_I(turbulatorCount); READ_D(turbulatorHeight); READ_D(turbulatorWidth); READ_B(topRearSpar);
  READ_D(topRearSparHeight); READ_D(topRearSparWidth); READ_B(bottomRearSpar); READ_D(bottomRearSparHeight);
  READ_D(bottomRearSparWidth); READ_I(leadingEdgeType); READ_D(leadingEdgeWidth); READ_D(leadingEdgeHeight);
  READ_D(leadingEdgeTubeOd); READ_D(leadingEdgeTubeId); READ_D(leadingEdgeRodOd); READ_I(trailingEdgeType);
  READ_D(trailingEdgeWidth); READ_D(trailingEdgeHeight); READ_B(slottedForRibs); READ_B(ailerons);
  READ_D(aileronWidth); READ_D(aileronHeight); READ_I(aileronStartRib); READ_I(aileronStopRib);
  READ_D(aileronHingePostWidth); READ_D(aileronHingePostHeight);
  READ_B(flaps); READ_D(flapWidth); READ_D(flapHeight); READ_I(flapStartRib); READ_I(flapStopRib);
  READ_D(flapHingePostWidth); READ_D(flapHingePostHeight);
  READ_B(addRib1a); READ_B(centerSparWoodJoiner); READ_B(behindSparJoiner);
  READ_I(behindSparJoinerType); READ_D(behindSparJoinerOd); READ_D(behindSparJoinerId);
  READ_B(fiftyPercentJoiner); READ_I(fiftyPercentJoinerType);
  READ_D(fiftyPercentJoinerOd); READ_D(fiftyPercentJoinerId);
#undef READ_D
#undef READ_I
#undef READ_B
  try {
    if (!d.rootAirfoilPath.isEmpty())
      d.rootAirfoil = domain::AirfoilProfile::fromDatFile(d.rootAirfoilPath.toStdWString());
    if (!d.tipAirfoilPath.isEmpty())
      d.tipAirfoil = domain::AirfoilProfile::fromDatFile(d.tipAirfoilPath.toStdWString());
  } catch (...) {
    d.rootAirfoilPath.clear();
    d.tipAirfoilPath.clear();
  }
  readUnitOverrides(o, d);
  return d;
}

WingPanelData roundedInchPanelData(const WingPanelData& metricData) {
  WingPanelData rounded = metricData;
  const auto roundLength = [](double& millimetres) {
    millimetres = snapToAllowedInches(millimetres / kMmPerInch) * kMmPerInch;
  };
#define ROUND_LENGTH(name) roundLength(rounded.name)
  ROUND_LENGTH(panelSpan); ROUND_LENGTH(rootChord); ROUND_LENGTH(tipChord); ROUND_LENGTH(sweep);
  ROUND_LENGTH(ribThickness); ROUND_LENGTH(topSparHeight); ROUND_LENGTH(topSparWidth);
  ROUND_LENGTH(bottomSparHeight); ROUND_LENGTH(bottomSparWidth); ROUND_LENGTH(shearWebWidth);
  ROUND_LENGTH(cfTubeOd); ROUND_LENGTH(cfTubeId); ROUND_LENGTH(cfRodOd);
  ROUND_LENGTH(leTopSheetThickness); ROUND_LENGTH(leBottomSheetThickness);
  ROUND_LENGTH(teTopSheetThickness); ROUND_LENGTH(teBottomSheetThickness);
  ROUND_LENGTH(turbulatorHeight); ROUND_LENGTH(turbulatorWidth);
  ROUND_LENGTH(topRearSparHeight); ROUND_LENGTH(topRearSparWidth);
  ROUND_LENGTH(bottomRearSparHeight); ROUND_LENGTH(bottomRearSparWidth);
  ROUND_LENGTH(leadingEdgeWidth); ROUND_LENGTH(leadingEdgeHeight);
  ROUND_LENGTH(leadingEdgeTubeOd); ROUND_LENGTH(leadingEdgeTubeId); ROUND_LENGTH(leadingEdgeRodOd);
  ROUND_LENGTH(trailingEdgeWidth); ROUND_LENGTH(trailingEdgeHeight);
  ROUND_LENGTH(aileronWidth); ROUND_LENGTH(aileronHeight);
  ROUND_LENGTH(aileronHingePostWidth); ROUND_LENGTH(aileronHingePostHeight);
  ROUND_LENGTH(flapWidth); ROUND_LENGTH(flapHeight);
  ROUND_LENGTH(flapHingePostWidth); ROUND_LENGTH(flapHingePostHeight);
  ROUND_LENGTH(behindSparJoinerOd); ROUND_LENGTH(behindSparJoinerId);
  ROUND_LENGTH(fiftyPercentJoinerOd); ROUND_LENGTH(fiftyPercentJoinerId);
#undef ROUND_LENGTH
  return rounded;
}

WingPanelData installedDefaultPanelData(const DisplayUnit unit) {
  WingPanelData defaults;
  defaults.leadingEdgeType = 1;
  defaults.trailingEdgeType = 2;
  defaults.unitOverrides.insert("cfRodOd", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("cfTubeId", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("cfTubeOd", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("leadingEdgeRodOd", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("leadingEdgeTubeId", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("leadingEdgeTubeOd", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("leadingEdgeWidth", UnitOverride::Inches);
  if (unit == DisplayUnit::Millimeters) return defaults;

  defaults.panelSpan = 685.8;
  defaults.rootChord = 254.0;
  defaults.tipChord = 152.4;
  defaults.sweep = 25.4;
  defaults.ribThickness = 2.38125;
  defaults.topSpar = true;
  defaults.topSparHeight = 4.7625;
  defaults.topSparWidth = 9.525;
  defaults.bottomSpar = true;
  defaults.bottomSparHeight = 4.7625;
  defaults.bottomSparWidth = 9.525;
  defaults.topRearSparHeight = 3.175;
  defaults.topRearSparWidth = 3.175;
  defaults.bottomRearSparHeight = 3.175;
  defaults.bottomRearSparWidth = 3.175;
  defaults.shearWebWidth = 3.175;
  defaults.leTopSheetThickness = 1.5875;
  defaults.leBottomSheetThickness = 1.5875;
  defaults.teTopSheetThickness = 1.5875;
  defaults.teBottomSheetThickness = 1.5875;
  defaults.turbulatorHeight = 3.175;
  defaults.turbulatorWidth = 3.175;
  defaults.leadingEdgeType = 2;
  defaults.leadingEdgeWidth = 4.7625;
  defaults.leadingEdgeHeight = 6.35;
  defaults.trailingEdgeWidth = 25.4;
  defaults.trailingEdgeHeight = 3.175;
  defaults.aileronWidth = 34.925;
  defaults.aileronHeight = 9.525;
  defaults.aileronHingePostWidth = 6.35;
  defaults.aileronHingePostHeight = 9.525;
  defaults.flapWidth = 38.1;
  defaults.flapHeight = 9.525;
  defaults.flapHingePostWidth = 6.35;
  defaults.flapHingePostHeight = 9.525;
  defaults.centerSparWoodJoiner = true;
  defaults.behindSparJoinerType = 3;
  defaults.behindSparJoinerOd = 7.0;
  defaults.behindSparJoinerId = 6.0;
  defaults.fiftyPercentJoinerType = 2;
  defaults.unitOverrides.insert("behindSparJoinerId", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("behindSparJoinerOd", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("fiftyPercentJoinerId", UnitOverride::Millimeters);
  defaults.unitOverrides.insert("fiftyPercentJoinerOd", UnitOverride::Millimeters);
  return defaults;
}

QString woodJoinerSparAlignmentError(const std::vector<WingPanelData>& panels) {
  constexpr double angleToleranceDegrees = 0.05;
  const auto sparAngle = [](const WingPanelData& panel) {
    const double sparAdvance = panel.sweep + 0.25 * (panel.tipChord - panel.rootChord);
    return std::atan2(sparAdvance, panel.panelSpan) * 180.0 / std::numbers::pi;
  };
  if (!panels.empty() && panels.front().centerSparWoodJoiner &&
      std::abs(sparAngle(panels.front())) > angleToleranceDegrees) {
    return QString{"Panel 1 center wood joiner cannot be built because the mirrored 25% wood spars do not form a straight line through the wing center. Adjust sweep or chord, or disable the wood joiner."};
  }
  for (std::size_t panel = 1; panel < panels.size(); ++panel) {
    const auto& outer = panels[panel];
    if (!outer.centerSparWoodJoiner) continue;
    const auto& inner = panels[panel - 1];
    const bool matchingWoodSpar = (inner.topSpar && outer.topSpar) ||
                                  (inner.bottomSpar && outer.bottomSpar);
    if (!matchingWoodSpar) {
      return QString{"Panel %1 wood joiner requires a matching top or bottom wood spar in Panel %2."}
          .arg(panel + 1).arg(panel);
    }
    if (std::abs(sparAngle(inner) - sparAngle(outer)) > angleToleranceDegrees) {
      return QString{"Panel %1 wood joiner cannot be built because the 25% wood spars in Panels %2 and %3 do not form a straight line. Adjust sweep or chord, or disable the wood joiner."}
          .arg(panel + 1).arg(panel).arg(panel + 1);
    }
  }
  return {};
}

WingPanelEditor::WingPanelEditor(const WingPanelData& data, const DisplayUnit globalUnit,
                                 const bool showUnitOverrides, const bool showJoinerPage,
                                 const bool showRootChord, QWidget* parent)
    : QWidget{parent}, airfoilData_{data}, globalUnit_{globalUnit},
      showUnitOverrides_{showUnitOverrides}, showJoinerPage_{showJoinerPage},
      showRootChord_{showRootChord} {
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  auto* tabs = new QTabWidget;
  tabs->setTabPosition(QTabWidget::West);
  tabs->addTab(makeSpecsPage(), "Specs");
  tabs->addTab(makeSparsPage(), "Spars");
  tabs->addTab(makeLeadingTrailingPage(), "LE/TE");
  tabs->addTab(makeControlsPage(), "Ailerons/Flaps");
  if (showJoinerPage_) tabs->addTab(makeJoinerPage(), "Joiner");
  layout->addWidget(tabs);
  setData(data);
}

QWidget* WingPanelEditor::makeSpecsPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  auto* airfoils = new QGroupBox{"Airfoils"};
  auto* airfoilLayout = new QVBoxLayout{airfoils};
  rootName_ = new QLabel;
  tipName_ = new QLabel;
  auto* rootButton = new QPushButton{"Import root .dat..."};
  auto* tipButton = new QPushButton{"Import tip .dat..."};
  airfoilLayout->addWidget(rootName_); airfoilLayout->addWidget(rootButton);
  airfoilLayout->addWidget(tipName_); airfoilLayout->addWidget(tipButton);
  rootName_->setVisible(showRootChord_); rootButton->setVisible(showRootChord_);
  layout->addWidget(airfoils);

  auto makeLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value};
    input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_);
    lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged);
    return input;
  };
  auto* dimensions = new QGroupBox{"Dimensions"};
  auto* form = new QFormLayout{dimensions};
  span_ = makeLength("panelSpan", 700.0);
  rootChord_ = makeLength("rootChord", 240.0);
  tipChord_ = makeLength("tipChord", 150.0);
  sweep_ = makeLength("sweep", 70.0);
  dihedral_ = angleInput(4.0);
  twist_ = angleInput(0.0);
  ribThickness_ = makeLength("ribThickness", 3.0);
  ribCount_ = new QSpinBox; ribCount_->setObjectName("ribCount"); ribCount_->setRange(2, 100);
  form->addRow("Panel Span", span_);
  rootChordLabel_ = new QLabel{"Root Chord"};
  form->addRow(rootChordLabel_, rootChord_);
  rootChordLabel_->setVisible(showRootChord_); rootChord_->setVisible(showRootChord_);
  form->addRow("Tip Chord", tipChord_); form->addRow("Tip Sweep", sweep_);
  form->addRow("Dihedral", dihedral_); form->addRow("Tip Twist", twist_);
  form->addRow("Rib Thickness", ribThickness_); form->addRow("Rib Count", ribCount_);
  layout->addWidget(dimensions); layout->addStretch();
  connect(rootButton, &QPushButton::clicked, this, [this] { importAirfoil(true); });
  connect(tipButton, &QPushButton::clicked, this, [this] { importAirfoil(false); });
  connect(dihedral_, &QDoubleSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  connect(twist_, &QDoubleSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  connect(ribCount_, &QSpinBox::valueChanged, this, [this] { updateConditionalControls(); emitChanged(); });
  return scrollPage(content);
}

QWidget* WingPanelEditor::makeSparsPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  auto makeLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value}; input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_); lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged); return input;
  };
  auto addCheck = [this, layout](const QString& label, QCheckBox*& check, QWidget*& details) {
    check = new QCheckBox{label}; layout->addWidget(check);
    connect(check, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
    details = nullptr;
  };
  addCheck("Top Spar", topSpar_, topSparDetails_);
  topSparHeight_ = makeLength("topSparHeight", 5); topSparWidth_ = makeLength("topSparWidth", 10);
  topSparDetails_ = detailRow({{"Height", topSparHeight_}, {"Width", topSparWidth_}}); layout->addWidget(topSparDetails_);
  addCheck("Bottom Spar", bottomSpar_, bottomSparDetails_);
  bottomSparHeight_ = makeLength("bottomSparHeight", 5); bottomSparWidth_ = makeLength("bottomSparWidth", 10);
  bottomSparDetails_ = detailRow({{"Height", bottomSparHeight_}, {"Width", bottomSparWidth_}}); layout->addWidget(bottomSparDetails_);
  addCheck("Shear Webs", shearWebs_, shearDetails_);
  shearWebWidth_ = makeLength("shearWebWidth", 3); shearDetails_ = detailRow({{"Thickness", shearWebWidth_}}); layout->addWidget(shearDetails_);

  auto* carbonGroup = new QButtonGroup{this}; carbonGroup->setExclusive(true);
  cfTube_ = new QRadioButton{"CF Tube"}; cfRod_ = new QRadioButton{"CF Rod"};
  carbonGroup->addButton(cfTube_); carbonGroup->addButton(cfRod_);
  layout->addWidget(cfTube_); cfTubeOd_ = makeLength("cfTubeOd", 6); cfTubeId_ = makeLength("cfTubeId", 5);
  cfTubeDetails_ = detailRow({{"OD", cfTubeOd_}, {"ID", cfTubeId_}}); layout->addWidget(cfTubeDetails_);
  layout->addWidget(cfRod_); cfRodOd_ = makeLength("cfRodOd", 6);
  cfRodDetails_ = detailRow({{"OD", cfRodOd_}}); layout->addWidget(cfRodDetails_);
  connect(cfTube_, &QRadioButton::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  connect(cfRod_, &QRadioButton::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  connect(cfTube_, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) { topSpar_->setChecked(false); bottomSpar_->setChecked(false); }
  });
  connect(cfRod_, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) { topSpar_->setChecked(false); bottomSpar_->setChecked(false); }
  });
  const auto clearCarbon = [carbonGroup, this](bool checked) {
    if (!checked) return;
    carbonGroup->setExclusive(false);
    cfTube_->setChecked(false); cfRod_->setChecked(false);
    carbonGroup->setExclusive(true);
  };
  connect(topSpar_, &QCheckBox::toggled, this, clearCarbon);
  connect(bottomSpar_, &QCheckBox::toggled, this, clearCarbon);

  auto* sheetingSeparator = new QFrame;
  sheetingSeparator->setFrameShape(QFrame::HLine);
  layout->addWidget(sheetingSeparator);

  auto addSheet = [&](const QString& label, const QString& key, QCheckBox*& check,
                      LengthInput*& value, QSpinBox*& stopRib, QWidget*& details) {
    check = new QCheckBox{label}; value = makeLength(key, 2);
    stopRib = new QSpinBox; stopRib->setRange(2, 100); stopRib->setValue(2);
    details = detailRow({{"Thickness", value}, {"Stop Rib Number", stopRib}});
    layout->addWidget(check); layout->addWidget(details);
    connect(check, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
    connect(stopRib, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  };
  addSheet("LE Top Sheeting", "leTopSheetThickness", leTopSheet_, leTopSheetThickness_, leTopSheetStopRib_, leTopSheetDetails_);
  addSheet("LE Bottom Sheeting", "leBottomSheetThickness", leBottomSheet_, leBottomSheetThickness_, leBottomSheetStopRib_, leBottomSheetDetails_);
  addSheet("TE Top Sheeting", "teTopSheetThickness", teTopSheet_, teTopSheetThickness_, teTopSheetStopRib_, teTopSheetDetails_);
  addSheet("TE Bottom Sheeting", "teBottomSheetThickness", teBottomSheet_, teBottomSheetThickness_, teBottomSheetStopRib_, teBottomSheetDetails_);
  auto* line1 = new QFrame; line1->setFrameShape(QFrame::HLine); layout->addWidget(line1);
  turbulators_ = new QCheckBox{"Turbulators"}; layout->addWidget(turbulators_);
  turbulatorCount_ = new QSpinBox; turbulatorCount_->setRange(1, 4);
  turbulatorHeight_ = makeLength("turbulatorHeight", 2); turbulatorWidth_ = makeLength("turbulatorWidth", 2);
  turbulatorDetails_ = detailRow({{"Count", turbulatorCount_}, {"Height", turbulatorHeight_}, {"Width", turbulatorWidth_}});
  layout->addWidget(turbulatorDetails_);
  connect(turbulators_, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  const auto rejectConflictingSelection = [this](QCheckBox* selected, QCheckBox* conflicting) {
    if (!selected->isChecked() || !conflicting->isChecked()) return;
    const QSignalBlocker blocker{selected};
    selected->setChecked(false);
    updateConditionalControls();
    QMessageBox::warning(this, "Conflicting spar options",
        "LE Top Sheeting and Turbulators cannot both be selected.");
  };
  connect(leTopSheet_, &QCheckBox::clicked, this, [this, rejectConflictingSelection](bool) {
    rejectConflictingSelection(leTopSheet_, turbulators_);
  });
  connect(turbulators_, &QCheckBox::clicked, this, [this, rejectConflictingSelection](bool) {
    rejectConflictingSelection(turbulators_, leTopSheet_);
  });
  connect(turbulatorCount_, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  auto* line2 = new QFrame; line2->setFrameShape(QFrame::HLine); layout->addWidget(line2);
  addCheck("Top 60% Rear Spar", topRearSpar_, topRearDetails_);
  topRearHeight_ = makeLength("topRearSparHeight", 4); topRearWidth_ = makeLength("topRearSparWidth", 4);
  topRearDetails_ = detailRow({{"Height", topRearHeight_}, {"Width", topRearWidth_}}); layout->addWidget(topRearDetails_);
  addCheck("Bottom 60% Rear Spar", bottomRearSpar_, bottomRearDetails_);
  bottomRearHeight_ = makeLength("bottomRearSparHeight", 4); bottomRearWidth_ = makeLength("bottomRearSparWidth", 4);
  bottomRearDetails_ = detailRow({{"Height", bottomRearHeight_}, {"Width", bottomRearWidth_}}); layout->addWidget(bottomRearDetails_);
  layout->addStretch();
  return scrollPage(content);
}

QWidget* WingPanelEditor::makeLeadingTrailingPage() {
  auto* content = new QWidget; auto* layout = new QVBoxLayout{content};
  auto makeLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value}; input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_); lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged); return input;
  };
  auto* leGroup = new QButtonGroup{this}; leGroup->setExclusive(true);
  shapedLe_ = new QRadioButton{"Shaped LE Stock"}; blockLe_ = new QRadioButton{"Block LE Stock"};
  tubeLe_ = new QRadioButton{"CF Tube LE"}; rodLe_ = new QRadioButton{"CF Rod LE"};
  for (auto* button : {shapedLe_, blockLe_, tubeLe_, rodLe_}) { leGroup->addButton(button); layout->addWidget(button); }
  leWidth_ = makeLength("leadingEdgeWidth", 5); leHeight_ = makeLength("leadingEdgeHeight", 7);
  stockLeDetails_ = detailRow({{"Width", leWidth_}, {"Height", leHeight_}}); layout->insertWidget(1, stockLeDetails_);
  leTubeOd_ = makeLength("leadingEdgeTubeOd", 2); leTubeId_ = makeLength("leadingEdgeTubeId", 1);
  tubeLeDetails_ = detailRow({{"OD", leTubeOd_}, {"ID", leTubeId_}}); layout->insertWidget(4, tubeLeDetails_);
  leRodOd_ = makeLength("leadingEdgeRodOd", 2); rodLeDetails_ = detailRow({{"OD", leRodOd_}}); layout->addWidget(rodLeDetails_);
  auto* line = new QFrame; line->setFrameShape(QFrame::HLine); layout->addWidget(line);
  auto* teGroup = new QButtonGroup{this}; teGroup->setExclusive(true);
  shapedTe_ = new QRadioButton{"Shaped TE Stock"}; sheetTe_ = new QRadioButton{"Sheet TE Stock"};
  teGroup->addButton(shapedTe_); teGroup->addButton(sheetTe_); layout->addWidget(shapedTe_); layout->addWidget(sheetTe_);
  teWidth_ = makeLength("trailingEdgeWidth", 20); teHeight_ = makeLength("trailingEdgeHeight", 3);
  stockTeDetails_ = detailRow({{"Width", teWidth_}, {"Height", teHeight_}}); layout->addWidget(stockTeDetails_);
  slottedForRibs_ = new QCheckBox{"Slotted for Ribs"}; slottedDetails_ = detailRow({{"", slottedForRibs_}}); layout->addWidget(slottedDetails_);
  for (auto* button : {shapedLe_, blockLe_, tubeLe_, rodLe_, shapedTe_, sheetTe_})
    connect(button, &QRadioButton::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  connect(slottedForRibs_, &QCheckBox::toggled, this, &WingPanelEditor::emitChanged);
  layout->addStretch(); return scrollPage(content);
}

QWidget* WingPanelEditor::makeControlsPage() {
  auto* content = new QWidget; auto* layout = new QVBoxLayout{content};
  auto makeLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value}; input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_); lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged); return input;
  };
  auto makeDetails = [&](const QString& prefix, double width, double height,
                         LengthInput*& widthInput, LengthInput*& heightInput,
                         LengthInput*& hingeWidth, LengthInput*& hingeHeight,
                         QSpinBox*& start, QSpinBox*& stop) {
    widthInput = makeLength(prefix + "Width", width); heightInput = makeLength(prefix + "Height", height);
    hingeWidth = makeLength(prefix + "HingePostWidth", 6.0);
    hingeHeight = makeLength(prefix + "HingePostHeight", 10.0);
    start = new QSpinBox; stop = new QSpinBox;
    start->setObjectName(prefix + "StartRib");
    stop->setObjectName(prefix + "StopRib");
    auto* details = new QWidget;
    auto* detailsLayout = new QVBoxLayout{details};
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(0);
    detailsLayout->addWidget(detailRow({{"Width", widthInput}, {"Height", heightInput}}));
    detailsLayout->addWidget(detailRow({{"Start Rib", start}, {"Stop Rib", stop}}));
    detailsLayout->addWidget(detailRow({{"Hinge Post Width", hingeWidth},
                                        {"Hinge Post Height", hingeHeight}}));
    return details;
  };
  ailerons_ = new QCheckBox{"Ailerons"}; layout->addWidget(ailerons_);
  aileronDetails_ = makeDetails("aileron", 35, 10, aileronWidth_, aileronHeight_,
      aileronHingePostWidth_, aileronHingePostHeight_, aileronStart_, aileronStop_);
  layout->addWidget(aileronDetails_);
  flaps_ = new QCheckBox{"Flaps"}; layout->addWidget(flaps_);
  flapDetails_ = makeDetails("flap", 40, 10, flapWidth_, flapHeight_,
      flapHingePostWidth_, flapHingePostHeight_, flapStart_, flapStop_);
  layout->addWidget(flapDetails_);
  connect(ailerons_, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  connect(flaps_, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  for (auto* spinner : {aileronStart_, aileronStop_, flapStart_, flapStop_})
    connect(spinner, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  layout->addStretch(); return scrollPage(content);
}

QWidget* WingPanelEditor::makeJoinerPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  auto makeLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value}; input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_); lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged); return input;
  };
  addRib1a_ = new QCheckBox{"Add Rib 1a"};
  addRib1a_->setVisible(showRootChord_);
  layout->addWidget(addRib1a_);
  centerSparJoinerLabel_ = new QLabel{"Dihedral Joiner(s)"};
  centerSparJoinerLabel_->setStyleSheet("font-weight: 600; margin-top: 8px;");
  layout->addWidget(centerSparJoinerLabel_);
  centerSparWoodJoiner_ = new QCheckBox{"Center Spar Wood Joiner"};
  layout->addWidget(centerSparWoodJoiner_);

  const auto addTubularJoiner = [&](const QString& label, const QString& key,
                                    QCheckBox*& enabled, QRadioButton*& rod,
                                    QRadioButton*& cfTube, QRadioButton*& aluminumTube,
                                    QWidget*& types, LengthInput*& od, LengthInput*& id,
                                    QWidget*& odDetails, QWidget*& idDetails) {
    enabled = new QCheckBox{label}; layout->addWidget(enabled);
    rod = new QRadioButton{"CF Rod"}; cfTube = new QRadioButton{"CF Tube"};
    aluminumTube = new QRadioButton{"Aluminum Tube"};
    auto* group = new QButtonGroup{content};
    group->setExclusive(true);
    for (auto* option : {rod, cfTube, aluminumTube}) group->addButton(option);
    types = detailRow({{"", rod}, {"", cfTube}, {"", aluminumTube}});
    layout->addWidget(types);
    od = makeLength(key + "Od", 6.0); id = makeLength(key + "Id", 5.0);
    odDetails = detailRow({{"OD", od}}); idDetails = detailRow({{"ID", id}});
    layout->addWidget(odDetails); layout->addWidget(idDetails);
    connect(enabled, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
    for (auto* option : {rod, cfTube, aluminumTube})
      connect(option, &QRadioButton::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  };
  addTubularJoiner("Joiner behind mid spar", "behindSparJoiner",
      behindSparJoiner_, behindJoinerRod_, behindJoinerCfTube_, behindJoinerAlTube_,
      behindJoinerTypeDetails_, behindJoinerOd_, behindJoinerId_,
      behindJoinerRodDetails_, behindJoinerTubeDetails_);
  addTubularJoiner("Joiner at 60% Chord", "fiftyPercentJoiner",
      fiftyPercentJoiner_, fiftyJoinerRod_, fiftyJoinerCfTube_, fiftyJoinerAlTube_,
      fiftyJoinerTypeDetails_, fiftyJoinerOd_, fiftyJoinerId_,
      fiftyJoinerRodDetails_, fiftyJoinerTubeDetails_);
  connect(addRib1a_, &QCheckBox::toggled, this, &WingPanelEditor::emitChanged);
  connect(centerSparWoodJoiner_, &QCheckBox::toggled, this, &WingPanelEditor::emitChanged);
  layout->addStretch();
  return scrollPage(content);
}

void WingPanelEditor::importAirfoil(const bool root) {
  const auto path = QFileDialog::getOpenFileName(this, root ? "Import root airfoil" : "Import tip airfoil", {},
                                                  "Airfoil data (*.dat);;All files (*)");
  if (path.isEmpty()) return;
  try {
    auto profile = domain::AirfoilProfile::fromDatFile(path.toStdWString());
    if (root) { airfoilData_.rootAirfoil = std::move(profile); airfoilData_.rootAirfoilPath = path; }
    else { airfoilData_.tipAirfoil = std::move(profile); airfoilData_.tipAirfoilPath = path; }
    rootName_->setText(QString::fromStdString(airfoilData_.rootAirfoil.name()));
    tipName_->setText(QString::fromStdString(airfoilData_.tipAirfoil.name()));
    emit changed();
  } catch (const std::exception& error) { QMessageBox::critical(this, "Airfoil import failed", error.what()); }
}

WingPanelData WingPanelEditor::data() const {
  WingPanelData d = airfoilData_;
  d.panelSpan = span_->valueMm(); d.rootChord = rootChord_->valueMm(); d.tipChord = tipChord_->valueMm();
  d.sweep = sweep_->valueMm(); d.dihedral = dihedral_->value(); d.twist = twist_->value();
  d.ribThickness = ribThickness_->valueMm(); d.ribCount = ribCount_->value();
  d.topSpar = topSpar_->isChecked(); d.topSparHeight = topSparHeight_->valueMm(); d.topSparWidth = topSparWidth_->valueMm();
  d.bottomSpar = bottomSpar_->isChecked(); d.bottomSparHeight = bottomSparHeight_->valueMm(); d.bottomSparWidth = bottomSparWidth_->valueMm();
  d.shearWebs = shearWebs_->isChecked(); d.shearWebWidth = shearWebWidth_->valueMm();
  d.carbonSpar = cfTube_->isChecked() ? 1 : cfRod_->isChecked() ? 2 : 0;
  d.cfTubeOd = cfTubeOd_->valueMm(); d.cfTubeId = cfTubeId_->valueMm(); d.cfRodOd = cfRodOd_->valueMm();
  d.leTopSheet = leTopSheet_->isChecked(); d.leTopSheetThickness = leTopSheetThickness_->valueMm(); d.leTopSheetStopRib = leTopSheetStopRib_->value();
  d.leBottomSheet = leBottomSheet_->isChecked(); d.leBottomSheetThickness = leBottomSheetThickness_->valueMm(); d.leBottomSheetStopRib = leBottomSheetStopRib_->value();
  d.teTopSheet = teTopSheet_->isChecked(); d.teTopSheetThickness = teTopSheetThickness_->valueMm(); d.teTopSheetStopRib = teTopSheetStopRib_->value();
  d.teBottomSheet = teBottomSheet_->isChecked(); d.teBottomSheetThickness = teBottomSheetThickness_->valueMm(); d.teBottomSheetStopRib = teBottomSheetStopRib_->value();
  d.turbulators = turbulators_->isChecked(); d.turbulatorCount = turbulatorCount_->value();
  d.turbulatorHeight = turbulatorHeight_->valueMm(); d.turbulatorWidth = turbulatorWidth_->valueMm();
  d.topRearSpar = topRearSpar_->isChecked(); d.topRearSparHeight = topRearHeight_->valueMm(); d.topRearSparWidth = topRearWidth_->valueMm();
  d.bottomRearSpar = bottomRearSpar_->isChecked(); d.bottomRearSparHeight = bottomRearHeight_->valueMm(); d.bottomRearSparWidth = bottomRearWidth_->valueMm();
  d.leadingEdgeType = shapedLe_->isChecked() ? 1 : blockLe_->isChecked() ? 2 : tubeLe_->isChecked() ? 3 : rodLe_->isChecked() ? 4 : 0;
  d.leadingEdgeWidth = leWidth_->valueMm(); d.leadingEdgeHeight = leHeight_->valueMm(); d.leadingEdgeTubeOd = leTubeOd_->valueMm();
  d.leadingEdgeTubeId = leTubeId_->valueMm(); d.leadingEdgeRodOd = leRodOd_->valueMm();
  d.trailingEdgeType = shapedTe_->isChecked() ? 1 : sheetTe_->isChecked() ? 2 : 0;
  d.trailingEdgeWidth = teWidth_->valueMm(); d.trailingEdgeHeight = teHeight_->valueMm(); d.slottedForRibs = slottedForRibs_->isChecked();
  d.ailerons = ailerons_->isChecked(); d.aileronWidth = aileronWidth_->valueMm(); d.aileronHeight = aileronHeight_->valueMm();
  d.aileronStartRib = aileronStart_->value(); d.aileronStopRib = aileronStop_->value();
  d.aileronHingePostWidth = aileronHingePostWidth_->valueMm(); d.aileronHingePostHeight = aileronHingePostHeight_->valueMm();
  d.flaps = flaps_->isChecked(); d.flapWidth = flapWidth_->valueMm(); d.flapHeight = flapHeight_->valueMm();
  d.flapStartRib = flapStart_->value(); d.flapStopRib = flapStop_->value();
  d.flapHingePostWidth = flapHingePostWidth_->valueMm(); d.flapHingePostHeight = flapHingePostHeight_->valueMm();
  if (showJoinerPage_) {
    d.addRib1a = showRootChord_ && addRib1a_->isChecked();
    d.centerSparWoodJoiner = centerSparWoodJoiner_->isChecked();
    d.behindSparJoiner = behindSparJoiner_->isChecked();
    d.behindSparJoinerType = behindJoinerRod_->isChecked() ? 1 :
        behindJoinerCfTube_->isChecked() ? 2 : behindJoinerAlTube_->isChecked() ? 3 : 0;
    d.behindSparJoinerOd = behindJoinerOd_->valueMm();
    d.behindSparJoinerId = behindJoinerId_->valueMm();
    d.fiftyPercentJoiner = fiftyPercentJoiner_->isChecked();
    d.fiftyPercentJoinerType = fiftyJoinerRod_->isChecked() ? 1 :
        fiftyJoinerCfTube_->isChecked() ? 2 : fiftyJoinerAlTube_->isChecked() ? 3 : 0;
    d.fiftyPercentJoinerOd = fiftyJoinerOd_->valueMm();
    d.fiftyPercentJoinerId = fiftyJoinerId_->valueMm();
  }
  for (auto it = lengths_.constBegin(); it != lengths_.constEnd(); ++it) d.unitOverrides.insert(it.key(), it.value()->unitOverride());
  return d;
}

void WingPanelEditor::setData(const WingPanelData& d) {
  airfoilData_ = d;
  rootName_->setText(QString::fromStdString(d.rootAirfoil.name())); tipName_->setText(QString::fromStdString(d.tipAirfoil.name()));
  auto setLength = [&](LengthInput* input, double value, const QString& key) {
    input->setValueMm(value); input->setUnitOverride(d.unitOverrides.value(key, UnitOverride::Global));
  };
  setLength(span_, d.panelSpan, "panelSpan"); setLength(rootChord_, d.rootChord, "rootChord");
  setLength(tipChord_, d.tipChord, "tipChord"); setLength(sweep_, d.sweep, "sweep");
  dihedral_->setValue(d.dihedral); twist_->setValue(d.twist); setLength(ribThickness_, d.ribThickness, "ribThickness"); ribCount_->setValue(d.ribCount);
#define SET_LENGTH(ptr, field) setLength(ptr, d.field, #field)
  topSpar_->setChecked(d.topSpar); SET_LENGTH(topSparHeight_, topSparHeight); SET_LENGTH(topSparWidth_, topSparWidth);
  bottomSpar_->setChecked(d.bottomSpar); SET_LENGTH(bottomSparHeight_, bottomSparHeight); SET_LENGTH(bottomSparWidth_, bottomSparWidth);
  shearWebs_->setChecked(d.shearWebs); SET_LENGTH(shearWebWidth_, shearWebWidth);
  cfTube_->setChecked(d.carbonSpar == 1); cfRod_->setChecked(d.carbonSpar == 2); SET_LENGTH(cfTubeOd_, cfTubeOd); SET_LENGTH(cfTubeId_, cfTubeId); SET_LENGTH(cfRodOd_, cfRodOd);
  leTopSheet_->setChecked(d.leTopSheet); SET_LENGTH(leTopSheetThickness_, leTopSheetThickness); leTopSheetStopRib_->setValue(d.leTopSheetStopRib);
  leBottomSheet_->setChecked(d.leBottomSheet); SET_LENGTH(leBottomSheetThickness_, leBottomSheetThickness); leBottomSheetStopRib_->setValue(d.leBottomSheetStopRib);
  teTopSheet_->setChecked(d.teTopSheet); SET_LENGTH(teTopSheetThickness_, teTopSheetThickness); teTopSheetStopRib_->setValue(d.teTopSheetStopRib);
  teBottomSheet_->setChecked(d.teBottomSheet); SET_LENGTH(teBottomSheetThickness_, teBottomSheetThickness); teBottomSheetStopRib_->setValue(d.teBottomSheetStopRib);
  turbulators_->setChecked(d.turbulators && !d.leTopSheet); turbulatorCount_->setValue(d.turbulatorCount); SET_LENGTH(turbulatorHeight_, turbulatorHeight); SET_LENGTH(turbulatorWidth_, turbulatorWidth);
  topRearSpar_->setChecked(d.topRearSpar); SET_LENGTH(topRearHeight_, topRearSparHeight); SET_LENGTH(topRearWidth_, topRearSparWidth);
  bottomRearSpar_->setChecked(d.bottomRearSpar); SET_LENGTH(bottomRearHeight_, bottomRearSparHeight); SET_LENGTH(bottomRearWidth_, bottomRearSparWidth);
  shapedLe_->setChecked(d.leadingEdgeType == 1); blockLe_->setChecked(d.leadingEdgeType == 2); tubeLe_->setChecked(d.leadingEdgeType == 3); rodLe_->setChecked(d.leadingEdgeType == 4);
  SET_LENGTH(leWidth_, leadingEdgeWidth); SET_LENGTH(leHeight_, leadingEdgeHeight); SET_LENGTH(leTubeOd_, leadingEdgeTubeOd); SET_LENGTH(leTubeId_, leadingEdgeTubeId); SET_LENGTH(leRodOd_, leadingEdgeRodOd);
  shapedTe_->setChecked(d.trailingEdgeType == 1); sheetTe_->setChecked(d.trailingEdgeType == 2); SET_LENGTH(teWidth_, trailingEdgeWidth); SET_LENGTH(teHeight_, trailingEdgeHeight); slottedForRibs_->setChecked(d.slottedForRibs);
  ailerons_->setChecked(d.ailerons); SET_LENGTH(aileronWidth_, aileronWidth); SET_LENGTH(aileronHeight_, aileronHeight); aileronStart_->setValue(d.aileronStartRib); aileronStop_->setValue(d.aileronStopRib); SET_LENGTH(aileronHingePostWidth_, aileronHingePostWidth); SET_LENGTH(aileronHingePostHeight_, aileronHingePostHeight);
  flaps_->setChecked(d.flaps); SET_LENGTH(flapWidth_, flapWidth); SET_LENGTH(flapHeight_, flapHeight); flapStart_->setValue(d.flapStartRib); flapStop_->setValue(d.flapStopRib); SET_LENGTH(flapHingePostWidth_, flapHingePostWidth); SET_LENGTH(flapHingePostHeight_, flapHingePostHeight);
  if (showJoinerPage_) {
    addRib1a_->setChecked(showRootChord_ && d.addRib1a); centerSparWoodJoiner_->setChecked(d.centerSparWoodJoiner);
    behindSparJoiner_->setChecked(d.behindSparJoiner);
    behindJoinerRod_->setChecked(d.behindSparJoinerType == 1);
    behindJoinerCfTube_->setChecked(d.behindSparJoinerType == 2);
    behindJoinerAlTube_->setChecked(d.behindSparJoinerType == 3);
    SET_LENGTH(behindJoinerOd_, behindSparJoinerOd); SET_LENGTH(behindJoinerId_, behindSparJoinerId);
    fiftyPercentJoiner_->setChecked(d.fiftyPercentJoiner);
    fiftyJoinerRod_->setChecked(d.fiftyPercentJoinerType == 1);
    fiftyJoinerCfTube_->setChecked(d.fiftyPercentJoinerType == 2);
    fiftyJoinerAlTube_->setChecked(d.fiftyPercentJoinerType == 3);
    SET_LENGTH(fiftyJoinerOd_, fiftyPercentJoinerOd); SET_LENGTH(fiftyJoinerId_, fiftyPercentJoinerId);
  }
#undef SET_LENGTH
  updateConditionalControls();
}

void WingPanelEditor::setGlobalUnit(const DisplayUnit unit) {
  globalUnit_ = unit;
  for (auto* input : lengths_) input->setGlobalUnit(unit);
}

bool WingPanelEditor::validate(QString& error) const {
  if (showJoinerPage_ && behindSparJoiner_->isChecked() &&
      !behindJoinerRod_->isChecked() && !behindJoinerCfTube_->isChecked() &&
      !behindJoinerAlTube_->isChecked()) {
    error = "Select CF Rod, CF Tube, or Aluminum Tube for the joiner behind the mid spar.";
    return false;
  }
  if (showJoinerPage_ && fiftyPercentJoiner_->isChecked() &&
      !fiftyJoinerRod_->isChecked() && !fiftyJoinerCfTube_->isChecked() &&
      !fiftyJoinerAlTube_->isChecked()) {
    error = "Select CF Rod, CF Tube, or Aluminum Tube for the joiner at 60% chord.";
    return false;
  }
  if (ailerons_->isChecked() && aileronStart_->value() >= aileronStop_->value()) {
    error = "Aileron Start Rib must be less than Aileron Stop Rib.";
    return false;
  }
  if (flaps_->isChecked() && flapStart_->value() >= flapStop_->value()) {
    error = "Flap Start Rib must be less than Flap Stop Rib.";
    return false;
  }
  if (flaps_->isChecked() && ailerons_->isChecked() &&
      std::max(flapStart_->value(), aileronStart_->value()) <
          std::min(flapStop_->value(), aileronStop_->value())) {
    error = "Flap and aileron rib ranges may meet at a rib but cannot overlap.";
    return false;
  }
  return true;
}

void WingPanelEditor::updateConditionalControls() {
  topSparDetails_->setVisible(topSpar_->isChecked()); bottomSparDetails_->setVisible(bottomSpar_->isChecked());
  const bool bothSpars = topSpar_->isChecked() && bottomSpar_->isChecked();
  shearWebs_->setVisible(bothSpars); shearDetails_->setVisible(bothSpars && shearWebs_->isChecked());
  cfTubeDetails_->setVisible(cfTube_->isChecked()); cfRodDetails_->setVisible(cfRod_->isChecked());
  leTopSheetDetails_->setVisible(leTopSheet_->isChecked()); leBottomSheetDetails_->setVisible(leBottomSheet_->isChecked());
  teTopSheetDetails_->setVisible(teTopSheet_->isChecked()); teBottomSheetDetails_->setVisible(teBottomSheet_->isChecked());
  turbulatorDetails_->setVisible(turbulators_->isChecked()); topRearDetails_->setVisible(topRearSpar_->isChecked()); bottomRearDetails_->setVisible(bottomRearSpar_->isChecked());
  stockLeDetails_->setVisible(shapedLe_->isChecked() || blockLe_->isChecked()); tubeLeDetails_->setVisible(tubeLe_->isChecked()); rodLeDetails_->setVisible(rodLe_->isChecked());
  stockTeDetails_->setVisible(shapedTe_->isChecked() || sheetTe_->isChecked()); slottedDetails_->setVisible(sheetTe_->isChecked());
  aileronDetails_->setVisible(ailerons_->isChecked()); flapDetails_->setVisible(flaps_->isChecked());
  if (showJoinerPage_) {
    const bool woodAvailable = topSpar_->isChecked() || bottomSpar_->isChecked();
    centerSparWoodJoiner_->setVisible(woodAvailable);
    if (!woodAvailable) centerSparWoodJoiner_->setChecked(false);
    const bool behind = behindSparJoiner_->isChecked();
    behindJoinerTypeDetails_->setVisible(behind);
    behindJoinerRodDetails_->setVisible(behind &&
        (behindJoinerRod_->isChecked() || behindJoinerCfTube_->isChecked() || behindJoinerAlTube_->isChecked()));
    behindJoinerTubeDetails_->setVisible(behind &&
        (behindJoinerCfTube_->isChecked() || behindJoinerAlTube_->isChecked()));
    const bool fifty = fiftyPercentJoiner_->isChecked();
    fiftyJoinerTypeDetails_->setVisible(fifty);
    fiftyJoinerRodDetails_->setVisible(fifty &&
        (fiftyJoinerRod_->isChecked() || fiftyJoinerCfTube_->isChecked() || fiftyJoinerAlTube_->isChecked()));
    fiftyJoinerTubeDetails_->setVisible(fifty &&
        (fiftyJoinerCfTube_->isChecked() || fiftyJoinerAlTube_->isChecked()));
  }
  const int ribs = ribCount_->value();
  for (auto* input : {leTopSheetStopRib_, leBottomSheetStopRib_,
                      teTopSheetStopRib_, teBottomSheetStopRib_})
    input->setRange(2, ribs);
  const int lastInternalRib = std::max(2, ribs - 1);
  for (auto* input : {aileronStart_, aileronStop_, flapStart_, flapStop_})
    input->setRange(2, lastInternalRib);
}

void WingPanelEditor::emitChanged() { emit changed(); }

} // namespace designrc::gui
