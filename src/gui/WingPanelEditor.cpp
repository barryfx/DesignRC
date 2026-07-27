#include "gui/WingPanelEditor.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <numbers>

namespace designrc::gui {
namespace {

constexpr double kMmPerInch = 25.4;
int nineCharacterSpinnerWidth(const QWidget* spinner) {
  const QFontMetrics metrics{spinner->font()};
  const int textWidth = metrics.horizontalAdvance(QString(9, QChar{'M'}));
  const int arrowWidth = spinner->style()->pixelMetric(
      QStyle::PM_ScrollBarExtent, nullptr, spinner);
  const int frameWidth = spinner->style()->pixelMetric(
      QStyle::PM_DefaultFrameWidth, nullptr, spinner);
  return textWidth + arrowWidth + 2 * frameWidth + 12;
}

bool isThirtySecondMultiple(const double value) {
  return std::abs(value * 32.0 - std::round(value * 32.0)) < 1.0e-8;
}

QString decimalInchText(const double value) {
  QString result = QString::number(value, 'f', 5);
  while (result.contains('.') && result.endsWith('0')) result.chop(1);
  if (result.endsWith('.')) result.chop(1);
  return result;
}

QString fractionalInchText(const double input) {
  const double value = std::round(input * 32.0) / 32.0;
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

enum class ExplicitLengthUnit { None, Millimeters, Inches };

bool takeTrailingUnit(QString& text, const QString& suffix) {
  text = text.trimmed();
  if (!text.endsWith(suffix, Qt::CaseInsensitive)) return false;
  text.chop(suffix.size());
  text = text.trimmed();
  return true;
}

QString editableLengthText(QString text, const bool displayedInches) {
  // QDoubleSpinBox may include its configured suffix in the text passed to
  // validation/parsing. Remove that suffix first so an explicitly typed
  // opposite unit can precede it (for example, "12mm in").
  takeTrailingUnit(text, displayedInches ? "in" : "mm");
  return text;
}

ExplicitLengthUnit takeExplicitLengthUnit(QString& text) {
  if (takeTrailingUnit(text, "mm"))
    return ExplicitLengthUnit::Millimeters;
  if (takeTrailingUnit(text, "in") || takeTrailingUnit(text, "\""))
    return ExplicitLengthUnit::Inches;
  return ExplicitLengthUnit::None;
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

  int takeExplicitDisplayUnit() {
    const int result = explicitDisplayUnit_;
    explicitDisplayUnit_ = -1;
    return result;
  }

  void setFractionalInches(const bool enabled) {
    fractionalInches_ = enabled;
    setDecimals(enabled ? 5 : 2);
    setSingleStep(enabled ? 1.0 / 32.0 : 1.0);
    setSuffix(enabled ? " in" : " mm");
  }

protected:
  QString textFromValue(const double value) const override {
    if (!fractionalInches_) return QDoubleSpinBox::textFromValue(value);
    return isThirtySecondMultiple(value)
        ? fractionalInchText(value) : decimalInchText(value);
  }

  double valueFromText(const QString& text) const override {
    explicitDisplayUnit_ = -1;
    QString candidate = editableLengthText(text, fractionalInches_);
    const auto explicitUnit = takeExplicitLengthUnit(candidate);
    double parsed = value();
    if (explicitUnit == ExplicitLengthUnit::Inches) {
      if (!parseInchText(candidate, parsed)) return value();
      explicitDisplayUnit_ = static_cast<int>(DisplayUnit::Inches);
      return fractionalInches_ ? parsed : parsed * kMmPerInch;
    }
    if (explicitUnit == ExplicitLengthUnit::Millimeters) {
      bool ok = false;
      parsed = locale().toDouble(candidate, &ok);
      if (!ok) return value();
      explicitDisplayUnit_ = static_cast<int>(DisplayUnit::Millimeters);
      return fractionalInches_ ? parsed / kMmPerInch : parsed;
    }
    if (fractionalInches_) {
      if (!parseInchText(candidate, parsed)) return value();
      return parsed;
    }
    return QDoubleSpinBox::valueFromText(text);
  }

  QValidator::State validate(QString& text, int& position) const override {
    QString candidate = editableLengthText(text, fractionalInches_);
    const auto explicitUnit = takeExplicitLengthUnit(candidate);
    if (candidate.isEmpty() || candidate == "-" || candidate.endsWith('/'))
      return QValidator::Intermediate;
    const QString lower = candidate.toLower();
    if (lower.endsWith('m') || lower.endsWith('i'))
      return QValidator::Intermediate;
    double parsed{};
    if (explicitUnit == ExplicitLengthUnit::Inches)
      return parseInchText(candidate, parsed)
          ? QValidator::Acceptable : QValidator::Invalid;
    if (explicitUnit == ExplicitLengthUnit::Millimeters) {
      bool ok = false;
      locale().toDouble(candidate, &ok);
      return ok ? QValidator::Acceptable : QValidator::Invalid;
    }
    if (fractionalInches_)
      return parseInchText(candidate, parsed)
          ? QValidator::Acceptable : QValidator::Invalid;
    if (candidate.contains('/'))
      return parseInchText(candidate, parsed)
          ? QValidator::Intermediate : QValidator::Invalid;
    return QDoubleSpinBox::validate(text, position);
  }

  void stepBy(int steps) override {
    if (!fractionalInches_) { QDoubleSpinBox::stepBy(steps); return; }
    if (steps == 0) return;
    constexpr double tolerance = 1.0e-8;
    const double thirtySeconds = value() * 32.0;
    const double grid = steps > 0
        ? std::floor(thirtySeconds + tolerance)
        : std::ceil(thirtySeconds - tolerance);
    setValue((grid + static_cast<double>(steps)) / 32.0);
  }

private:
  bool fractionalInches_{false};
  mutable int explicitDisplayUnit_{-1};
};

class RibStationSpinBox final : public QSpinBox {
public:
  using QSpinBox::QSpinBox;

  void setRib1aPresent(const bool present) {
    rib1aPresent_ = present;
    update();
  }

protected:
  QString textFromValue(const int value) const override {
    if (!rib1aPresent_) return QString{"R%1"}.arg(value);
    if (value == 1) return "R1";
    if (value == 2) return "R1a";
    return QString{"R%1"}.arg(value - 1);
  }

private:
  bool rib1aPresent_{false};
};

LengthInput::LengthInput(const QString& key, const double valueMm, QWidget* parent)
    : QWidget{parent}, key_{key}, valueMm_{valueMm} {
  setObjectName(key);
  auto* layout = new QHBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(3);
  spin_ = new FractionalSpinBox;
  spin_->setDecimals(2);
  spin_->setRange(std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::max());
  spin_->setFixedWidth(nineCharacterSpinnerWidth(spin_));
  unit_ = new QComboBox;
  unit_->addItems({"Global", "mm", "in"});
  unit_->setToolTip("Use the global unit or override this length");
  layout->addWidget(spin_, 0, Qt::AlignLeft | Qt::AlignVCenter);
  layout->addWidget(unit_);
  layout->addStretch(1);
  const auto applyEnteredUnit = [this] {
    const int explicitUnit = spin_->takeExplicitDisplayUnit();
    if (explicitUnit < 0) return false;
    const QSignalBlocker blocker{unit_};
    unit_->setCurrentIndex(explicitUnit == static_cast<int>(DisplayUnit::Inches)
        ? static_cast<int>(UnitOverride::Inches)
        : static_cast<int>(UnitOverride::Millimeters));
    refreshDisplay();
    return true;
  };
  connect(spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (refreshing_) return;
    const auto effective = unitOverride() == UnitOverride::Global
        ? globalUnit_ : unitOverride() == UnitOverride::Inches
            ? DisplayUnit::Inches : DisplayUnit::Millimeters;
    valueMm_ = effective == DisplayUnit::Inches ? value * kMmPerInch : value;
    const int explicitUnit = spin_->takeExplicitDisplayUnit();
    if (explicitUnit >= 0) {
      const QSignalBlocker blocker{unit_};
      unit_->setCurrentIndex(explicitUnit == static_cast<int>(DisplayUnit::Inches)
          ? static_cast<int>(UnitOverride::Inches)
          : static_cast<int>(UnitOverride::Millimeters));
      refreshDisplay();
    }
    emit valueChanged();
  });
  connect(spin_, &QDoubleSpinBox::editingFinished, this,
      [this, applyEnteredUnit] {
        if (refreshing_ || !applyEnteredUnit()) return;
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
int LengthInput::measurementFieldWidth() const { return spin_->width(); }
void LengthInput::refreshDisplay() {
  refreshing_ = true;
  const auto effective = unitOverride() == UnitOverride::Global
      ? globalUnit_ : unitOverride() == UnitOverride::Inches
          ? DisplayUnit::Inches : DisplayUnit::Millimeters;
  spin_->setFractionalInches(effective == DisplayUnit::Inches);
  const double inches = valueMm_ / kMmPerInch;
  spin_->setValue(effective == DisplayUnit::Inches
      ? inches
      : valueMm_);
  refreshing_ = false;
}

QJsonObject panelDataToJson(const WingPanelData& d) {
  QJsonObject o;
#define PUT(name) o.insert(#name, d.name)
  PUT(panelSpan); PUT(rootChord); PUT(tipChord); PUT(sweep); PUT(dihedral); PUT(twist);
  PUT(ribThickness); PUT(ribCount); PUT(rootAirfoilPath); PUT(tipAirfoilPath);
  PUT(ribLighteningHoles); PUT(ribLighteningStartRib);
  PUT(ribLighteningStopRib); PUT(ribLighteningMinimumWoodMargin);
  PUT(ribLighteningMinimumHoleDistance);
  PUT(riblets); PUT(ribletStartRib); PUT(ribletEndRib);
  PUT(ribletsPerBay);
  PUT(topSpar); PUT(topSparHeight); PUT(topSparWidth); PUT(bottomSpar); PUT(bottomSparHeight);
  PUT(bottomSparWidth); PUT(shearWebs); PUT(shearWebWidth); PUT(carbonSpar); PUT(cfTubeOd);
  PUT(cfTubeId); PUT(cfRodOd); PUT(leTopSheet); PUT(leTopSheetThickness); PUT(leTopSheetStopRib); PUT(leBottomSheet);
  PUT(leBottomSheetThickness); PUT(leBottomSheetStopRib); PUT(teTopSheet); PUT(teTopSheetThickness); PUT(teTopSheetStopRib); PUT(teBottomSheet);
  PUT(teBottomSheetThickness); PUT(teBottomSheetStopRib); PUT(turbulators); PUT(turbulatorCount); PUT(turbulatorHeight);
  PUT(turbulatorWidth); PUT(topRearSpar); PUT(topRearSparHeight); PUT(topRearSparWidth);
  PUT(bottomRearSpar); PUT(bottomRearSparHeight); PUT(bottomRearSparWidth);
  PUT(leadingEdgeWidth); PUT(leadingEdgeHeight); PUT(leadingEdgeTubeOd); PUT(leadingEdgeTubeId);
  PUT(leadingEdgeRodOd); PUT(trailingEdgeWidth); PUT(trailingEdgeHeight);
  PUT(slottedForRibs); PUT(ailerons); PUT(aileronWidth); PUT(aileronHeight); PUT(aileronStartRib);
  PUT(aileronStopRib); PUT(aileronHingePostWidth); PUT(aileronHingePostHeight);
  PUT(flaps); PUT(flapWidth); PUT(flapHeight); PUT(flapStartRib); PUT(flapStopRib);
  PUT(flapHingePostWidth); PUT(flapHingePostHeight);
  PUT(spoilers); PUT(spoilerStartRib); PUT(spoilerEndRib);
  PUT(spoilerChordLocationPercent); PUT(spoilerWidth); PUT(spoilerThickness);
  PUT(spoilerFrameRailWidth); PUT(spoilerSupportRailHeight);
  PUT(spoilerLighteningHoles); PUT(spoilerMinimumWoodMargin);
  PUT(spoilerMinimumCircleDistance);
  PUT(wiringHoles); PUT(wiringHoleStartRib); PUT(wiringHoleEndRib);
  PUT(wiringHoleChordLocationPercent); PUT(wiringHoleWidth); PUT(wiringHoleHeight);
  PUT(addRib1a); PUT(centerSparWoodJoiner); PUT(behindSparJoiner); PUT(behindSparJoinerType);
  PUT(behindSparJoinerOd); PUT(behindSparJoinerId); PUT(fiftyPercentJoiner);
  PUT(fiftyPercentJoinerType); PUT(fiftyPercentJoinerOd); PUT(fiftyPercentJoinerId);
  PUT(sparShearWebs); PUT(joinerPanelMode);
#undef PUT
  QJsonObject sparDefaults;
#define PUT_SPAR(name) sparDefaults.insert(#name, d.sparDefaults.name)
  PUT_SPAR(chordLocationPercent); PUT_SPAR(verticalLocation); PUT_SPAR(material);
  PUT_SPAR(type); PUT_SPAR(woodHeight); PUT_SPAR(woodWidth); PUT_SPAR(tubeOd);
  PUT_SPAR(tubeId); PUT_SPAR(rodOd); PUT_SPAR(stripWidth); PUT_SPAR(stripThickness);
  PUT_SPAR(shearWebThickness);
#undef PUT_SPAR
  o.insert("sparDefaults", sparDefaults);
  QJsonArray spars;
  for (const auto& spar : d.spars) {
    QJsonObject item;
#define PUT_ITEM(name) item.insert(#name, spar.name)
    PUT_ITEM(chordLocationPercent); PUT_ITEM(verticalLocation); PUT_ITEM(material);
    PUT_ITEM(type); PUT_ITEM(woodHeight); PUT_ITEM(woodWidth); PUT_ITEM(tubeOd);
    PUT_ITEM(tubeId); PUT_ITEM(rodOd); PUT_ITEM(stripWidth); PUT_ITEM(stripThickness);
#undef PUT_ITEM
    spars.append(item);
  }
  o.insert("spars", spars);
  QJsonArray fixedJoiners;
  for (const auto& joiner : d.fixedJoiners) {
    QJsonObject item;
#define PUT_FIXED(name) item.insert(#name, joiner.name)
    PUT_FIXED(chordLocationPercent); PUT_FIXED(material); PUT_FIXED(woodThickness);
    PUT_FIXED(carbonType); PUT_FIXED(carbonTubeOd); PUT_FIXED(carbonTubeId);
    PUT_FIXED(carbonRodOd); PUT_FIXED(steelRodOd);
    item.insert("woodThicknessUnit", static_cast<int>(joiner.woodThicknessUnit));
    item.insert("carbonTubeOdUnit", static_cast<int>(joiner.carbonTubeOdUnit));
    item.insert("carbonTubeIdUnit", static_cast<int>(joiner.carbonTubeIdUnit));
    item.insert("carbonRodOdUnit", static_cast<int>(joiner.carbonRodOdUnit));
    item.insert("steelRodOdUnit", static_cast<int>(joiner.steelRodOdUnit));
#undef PUT_FIXED
    fixedJoiners.append(item);
  }
  o.insert("fixedJoiners", fixedJoiners);
  QJsonArray removableJoiners;
  for (const auto& joiner : d.removableJoiners) {
    QJsonObject item;
#define PUT_REMOVABLE(name) item.insert(#name, joiner.name)
    PUT_REMOVABLE(kind); PUT_REMOVABLE(chordLocationPercent);
    PUT_REMOVABLE(thisPanelPart); PUT_REMOVABLE(adjoiningPanelPart); PUT_REMOVABLE(thisRodMaterial);
    PUT_REMOVABLE(thisRodOd); PUT_REMOVABLE(thisSleeveMaterial); PUT_REMOVABLE(thisSleeveOd);
    PUT_REMOVABLE(adjoiningRodMaterial); PUT_REMOVABLE(adjoiningRodOd);
    PUT_REMOVABLE(adjoiningSleeveMaterial); PUT_REMOVABLE(adjoiningSleeveOd);
    PUT_REMOVABLE(alignmentMode); PUT_REMOVABLE(pinHoleThisPart);
    PUT_REMOVABLE(pinMaterial); PUT_REMOVABLE(pinOd);
    item.insert("thisRodOdUnit", static_cast<int>(joiner.thisRodOdUnit));
    item.insert("thisSleeveOdUnit", static_cast<int>(joiner.thisSleeveOdUnit));
    item.insert("adjoiningRodOdUnit", static_cast<int>(joiner.adjoiningRodOdUnit));
    item.insert("adjoiningSleeveOdUnit", static_cast<int>(joiner.adjoiningSleeveOdUnit));
    item.insert("pinOdUnit", static_cast<int>(joiner.pinOdUnit));
#undef PUT_REMOVABLE
    removableJoiners.append(item);
  }
  o.insert("removableJoiners", removableJoiners);
  o.insert("leadingEdgeType", d.leadingEdgeType == 1 ? 2 : d.leadingEdgeType);
  o.insert("trailingEdgeType", d.trailingEdgeType == 1 ? 2 : d.trailingEdgeType);
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
  READ_B(ribLighteningHoles); READ_I(ribLighteningStartRib);
  READ_I(ribLighteningStopRib); READ_D(ribLighteningMinimumWoodMargin);
  READ_D(ribLighteningMinimumHoleDistance);
  READ_B(riblets); READ_I(ribletStartRib); READ_I(ribletEndRib);
  READ_I(ribletsPerBay);
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
  READ_B(spoilers); READ_I(spoilerStartRib); READ_I(spoilerEndRib);
  READ_I(spoilerChordLocationPercent); READ_D(spoilerWidth); READ_D(spoilerThickness);
  READ_D(spoilerFrameRailWidth); READ_D(spoilerSupportRailHeight);
  READ_B(spoilerLighteningHoles); READ_D(spoilerMinimumWoodMargin);
  READ_D(spoilerMinimumCircleDistance);
  READ_B(wiringHoles); READ_I(wiringHoleStartRib); READ_I(wiringHoleEndRib);
  READ_I(wiringHoleChordLocationPercent); READ_D(wiringHoleWidth); READ_D(wiringHoleHeight);
  READ_B(addRib1a); READ_B(centerSparWoodJoiner); READ_B(behindSparJoiner);
  READ_I(behindSparJoinerType); READ_D(behindSparJoinerOd); READ_D(behindSparJoinerId);
  READ_B(fiftyPercentJoiner); READ_I(fiftyPercentJoinerType);
  READ_D(fiftyPercentJoinerOd); READ_D(fiftyPercentJoinerId);
  READ_B(sparShearWebs); READ_I(joinerPanelMode);
#undef READ_D
#undef READ_I
#undef READ_B
  const auto sparDefaults = o.value("sparDefaults").toObject();
#define READ_SPAR_D(name) if (sparDefaults.contains(#name)) d.sparDefaults.name = sparDefaults.value(#name).toDouble(d.sparDefaults.name)
#define READ_SPAR_I(name) if (sparDefaults.contains(#name)) d.sparDefaults.name = sparDefaults.value(#name).toInt(d.sparDefaults.name)
  READ_SPAR_I(chordLocationPercent); READ_SPAR_I(verticalLocation);
  READ_SPAR_I(material); READ_SPAR_I(type); READ_SPAR_D(woodHeight);
  READ_SPAR_D(woodWidth); READ_SPAR_D(tubeOd); READ_SPAR_D(tubeId);
  READ_SPAR_D(rodOd); READ_SPAR_D(stripWidth); READ_SPAR_D(stripThickness);
  READ_SPAR_D(shearWebThickness);
#undef READ_SPAR_D
#undef READ_SPAR_I
  if (o.contains("spars")) {
    d.spars.clear();
    for (const auto value : o.value("spars").toArray()) {
      SparDefaults spar;
      const auto item = value.toObject();
#define READ_ITEM_D(name) if (item.contains(#name)) spar.name = item.value(#name).toDouble(spar.name)
#define READ_ITEM_I(name) if (item.contains(#name)) spar.name = item.value(#name).toInt(spar.name)
      READ_ITEM_I(chordLocationPercent); READ_ITEM_I(verticalLocation);
      READ_ITEM_I(material); READ_ITEM_I(type); READ_ITEM_D(woodHeight);
      READ_ITEM_D(woodWidth); READ_ITEM_D(tubeOd); READ_ITEM_D(tubeId);
      READ_ITEM_D(rodOd); READ_ITEM_D(stripWidth); READ_ITEM_D(stripThickness);
#undef READ_ITEM_D
#undef READ_ITEM_I
      if (spar.material != 0) spar.material = 1;
      d.spars.push_back(spar);
    }
  } else if (o.contains("sparDefaults")) {
    // Intermediate builds saved the new Spar Defaults object before panel
    // spar rows themselves were persisted. Migrate that seed into the one
    // default row the UI displayed when those defaults were authored.
    d.spars = {d.sparDefaults};
  } else {
    d.spars.clear();
    const auto addWoodSpar = [&d](const int chordLocationPercent,
                                  const int verticalLocation,
                                  const double height, const double width) {
      SparDefaults spar;
      spar.chordLocationPercent = chordLocationPercent;
      spar.verticalLocation = verticalLocation;
      spar.material = 0;
      spar.woodHeight = height;
      spar.woodWidth = width;
      d.spars.push_back(spar);
    };
    if (d.topSpar)
      addWoodSpar(25, 0, d.topSparHeight, d.topSparWidth);
    if (d.bottomSpar)
      addWoodSpar(25, 1, d.bottomSparHeight, d.bottomSparWidth);
    if (d.carbonSpar == 1 || d.carbonSpar == 2) {
      SparDefaults spar;
      spar.chordLocationPercent = 25;
      spar.verticalLocation = 2;
      spar.material = 1;
      spar.type = d.carbonSpar == 1 ? 0 : 1;
      spar.tubeOd = d.cfTubeOd;
      spar.tubeId = d.cfTubeId;
      spar.rodOd = d.cfRodOd;
      d.spars.push_back(spar);
    }
    if (d.topRearSpar)
      addWoodSpar(60, 0, d.topRearSparHeight, d.topRearSparWidth);
    if (d.bottomRearSpar)
      addWoodSpar(60, 1, d.bottomRearSparHeight, d.bottomRearSparWidth);
    if (!d.spars.empty()) d.sparDefaults = d.spars.front();
    if (d.shearWebs && d.topSpar && d.bottomSpar) {
      d.sparShearWebs = true;
      d.sparDefaults.shearWebThickness = d.shearWebWidth;
    }
  }
  for (const auto value : o.value("fixedJoiners").toArray()) {
    FixedJoinerData joiner;
    const auto item = value.toObject();
#define READ_FIXED_D(name) if (item.contains(#name)) joiner.name = item.value(#name).toDouble(joiner.name)
#define READ_FIXED_I(name) if (item.contains(#name)) joiner.name = item.value(#name).toInt(joiner.name)
    READ_FIXED_I(chordLocationPercent); READ_FIXED_I(material); READ_FIXED_D(woodThickness);
    READ_FIXED_I(carbonType); READ_FIXED_D(carbonTubeOd); READ_FIXED_D(carbonTubeId);
    READ_FIXED_D(carbonRodOd); READ_FIXED_D(steelRodOd);
    if (item.contains("woodThicknessUnit"))
      joiner.woodThicknessUnit = static_cast<UnitOverride>(
          item.value("woodThicknessUnit").toInt());
    if (item.contains("carbonTubeOdUnit"))
      joiner.carbonTubeOdUnit = static_cast<UnitOverride>(
          item.value("carbonTubeOdUnit").toInt());
    if (item.contains("carbonTubeIdUnit"))
      joiner.carbonTubeIdUnit = static_cast<UnitOverride>(
          item.value("carbonTubeIdUnit").toInt());
    if (item.contains("carbonRodOdUnit"))
      joiner.carbonRodOdUnit = static_cast<UnitOverride>(
          item.value("carbonRodOdUnit").toInt());
    if (item.contains("steelRodOdUnit"))
      joiner.steelRodOdUnit = static_cast<UnitOverride>(
          item.value("steelRodOdUnit").toInt());
#undef READ_FIXED_D
#undef READ_FIXED_I
    d.fixedJoiners.push_back(joiner);
  }
  for (const auto value : o.value("removableJoiners").toArray()) {
    RemovableJoinerData joiner;
    const auto item = value.toObject();
#define READ_REMOVABLE_D(name) if (item.contains(#name)) joiner.name = item.value(#name).toDouble(joiner.name)
#define READ_REMOVABLE_I(name) if (item.contains(#name)) joiner.name = item.value(#name).toInt(joiner.name)
    READ_REMOVABLE_I(kind); READ_REMOVABLE_I(chordLocationPercent);
    READ_REMOVABLE_I(thisPanelPart);
    if (item.contains("adjoiningPanelPart"))
      joiner.adjoiningPanelPart = item.value("adjoiningPanelPart").toInt(joiner.adjoiningPanelPart);
    else
      joiner.adjoiningPanelPart = 1 - std::clamp(joiner.thisPanelPart, 0, 1);
    READ_REMOVABLE_I(thisRodMaterial); READ_REMOVABLE_D(thisRodOd);
    READ_REMOVABLE_I(thisSleeveMaterial); READ_REMOVABLE_D(thisSleeveOd);
    READ_REMOVABLE_I(adjoiningRodMaterial); READ_REMOVABLE_D(adjoiningRodOd);
    READ_REMOVABLE_I(adjoiningSleeveMaterial); READ_REMOVABLE_D(adjoiningSleeveOd);
    READ_REMOVABLE_I(alignmentMode); READ_REMOVABLE_I(pinHoleThisPart);
    READ_REMOVABLE_I(pinMaterial); READ_REMOVABLE_D(pinOd);
    if (item.contains("thisRodOdUnit"))
      joiner.thisRodOdUnit = static_cast<UnitOverride>(
          item.value("thisRodOdUnit").toInt());
    if (item.contains("thisSleeveOdUnit"))
      joiner.thisSleeveOdUnit = static_cast<UnitOverride>(
          item.value("thisSleeveOdUnit").toInt());
    if (item.contains("adjoiningRodOdUnit"))
      joiner.adjoiningRodOdUnit = static_cast<UnitOverride>(
          item.value("adjoiningRodOdUnit").toInt());
    if (item.contains("adjoiningSleeveOdUnit"))
      joiner.adjoiningSleeveOdUnit = static_cast<UnitOverride>(
          item.value("adjoiningSleeveOdUnit").toInt());
    if (item.contains("pinOdUnit"))
      joiner.pinOdUnit = static_cast<UnitOverride>(
          item.value("pinOdUnit").toInt());
#undef READ_REMOVABLE_D
#undef READ_REMOVABLE_I
    if (!item.contains("chordLocationPercent") && joiner.kind == 1)
      joiner.chordLocationPercent = 70;
    d.removableJoiners.push_back(joiner);
  }
  const bool hasCurrentJoinerData = o.contains("joinerPanelMode") ||
      o.contains("fixedJoiners") || o.contains("removableJoiners");
  if (hasCurrentJoinerData) {
    // Current Joiners-tab data supersedes these hidden legacy controls.
    // Older saved projects can contain both after being resaved; retaining an
    // enabled legacy flag would make validation reject a joiner that the GUI
    // neither displays nor builds.
    d.centerSparWoodJoiner = false;
    d.behindSparJoiner = false;
    d.fiftyPercentJoiner = false;
  } else {
    const auto supportedLegacyType = [](const bool enabled, const int type) {
      return !enabled || type == 1 || type == 2;
    };
    const bool hasLegacyCircularJoiner =
        d.behindSparJoiner || d.fiftyPercentJoiner;
    const bool canMigrateToFixedJoiners = !d.centerSparWoodJoiner &&
        supportedLegacyType(d.behindSparJoiner, d.behindSparJoinerType) &&
        supportedLegacyType(d.fiftyPercentJoiner, d.fiftyPercentJoinerType);
    if (hasLegacyCircularJoiner && canMigrateToFixedJoiners) {
      const auto migrate = [&d](const bool enabled, const int type,
                                const int chordLocationPercent,
                                const double outerDiameter,
                                const double innerDiameter) {
        if (!enabled) return;
        FixedJoinerData joiner;
        joiner.chordLocationPercent = chordLocationPercent;
        joiner.material = 1;
        joiner.carbonType = type == 1 ? 1 : 0;
        if (joiner.carbonType == 0) {
          joiner.carbonTubeOd = outerDiameter;
          joiner.carbonTubeId = innerDiameter;
        } else {
          joiner.carbonRodOd = outerDiameter;
        }
        d.fixedJoiners.push_back(joiner);
      };
      migrate(d.behindSparJoiner, d.behindSparJoinerType, 30,
              d.behindSparJoinerOd, d.behindSparJoinerId);
      migrate(d.fiftyPercentJoiner, d.fiftyPercentJoinerType, 60,
              d.fiftyPercentJoinerOd, d.fiftyPercentJoinerId);
      d.joinerPanelMode = 1;
    }
  }
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
  // Legacy projects used type 1 for the removed shaped-stock choices.
  // Preserve those designs by loading them as the remaining wood-stock types.
  if (d.leadingEdgeType == 1) d.leadingEdgeType = 2;
  if (d.trailingEdgeType == 1) d.trailingEdgeType = 2;
  return d;
}

WingPanelData roundedInchPanelData(const WingPanelData& metricData) {
  WingPanelData rounded = metricData;
  const auto roundLength = [](double& millimetres) {
    const double inches = millimetres / kMmPerInch;
    millimetres = (std::round(inches * 32.0) / 32.0) * kMmPerInch;
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
  ROUND_LENGTH(spoilerWidth); ROUND_LENGTH(spoilerThickness);
  ROUND_LENGTH(spoilerFrameRailWidth); ROUND_LENGTH(spoilerSupportRailHeight);
  ROUND_LENGTH(spoilerMinimumWoodMargin);
  ROUND_LENGTH(spoilerMinimumCircleDistance);
  ROUND_LENGTH(ribLighteningMinimumWoodMargin);
  ROUND_LENGTH(ribLighteningMinimumHoleDistance);
  ROUND_LENGTH(wiringHoleWidth); ROUND_LENGTH(wiringHoleHeight);
  ROUND_LENGTH(behindSparJoinerOd); ROUND_LENGTH(behindSparJoinerId);
  ROUND_LENGTH(fiftyPercentJoinerOd); ROUND_LENGTH(fiftyPercentJoinerId);
#undef ROUND_LENGTH
  roundLength(rounded.sparDefaults.woodHeight);
  roundLength(rounded.sparDefaults.woodWidth);
  roundLength(rounded.sparDefaults.tubeOd);
  roundLength(rounded.sparDefaults.tubeId);
  roundLength(rounded.sparDefaults.rodOd);
  roundLength(rounded.sparDefaults.stripWidth);
  roundLength(rounded.sparDefaults.stripThickness);
  roundLength(rounded.sparDefaults.shearWebThickness);
  for (auto& spar : rounded.spars) {
    roundLength(spar.woodHeight); roundLength(spar.woodWidth);
    roundLength(spar.tubeOd); roundLength(spar.tubeId); roundLength(spar.rodOd);
    roundLength(spar.stripWidth); roundLength(spar.stripThickness);
  }
  for (auto& joiner : rounded.fixedJoiners) {
    roundLength(joiner.woodThickness); roundLength(joiner.carbonTubeOd);
    roundLength(joiner.carbonTubeId); roundLength(joiner.carbonRodOd);
    roundLength(joiner.steelRodOd);
  }
  for (auto& joiner : rounded.removableJoiners) {
    roundLength(joiner.thisRodOd); roundLength(joiner.thisSleeveOd);
    roundLength(joiner.adjoiningRodOd); roundLength(joiner.adjoiningSleeveOd);
    roundLength(joiner.pinOd);
  }
  return rounded;
}

WingPanelData installedDefaultPanelData(const DisplayUnit unit) {
  WingPanelData defaults;
  defaults.leadingEdgeType = 2;
  defaults.trailingEdgeType = 2;
  defaults.leTopSheetStopRib = 1;
  defaults.leBottomSheetStopRib = 1;
  defaults.teTopSheetStopRib = 1;
  defaults.teBottomSheetStopRib = 1;
  defaults.aileronStartRib = 1;
  defaults.aileronStopRib = 9;
  defaults.flapStartRib = 1;
  defaults.wiringHoleEndRib = defaults.ribCount;
  defaults.ribLighteningStopRib = defaults.ribCount - 1;
  defaults.ribletEndRib = defaults.ribCount;
  for (const auto* key : {
           "aileronHeight", "aileronWidth", "bottomRearSparHeight",
           "bottomRearSparWidth", "bottomSparHeight", "bottomSparWidth",
           "flapHeight", "flapWidth", "leBottomSheetThickness",
           "leTopSheetThickness", "leadingEdgeHeight", "panelSpan",
           "ribLighteningMinimumHoleDistance",
           "ribLighteningMinimumWoodMargin", "ribThickness", "rootChord",
           "shearWebWidth", "sweep",
           "teBottomSheetThickness", "teTopSheetThickness", "tipChord",
           "topRearSparHeight", "topRearSparWidth", "topSparHeight",
           "topSparWidth", "trailingEdgeHeight", "trailingEdgeWidth",
           "turbulatorHeight", "turbulatorWidth", "wiringHoleWidth",
           "wiringHoleHeight"})
    defaults.unitOverrides.insert(key, UnitOverride::Global);
  for (const auto* key : {
           "cfRodOd", "cfTubeId", "cfTubeOd", "leadingEdgeRodOd",
           "leadingEdgeTubeId", "leadingEdgeTubeOd"})
    defaults.unitOverrides.insert(key, UnitOverride::Millimeters);
  defaults.unitOverrides.insert("leadingEdgeWidth", UnitOverride::Inches);
  if (unit == DisplayUnit::Millimeters) return defaults;

  defaults.panelSpan = 698.5;
  defaults.rootChord = 254.0;
  defaults.tipChord = 152.4;
  defaults.sweep = 25.4;
  defaults.ribThickness = 2.38125;
  defaults.ribCount = 11;
  defaults.ribLighteningStopRib = defaults.ribCount - 1;
  defaults.ribletEndRib = defaults.ribCount;
  defaults.aileronStartRib = 2;
  defaults.aileronStopRib = 8;
  defaults.flapStartRib = 2;
  defaults.flapStopRib = 5;
  defaults.wiringHoleEndRib = 8;
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
  defaults.sparDefaults.chordLocationPercent = 25;
  defaults.sparDefaults.verticalLocation = 0;
  defaults.sparDefaults.material = 0;
  defaults.sparDefaults.type = 2;
  defaults.sparDefaults.woodHeight = 3.175;
  defaults.sparDefaults.woodWidth = 6.35;
  defaults.sparDefaults.shearWebThickness = 3.175;
  auto bottomSpar = defaults.sparDefaults;
  bottomSpar.verticalLocation = 1;
  bottomSpar.type = 0;
  defaults.spars = {defaults.sparDefaults, bottomSpar};
  defaults.leTopSheetThickness = 1.5875;
  defaults.leBottomSheetThickness = 1.5875;
  defaults.teTopSheetThickness = 1.5875;
  defaults.teBottomSheetThickness = 1.5875;
  defaults.leTopSheetStopRib = 2;
  defaults.leBottomSheetStopRib = 2;
  defaults.teTopSheetStopRib = 2;
  defaults.teBottomSheetStopRib = 2;
  defaults.turbulatorHeight = 3.175;
  defaults.turbulatorWidth = 3.175;
  defaults.leadingEdgeWidth = 4.7625;
  defaults.leadingEdgeHeight = 15.875;
  defaults.trailingEdgeWidth = 25.4;
  defaults.trailingEdgeHeight = 9.525;
  defaults.aileronWidth = 34.925;
  defaults.aileronHeight = 9.525;
  defaults.aileronHingePostWidth = 6.35;
  defaults.aileronHingePostHeight = 9.525;
  defaults.flapWidth = 38.1;
  defaults.flapHeight = 9.525;
  defaults.flapHingePostWidth = 6.35;
  defaults.flapHingePostHeight = 9.525;
  defaults.spoilerWidth = 25.4;
  defaults.spoilerThickness = 3.175;
  defaults.spoilerFrameRailWidth = 6.35;
  defaults.spoilerSupportRailHeight = 3.175;
  defaults.spoilerMinimumWoodMargin = 7.9375;
  defaults.spoilerMinimumCircleDistance = 12.7;
  defaults.ribLighteningMinimumWoodMargin = 7.9375;
  defaults.ribLighteningMinimumHoleDistance = 12.7;
  defaults.centerSparWoodJoiner = false;
  defaults.behindSparJoinerType = 3;
  defaults.behindSparJoinerOd = 7.0;
  defaults.behindSparJoinerId = 6.0;
  defaults.fiftyPercentJoinerType = 2;
  defaults.joinerPanelMode = 1;
  FixedJoinerData fixedJoiner;
  fixedJoiner.chordLocationPercent = 25;
  fixedJoiner.material = 0;
  fixedJoiner.woodThickness = 3.175;
  fixedJoiner.carbonType = 0;
  fixedJoiner.carbonTubeOd = 6.0;
  fixedJoiner.carbonTubeId = 5.0;
  fixedJoiner.carbonRodOd = 6.0;
  fixedJoiner.steelRodOd = 6.0;
  fixedJoiner.carbonTubeOdUnit = UnitOverride::Millimeters;
  fixedJoiner.carbonTubeIdUnit = UnitOverride::Millimeters;
  fixedJoiner.carbonRodOdUnit = UnitOverride::Millimeters;
  fixedJoiner.steelRodOdUnit = UnitOverride::Millimeters;
  auto carbonRodJoiner = fixedJoiner;
  carbonRodJoiner.carbonType = 1;
  carbonRodJoiner.steelRodOdUnit = UnitOverride::Global;
  defaults.fixedJoiners = {fixedJoiner, carbonRodJoiner};

  RemovableJoinerData sleeveRod;
  sleeveRod.kind = 0;
  sleeveRod.chordLocationPercent = 35;
  sleeveRod.thisPanelPart = 1;
  sleeveRod.adjoiningPanelPart = 0;
  sleeveRod.thisRodMaterial = 0;
  sleeveRod.thisRodOd = 5.0;
  sleeveRod.thisSleeveMaterial = 1;
  sleeveRod.thisSleeveOd = 6.0;
  sleeveRod.adjoiningRodMaterial = 0;
  sleeveRod.adjoiningRodOd = 6.0;
  sleeveRod.adjoiningSleeveMaterial = 0;
  sleeveRod.adjoiningSleeveOd = 7.0;
  sleeveRod.thisRodOdUnit = UnitOverride::Millimeters;
  sleeveRod.thisSleeveOdUnit = UnitOverride::Millimeters;
  sleeveRod.adjoiningRodOdUnit = UnitOverride::Millimeters;
  sleeveRod.adjoiningSleeveOdUnit = UnitOverride::Millimeters;

  RemovableJoinerData alignmentPin;
  alignmentPin.kind = 1;
  alignmentPin.chordLocationPercent = 70;
  alignmentPin.thisPanelPart = 1;
  alignmentPin.adjoiningPanelPart = 0;
  alignmentPin.thisRodMaterial = 0;
  alignmentPin.thisRodOd = 2.0;
  alignmentPin.thisSleeveMaterial = 0;
  alignmentPin.thisSleeveOd = 3.0;
  alignmentPin.adjoiningRodMaterial = 0;
  alignmentPin.adjoiningRodOd = 2.0;
  alignmentPin.adjoiningSleeveMaterial = 0;
  alignmentPin.adjoiningSleeveOd = 3.0;
  alignmentPin.alignmentMode = 0;
  alignmentPin.pinHoleThisPart = 1;
  alignmentPin.pinMaterial = 1;
  alignmentPin.pinOd = 2.0;
  alignmentPin.thisRodOdUnit = UnitOverride::Millimeters;
  alignmentPin.thisSleeveOdUnit = UnitOverride::Millimeters;
  alignmentPin.adjoiningRodOdUnit = UnitOverride::Millimeters;
  alignmentPin.adjoiningSleeveOdUnit = UnitOverride::Millimeters;
  alignmentPin.pinOdUnit = UnitOverride::Millimeters;
  defaults.removableJoiners = {sleeveRod, alignmentPin};
  defaults.unitOverrides.clear();
  for (const auto* key : {
           "aileronHeight", "aileronHingePostHeight", "aileronHingePostWidth",
           "aileronWidth", "bottomRearSparHeight", "bottomRearSparWidth",
           "bottomSparHeight", "bottomSparWidth", "flapHeight",
           "flapHingePostHeight", "flapHingePostWidth", "flapWidth",
           "leBottomSheetThickness", "leTopSheetThickness", "leadingEdgeHeight",
           "panelSpan", "ribLighteningMinimumHoleDistance",
           "ribLighteningMinimumWoodMargin", "ribThickness", "rootChord",
           "shearWebWidth",
           "sparShearWebThickness", "sparWoodHeight", "sparWoodWidth", "sweep",
           "spoilerFrameRailWidth", "spoilerMinimumCircleDistance",
           "spoilerMinimumWoodMargin",
           "spoilerThickness", "spoilerWidth",
           "teBottomSheetThickness", "teTopSheetThickness", "tipChord",
           "topRearSparHeight", "topRearSparWidth", "topSparHeight",
           "topSparWidth", "trailingEdgeHeight", "trailingEdgeWidth",
           "turbulatorHeight", "turbulatorWidth", "wiringHoleHeight",
           "wiringHoleWidth"})
    defaults.unitOverrides.insert(key, UnitOverride::Global);
  for (const auto* key : {
           "behindSparJoinerId", "behindSparJoinerOd", "cfRodOd", "cfTubeId",
           "cfTubeOd", "fiftyPercentJoinerId", "fiftyPercentJoinerOd",
           "leadingEdgeRodOd", "leadingEdgeTubeId", "leadingEdgeTubeOd",
           "sparRodOd", "sparStripThickness", "sparStripWidth", "sparTubeId",
           "sparTubeOd"})
    defaults.unitOverrides.insert(key, UnitOverride::Millimeters);
  defaults.unitOverrides.insert("leadingEdgeWidth", UnitOverride::Inches);
  return defaults;
}

QString woodJoinerSparAlignmentError(const std::vector<WingPanelData>& panels) {
  constexpr double angleToleranceDegrees = 0.05;
  const auto hasLegacyWoodJoiner = [](const WingPanelData& panel) {
    return panel.joinerPanelMode < 0 && panel.centerSparWoodJoiner;
  };
  const auto sparAngle = [](const WingPanelData& panel) {
    const double sparAdvance = panel.sweep + 0.25 * (panel.tipChord - panel.rootChord);
    return std::atan2(sparAdvance, panel.panelSpan) * 180.0 / std::numbers::pi;
  };
  if (!panels.empty() && hasLegacyWoodJoiner(panels.front()) &&
      std::abs(sparAngle(panels.front())) > angleToleranceDegrees) {
    return QString{"Panel 1 center wood joiner cannot be built because the mirrored 25% wood spars do not form a straight line through the wing center. Adjust sweep or chord, or disable the wood joiner."};
  }
  for (std::size_t panel = 1; panel < panels.size(); ++panel) {
    const auto& outer = panels[panel];
    if (!hasLegacyWoodJoiner(outer)) continue;
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
  setJoinerAddDefaults(data);
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  auto* tabs = new QTabWidget;
  tabs->setTabPosition(QTabWidget::West);
  tabs->addTab(makeSpecsPage(), "Specs");
  tabs->addTab(makeRibsPage(), "Ribs");
  tabs->addTab(makeSparsPage(), "Spars");
  tabs->addTab(makeSheetingPage(), "Sheeting");
  if (showJoinerPage_) tabs->addTab(makeJoinerPage(), "Joiners");
  tabs->addTab(makeLeadingTrailingPage(), "LE/TE");
  tabs->addTab(makeControlsPage(), "Ailerons/Flaps");
  if (showRootChord_) tabs->addTab(makeSpoilersPage(), "Spoilers");
  layout->addWidget(tabs);
  setData(data);
}

void WingPanelEditor::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  QTimer::singleShot(0, this, [this] { updateAngleInputWidths(); });
}

void WingPanelEditor::updateAngleInputWidths() {
  if (!span_ || !dihedral_ || !twist_ || !ribCount_) return;
  const int width = span_->measurementFieldWidth();
  dihedral_->setFixedWidth(width);
  twist_->setFixedWidth(width);
  ribCount_->setFixedWidth(width);
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
  dihedral_->setObjectName("dihedral");
  twist_ = angleInput(0.0);
  twist_->setObjectName("twist");
  form->addRow("Panel Span", span_);
  rootChordLabel_ = new QLabel{"Root Chord"};
  form->addRow(rootChordLabel_, rootChord_);
  rootChordLabel_->setVisible(showRootChord_); rootChord_->setVisible(showRootChord_);
  form->addRow("Tip Chord", tipChord_); form->addRow("Tip Sweep", sweep_);
  form->addRow("Dihedral", dihedral_); form->addRow("Tip Twist", twist_);
  layout->addWidget(dimensions); layout->addStretch();
  connect(rootButton, &QPushButton::clicked, this, [this] { importAirfoil(true); });
  connect(tipButton, &QPushButton::clicked, this, [this] { importAirfoil(false); });
  connect(dihedral_, &QDoubleSpinBox::valueChanged, this, [this] {
    updateConditionalControls(); emitChanged();
  });
  connect(twist_, &QDoubleSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  return scrollPage(content);
}

QWidget* WingPanelEditor::makeRibsPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  auto* ribs = new QWidget;
  ribs->setObjectName("ribsContents");
  auto* form = new QFormLayout{ribs};
  ribCount_ = new QSpinBox;
  ribCount_->setObjectName("ribCount");
  ribCount_->setRange(2, 100);
  ribSpacing_ = new QLabel;
  ribSpacing_->setObjectName("ribSpacing");
  ribThickness_ = new LengthInput{"ribThickness", 3.0};
  ribThickness_->setGlobalUnit(globalUnit_);
  ribThickness_->setOverrideSelectorVisible(showUnitOverrides_);
  lengths_.insert("ribThickness", ribThickness_);
  connect(ribThickness_, &LengthInput::valueChanged,
          this, &WingPanelEditor::emitChanged);
  form->addRow("Rib Count", ribCount_);
  form->addRow(ribSpacing_);
  form->addRow("Rib Thickness", ribThickness_);
  addRib1a_ = new QCheckBox{
      "Add Rib 1a (Adds an extra rib between ribs 1 and 2 for extra strength)"};
  addRib1a_->setObjectName("addRib1a");
  form->addRow(addRib1a_);
  addRib1a_->setVisible(showRootChord_);
  layout->addWidget(ribs);
  ribLighteningHoles_ = new QCheckBox{
      "Lightening Holes (Add this option last due to longer wing generation times)"};
  ribLighteningHoles_->setObjectName("ribLighteningHoles");
  form->addRow(ribLighteningHoles_);
  ribLighteningStartRib_ = new QSpinBox;
  ribLighteningStartRib_->setObjectName("ribLighteningStartRib");
  ribLighteningStopRib_ = new QSpinBox;
  ribLighteningStopRib_->setObjectName("ribLighteningStopRib");
  ribLighteningMinimumWoodMargin_ = new LengthInput{
      "ribLighteningMinimumWoodMargin",
      globalUnit_ == DisplayUnit::Inches ? 7.9375 : 6.0};
  ribLighteningMinimumHoleDistance_ = new LengthInput{
      "ribLighteningMinimumHoleDistance",
      globalUnit_ == DisplayUnit::Inches ? 12.7 : 12.0};
  for (auto* input : {ribLighteningMinimumWoodMargin_,
                      ribLighteningMinimumHoleDistance_}) {
    input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_);
  }
  lengths_.insert(
      "ribLighteningMinimumWoodMargin", ribLighteningMinimumWoodMargin_);
  lengths_.insert(
      "ribLighteningMinimumHoleDistance", ribLighteningMinimumHoleDistance_);
  ribLighteningHoleDetails_ = new QWidget;
  auto* lighteningLayout = new QVBoxLayout{ribLighteningHoleDetails_};
  lighteningLayout->setContentsMargins(0, 0, 0, 0);
  lighteningLayout->addWidget(
      detailRow({{"Start Rib", ribLighteningStartRib_}}));
  lighteningLayout->addWidget(
      detailRow({{"Stop Rib", ribLighteningStopRib_}}));
  lighteningLayout->addWidget(detailRow(
      {{"Min Border Distance", ribLighteningMinimumWoodMargin_}}));
  lighteningLayout->addWidget(detailRow(
      {{"Min Hole Distance", ribLighteningMinimumHoleDistance_}}));
  form->addRow(ribLighteningHoleDetails_);

  riblets_ = new QCheckBox{
      "Riblets (Riblets require CF LE and mid Spar)"};
  riblets_->setObjectName("riblets");
  form->addRow(riblets_);
  ribletStartRib_ = new QSpinBox;
  ribletStartRib_->setObjectName("ribletStartRib");
  ribletEndRib_ = new QSpinBox;
  ribletEndRib_->setObjectName("ribletEndRib");
  ribletsPerBay_ = new QSpinBox;
  ribletsPerBay_->setObjectName("ribletsPerBay");
  ribletsPerBay_->setRange(1, 5);
  ribletsPerBay_->setValue(2);
  ribletDetails_ = new QWidget;
  auto* ribletLayout = new QVBoxLayout{ribletDetails_};
  ribletLayout->setContentsMargins(0, 0, 0, 0);
  ribletLayout->addWidget(detailRow({{"Start Rib", ribletStartRib_}}));
  ribletLayout->addWidget(detailRow({{"End Rib", ribletEndRib_}}));
  ribletLayout->addWidget(detailRow({{"Riblets per Bay", ribletsPerBay_}}));
  form->addRow(ribletDetails_);
  layout->addStretch();
  connect(span_, &LengthInput::valueChanged,
          this, &WingPanelEditor::updateRibSpacing);
  connect(ribCount_, &QSpinBox::valueChanged, this, [this] {
    updateRibSpacing(); updateWiringHoleRibRanges(true);
    updateConditionalControls(); emitChanged();
  });
  connect(ribLighteningHoles_, &QCheckBox::toggled, this, [this] {
    updateConditionalControls(); emitChanged();
  });
  connect(riblets_, &QCheckBox::toggled, this, [this] {
    updateConditionalControls(); emitChanged();
  });
  for (auto* input : {ribLighteningStartRib_, ribLighteningStopRib_})
    connect(input, &QSpinBox::valueChanged,
            this, &WingPanelEditor::emitChanged);
  connect(ribLighteningMinimumWoodMargin_, &LengthInput::valueChanged,
          this, &WingPanelEditor::emitChanged);
  connect(ribLighteningMinimumHoleDistance_, &LengthInput::valueChanged,
          this, &WingPanelEditor::emitChanged);
  for (auto* input : {ribletStartRib_, ribletEndRib_, ribletsPerBay_})
    connect(input, &QSpinBox::valueChanged,
            this, &WingPanelEditor::emitChanged);
  connect(addRib1a_, &QCheckBox::toggled, this, [this] {
    updateWiringHoleRibRanges(true);
    emitChanged();
  });
  return scrollPage(content);
}

QWidget* WingPanelEditor::makeSparsPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};

  auto* buttonRow = new QHBoxLayout;
  auto* addButton = new QPushButton{"Add Spar"};
  addButton->setObjectName("addSparButton");
  auto* deleteButton = new QPushButton{"Delete Checked"};
  deleteButton->setObjectName("deleteSparButton");
  buttonRow->addWidget(addButton);
  buttonRow->addWidget(deleteButton);
  buttonRow->addStretch();
  layout->addLayout(buttonRow);
  connect(addButton, &QPushButton::clicked, this, &WingPanelEditor::addSparEditor);
  connect(deleteButton, &QPushButton::clicked,
          this, &WingPanelEditor::deleteSelectedSparEditors);

  auto* sparEditors = new QWidget;
  sparEditorsLayout_ = new QVBoxLayout{sparEditors};
  sparEditorsLayout_->setContentsMargins(0, 0, 0, 0);
  sparEditorsLayout_->setSpacing(8);
  layout->addWidget(sparEditors);

  auto* separator = new QFrame;
  separator->setFrameShape(QFrame::HLine);
  layout->addWidget(separator);
  sparShearWebs_ = new QCheckBox{"Shear Webs"};
  sparShearWebs_->setObjectName("sparShearWebs");
  sparShearWebThickness_ = new LengthInput{
      "sparShearWebThickness", airfoilData_.sparDefaults.shearWebThickness};
  sparShearWebThickness_->setGlobalUnit(globalUnit_);
  sparShearWebThickness_->setUnitOverride(
      airfoilData_.unitOverrides.value("sparShearWebThickness", UnitOverride::Global));
  sparShearWebThickness_->setOverrideSelectorVisible(showUnitOverrides_);
  sparShearWebDetails_ = detailRow({{"Thickness", sparShearWebThickness_}});
  layout->addWidget(sparShearWebs_);
  layout->addWidget(sparShearWebDetails_);
  layout->addStretch();

  connect(sparShearWebs_, &QCheckBox::toggled, this, [this] {
    updateSparEditorControls();
    emitChanged();
  });
  addSparEditor();

  // Keep the current backend controls alive but outside the visible page. The
  // dynamic spar editor is intentionally frontend-only until its data model and
  // geometry processing are implemented.
  auto* legacyContent = new QWidget{content};
  legacyContent->hide();
  auto* legacyLayout = new QVBoxLayout{legacyContent};
  auto makeLegacyLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value}; input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_); lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged); return input;
  };
  auto addCheck = [this, legacyLayout](const QString& label, QCheckBox*& check, QWidget*& details) {
    check = new QCheckBox{label}; legacyLayout->addWidget(check);
    connect(check, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
    details = nullptr;
  };
  addCheck("Top Spar", topSpar_, topSparDetails_);
  topSparHeight_ = makeLegacyLength("topSparHeight", 5); topSparWidth_ = makeLegacyLength("topSparWidth", 10);
  topSparDetails_ = detailRow({{"Height", topSparHeight_}, {"Width", topSparWidth_}}); legacyLayout->addWidget(topSparDetails_);
  addCheck("Bottom Spar", bottomSpar_, bottomSparDetails_);
  bottomSparHeight_ = makeLegacyLength("bottomSparHeight", 5); bottomSparWidth_ = makeLegacyLength("bottomSparWidth", 10);
  bottomSparDetails_ = detailRow({{"Height", bottomSparHeight_}, {"Width", bottomSparWidth_}}); legacyLayout->addWidget(bottomSparDetails_);
  addCheck("Shear Webs", shearWebs_, shearDetails_);
  shearWebWidth_ = makeLegacyLength("shearWebWidth", 3); shearDetails_ = detailRow({{"Thickness", shearWebWidth_}}); legacyLayout->addWidget(shearDetails_);

  auto* carbonGroup = new QButtonGroup{this}; carbonGroup->setExclusive(true);
  cfTube_ = new QRadioButton{"CF Tube"}; cfRod_ = new QRadioButton{"CF Rod"};
  carbonGroup->addButton(cfTube_); carbonGroup->addButton(cfRod_);
  legacyLayout->addWidget(cfTube_); cfTubeOd_ = makeLegacyLength("cfTubeOd", 6); cfTubeId_ = makeLegacyLength("cfTubeId", 5);
  cfTubeDetails_ = detailRow({{"OD", cfTubeOd_}, {"ID", cfTubeId_}}); legacyLayout->addWidget(cfTubeDetails_);
  legacyLayout->addWidget(cfRod_); cfRodOd_ = makeLegacyLength("cfRodOd", 6);
  cfRodDetails_ = detailRow({{"OD", cfRodOd_}}); legacyLayout->addWidget(cfRodDetails_);
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

  turbulators_ = new QCheckBox{"Turbulators"}; legacyLayout->addWidget(turbulators_);
  turbulatorCount_ = new QSpinBox; turbulatorCount_->setRange(1, 4);
  turbulatorHeight_ = makeLegacyLength("turbulatorHeight", 2); turbulatorWidth_ = makeLegacyLength("turbulatorWidth", 2);
  turbulatorDetails_ = detailRow({{"Count", turbulatorCount_}, {"Height", turbulatorHeight_}, {"Width", turbulatorWidth_}});
  legacyLayout->addWidget(turbulatorDetails_);
  connect(turbulators_, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  connect(turbulatorCount_, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  auto* line2 = new QFrame; line2->setFrameShape(QFrame::HLine); legacyLayout->addWidget(line2);
  addCheck("Top 60% Rear Spar", topRearSpar_, topRearDetails_);
  topRearHeight_ = makeLegacyLength("topRearSparHeight", 4); topRearWidth_ = makeLegacyLength("topRearSparWidth", 4);
  topRearDetails_ = detailRow({{"Height", topRearHeight_}, {"Width", topRearWidth_}}); legacyLayout->addWidget(topRearDetails_);
  addCheck("Bottom 60% Rear Spar", bottomRearSpar_, bottomRearDetails_);
  bottomRearHeight_ = makeLegacyLength("bottomRearSparHeight", 4); bottomRearWidth_ = makeLegacyLength("bottomRearSparWidth", 4);
  bottomRearDetails_ = detailRow({{"Height", bottomRearHeight_}, {"Width", bottomRearWidth_}}); legacyLayout->addWidget(bottomRearDetails_);
  return scrollPage(content);
}

QWidget* WingPanelEditor::makeSheetingPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  const auto makeLength = [this](const QString& key, const double value) {
    auto* input = new LengthInput{key, value};
    input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_);
    lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged);
    return input;
  };
  const auto addSheet = [&, this](const QString& label, const QString& key,
                                  QCheckBox*& check, LengthInput*& value,
                                  QSpinBox*& stopRib, QWidget*& details) {
    check = new QCheckBox{label};
    value = makeLength(key, 2.0);
    stopRib = new QSpinBox;
    stopRib->setRange(2, 100);
    stopRib->setValue(2);
    details = detailRow({{"Thickness", value}, {"Stop Rib Number", stopRib}});
    layout->addWidget(check);
    layout->addWidget(details);
    connect(check, &QCheckBox::toggled, this, [this] {
      updateConditionalControls();
      emitChanged();
    });
    connect(stopRib, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  };
  addSheet("LE Top Sheeting", "leTopSheetThickness", leTopSheet_,
           leTopSheetThickness_, leTopSheetStopRib_, leTopSheetDetails_);
  addSheet("LE Bottom Sheeting", "leBottomSheetThickness", leBottomSheet_,
           leBottomSheetThickness_, leBottomSheetStopRib_, leBottomSheetDetails_);
  addSheet("TE Top Sheeting", "teTopSheetThickness", teTopSheet_,
           teTopSheetThickness_, teTopSheetStopRib_, teTopSheetDetails_);
  addSheet("TE Bottom Sheeting", "teBottomSheetThickness", teBottomSheet_,
           teBottomSheetThickness_, teBottomSheetStopRib_, teBottomSheetDetails_);
  layout->addStretch();
  return scrollPage(content);
}

void WingPanelEditor::addSparEditor() {
  if (!sparEditorsLayout_) return;
  const int id = nextSparEditorId_++;
  SparEditorWidgets row;
  auto* group = new QGroupBox;
  group->setObjectName("sparEditorRow");
  auto* layout = new QVBoxLayout{group};
  row.container = group;
  row.deleteSelection = new QCheckBox{group};
  row.deleteSelection->setObjectName("sparDeleteSelection");
  layout->addWidget(row.deleteSelection);

  row.chordLocation = new QSpinBox;
  row.chordLocation->setObjectName("fixedJoinerChordLocation");
  row.chordLocation->setObjectName("sparChordLocation");
  row.chordLocation->setRange(0, 90);
  row.chordLocation->setSingleStep(1);
  row.chordLocation->setSuffix("%");
  row.chordLocation->setValue(25);
  layout->addWidget(detailRow({{"Chord Location", row.chordLocation}}));

  row.top = new QRadioButton{"Top"};
  row.bottom = new QRadioButton{"Bottom"};
  row.mid = new QRadioButton{"Mid"};
  row.mid->setChecked(true);
  auto* verticalGroup = new QButtonGroup{group};
  verticalGroup->addButton(row.top);
  verticalGroup->addButton(row.bottom);
  verticalGroup->addButton(row.mid);
  layout->addWidget(detailRow({{"Vertical Location", row.top}, {"", row.bottom}, {"", row.mid}}));

  row.wood = new QRadioButton{"Wood"};
  row.carbonFiber = new QRadioButton{"CF"};
  row.carbonFiber->setChecked(true);
  auto* materialGroup = new QButtonGroup{group};
  materialGroup->addButton(row.wood);
  materialGroup->addButton(row.carbonFiber);
  layout->addWidget(detailRow({{"Material", row.wood}, {"", row.carbonFiber}}));

  const QString keyPrefix = QString{"sparEditor%1"}.arg(id);
  const auto makeLength = [this, &keyPrefix](const QString& suffix, const double value) {
    auto* input = new LengthInput{keyPrefix + suffix, value};
    input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged);
    return input;
  };
  row.woodHeight = makeLength("WoodHeight", 5.0);
  row.woodWidth = makeLength("WoodWidth", 9.0);
  row.woodDetails = detailRow({{"Height", row.woodHeight}, {"Width", row.woodWidth}});
  layout->addWidget(row.woodDetails);

  row.tube = new QRadioButton{"Tube"};
  row.rod = new QRadioButton{"Rod"};
  row.strip = new QRadioButton{"Strip"};
  row.tube->setChecked(true);
  auto* typeGroup = new QButtonGroup{group};
  typeGroup->addButton(row.tube);
  typeGroup->addButton(row.rod);
  typeGroup->addButton(row.strip);
  row.typeDetails = detailRow({{"Type", row.tube}, {"", row.rod}, {"", row.strip}});
  layout->addWidget(row.typeDetails);

  row.tubeOd = makeLength("TubeOd", 6.0);
  row.tubeId = makeLength("TubeId", 5.0);
  row.tubeDetails = detailRow({{"OD", row.tubeOd}, {"ID", row.tubeId}});
  layout->addWidget(row.tubeDetails);
  row.rodOd = makeLength("RodOd", 6.0);
  row.rodDetails = detailRow({{"OD", row.rodOd}});
  layout->addWidget(row.rodDetails);
  row.stripWidth = makeLength("StripWidth", 6.0);
  row.stripThickness = makeLength("StripThickness", 1.0);
  row.stripDetails = detailRow({{"Width", row.stripWidth}, {"Thickness", row.stripThickness}});
  layout->addWidget(row.stripDetails);

  applySparDefaults(row);

  for (auto* button : {row.top, row.bottom, row.mid, row.wood, row.carbonFiber,
                       row.tube, row.rod, row.strip})
    connect(button, &QRadioButton::toggled, this, [this] {
      updateSparEditorControls();
      emitChanged();
    });
  connect(row.chordLocation, &QSpinBox::valueChanged, this, [this] {
    updateSparEditorControls();
    emitChanged();
  });

  sparEditorsLayout_->addWidget(group);
  sparEditors_.push_back(row);
  renumberSparEditors();
  updateSparEditorControls();
}

void WingPanelEditor::applySparDefaults(SparEditorWidgets& row) {
  applySparData(row, airfoilData_.sparDefaults);
  row.woodHeight->setUnitOverride(
      airfoilData_.unitOverrides.value("sparWoodHeight", UnitOverride::Global));
  row.woodWidth->setUnitOverride(
      airfoilData_.unitOverrides.value("sparWoodWidth", UnitOverride::Global));
  row.tubeOd->setUnitOverride(
      airfoilData_.unitOverrides.value("sparTubeOd", UnitOverride::Millimeters));
  row.tubeId->setUnitOverride(
      airfoilData_.unitOverrides.value("sparTubeId", UnitOverride::Millimeters));
  row.rodOd->setUnitOverride(
      airfoilData_.unitOverrides.value("sparRodOd", UnitOverride::Millimeters));
  row.stripWidth->setUnitOverride(
      airfoilData_.unitOverrides.value("sparStripWidth", UnitOverride::Millimeters));
  row.stripThickness->setUnitOverride(
      airfoilData_.unitOverrides.value("sparStripThickness", UnitOverride::Millimeters));
}

void WingPanelEditor::applySparData(SparEditorWidgets& row, const SparDefaults& spar) {
  row.chordLocation->setValue(spar.chordLocationPercent);
  row.top->setChecked(spar.verticalLocation == 0);
  row.bottom->setChecked(spar.verticalLocation == 1);
  row.mid->setChecked(spar.verticalLocation < 0 || spar.verticalLocation > 1);
  row.wood->setChecked(spar.material == 0);
  row.carbonFiber->setChecked(spar.material != 0);
  row.tube->setChecked(spar.type == 0);
  row.rod->setChecked(spar.type == 1);
  row.strip->setChecked(spar.type < 0 || spar.type > 1);
  row.woodHeight->setValueMm(spar.woodHeight);
  row.woodWidth->setValueMm(spar.woodWidth);
  row.tubeOd->setValueMm(spar.tubeOd);
  row.tubeId->setValueMm(spar.tubeId);
  row.rodOd->setValueMm(spar.rodOd);
  row.stripWidth->setValueMm(spar.stripWidth);
  row.stripThickness->setValueMm(spar.stripThickness);
}

void WingPanelEditor::deleteSelectedSparEditors() {
  for (auto it = sparEditors_.begin(); it != sparEditors_.end();) {
    if (!it->deleteSelection->isChecked()) {
      ++it;
      continue;
    }
    delete it->container;
    it = sparEditors_.erase(it);
  }
  renumberSparEditors();
  updateSparEditorControls();
}

void WingPanelEditor::renumberSparEditors() {
  for (std::size_t i = 0; i < sparEditors_.size(); ++i)
    sparEditors_[i].deleteSelection->setText(QString{"Spar %1"}.arg(i + 1));
}

void WingPanelEditor::updateSparEditorControls() {
  QHash<int, int> woodLocations;
  for (auto& row : sparEditors_) {
    const bool wood = row.wood->isChecked();
    const bool shapedMaterial = row.carbonFiber->isChecked();
    row.woodDetails->setVisible(wood);
    row.typeDetails->setVisible(shapedMaterial);
    row.tubeDetails->setVisible(shapedMaterial && row.tube->isChecked());
    row.rodDetails->setVisible(shapedMaterial && row.rod->isChecked());
    row.stripDetails->setVisible(shapedMaterial && row.strip->isChecked());
    if (wood && row.top->isChecked())
      woodLocations[row.chordLocation->value()] |= 1;
    if (wood && row.bottom->isChecked())
      woodLocations[row.chordLocation->value()] |= 2;
  }
  bool shearWebsAvailable = showUnitOverrides_;
  for (auto it = woodLocations.constBegin(); it != woodLocations.constEnd(); ++it)
    shearWebsAvailable = shearWebsAvailable || it.value() == 3;
  sparShearWebs_->setVisible(shearWebsAvailable);
  if (!shearWebsAvailable) sparShearWebs_->setChecked(false);
  sparShearWebDetails_->setVisible(shearWebsAvailable && sparShearWebs_->isChecked());
  if (joinerEditorsLayout_) updateJoinerEditorControls();
  if (riblets_) {
    const bool available = ribletsAvailable();
    riblets_->setEnabled(available);
    if (!available) riblets_->setChecked(false);
    if (ribletDetails_)
      ribletDetails_->setVisible(available && riblets_->isChecked());
  }
}

bool WingPanelEditor::ribletsAvailable() const {
  if (!tubeLe_ || !rodLe_ || (!tubeLe_->isChecked() && !rodLe_->isChecked()))
    return false;
  if (sparEditors_.empty())
    return cfTube_ && cfRod_ && (cfTube_->isChecked() || cfRod_->isChecked());
  return std::any_of(
      sparEditors_.begin(), sparEditors_.end(),
      [](const SparEditorWidgets& row) {
        const int chord = row.chordLocation->value();
        return row.carbonFiber->isChecked() && row.mid->isChecked() &&
            chord >= 20 && chord <= 40;
      });
}

QWidget* WingPanelEditor::makeLeadingTrailingPage() {
  auto* content = new QWidget; auto* layout = new QVBoxLayout{content};
  auto makeLength = [this](const QString& key, double value) {
    auto* input = new LengthInput{key, value}; input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_); lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged); return input;
  };
  auto* leGroup = new QButtonGroup{this}; leGroup->setExclusive(true);
  blockLe_ = new QRadioButton{"Block LE Stock"};
  tubeLe_ = new QRadioButton{"CF Tube LE"}; rodLe_ = new QRadioButton{"CF Rod LE"};
  leGroup->addButton(blockLe_); leGroup->addButton(tubeLe_); leGroup->addButton(rodLe_);
  layout->addWidget(blockLe_);
  leWidth_ = makeLength("leadingEdgeWidth", 5); leHeight_ = makeLength("leadingEdgeHeight", 7);
  stockLeDetails_ = detailRow({{"Width", leWidth_}, {"Height", leHeight_}}); layout->addWidget(stockLeDetails_);
  layout->addWidget(tubeLe_);
  leTubeOd_ = makeLength("leadingEdgeTubeOd", 2); leTubeId_ = makeLength("leadingEdgeTubeId", 1);
  tubeLeDetails_ = detailRow({{"OD", leTubeOd_}, {"ID", leTubeId_}}); layout->addWidget(tubeLeDetails_);
  layout->addWidget(rodLe_);
  leRodOd_ = makeLength("leadingEdgeRodOd", 2); rodLeDetails_ = detailRow({{"OD", leRodOd_}}); layout->addWidget(rodLeDetails_);
  auto* line = new QFrame; line->setFrameShape(QFrame::HLine); layout->addWidget(line);
  auto* teGroup = new QButtonGroup{this}; teGroup->setExclusive(true);
  sheetTe_ = new QRadioButton{"Sheet TE Stock"};
  teGroup->addButton(sheetTe_); layout->addWidget(sheetTe_);
  teWidth_ = makeLength("trailingEdgeWidth", 20); teHeight_ = makeLength("trailingEdgeHeight", 3);
  stockTeDetails_ = detailRow({{"Width", teWidth_}, {"Height", teHeight_}}); layout->addWidget(stockTeDetails_);
  slottedForRibs_ = new QCheckBox{"Slotted for Ribs"}; slottedDetails_ = detailRow({{"", slottedForRibs_}}); layout->addWidget(slottedDetails_);
  for (auto* button : {blockLe_, tubeLe_, rodLe_, sheetTe_})
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
    detailsLayout->addWidget(detailRow({{"Hinge Post Width", hingeWidth}}));
    detailsLayout->addWidget(detailRow({{"Hinge Post Height", hingeHeight}}));
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
  connect(aileronWidth_, &LengthInput::valueChanged, this,
      [this] { lastControlWidthEdited_ = aileronWidth_; });
  connect(flapWidth_, &LengthInput::valueChanged, this,
      [this] { lastControlWidthEdited_ = flapWidth_; });
  connect(ailerons_, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  connect(flaps_, &QCheckBox::toggled, this, [this] { updateConditionalControls(); emitChanged(); });
  for (auto* spinner : {aileronStart_, aileronStop_, flapStart_, flapStop_})
    connect(spinner, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  layout->addWidget(makeWiringHolesSection("Controls"));
  layout->addStretch(); return scrollPage(content);
}

QWidget* WingPanelEditor::makeSpoilersPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  auto makeLength = [this](const QString& key, const double value) {
    auto* input = new LengthInput{key, value};
    input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_);
    lengths_.insert(key, input);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged);
    return input;
  };
  spoilers_ =
      new QCheckBox{"Spoilers (Center spoiler requires 0 dihedral)"};
  spoilers_->setObjectName("spoilers");
  layout->addWidget(spoilers_);
  spoilerStartRib_ = new QSpinBox;
  spoilerStartRib_->setObjectName("spoilerStartRib");
  spoilerEndRib_ = new QSpinBox;
  spoilerEndRib_->setObjectName("spoilerEndRib");
  spoilerChordLocation_ = new QSpinBox;
  spoilerChordLocation_->setObjectName("spoilerChordLocationPercent");
  spoilerChordLocation_->setRange(1, 95);
  spoilerChordLocation_->setSuffix(" %");
  spoilerWidth_ = makeLength("spoilerWidth", 25.4);
  spoilerThickness_ = makeLength("spoilerThickness", 3.0);
  spoilerFrameRailWidth_ = makeLength("spoilerFrameRailWidth", 6.0);
  spoilerMinimumWoodMargin_ = makeLength(
      "spoilerMinimumWoodMargin",
      globalUnit_ == DisplayUnit::Inches ? 7.9375 : 6.0);
  spoilerMinimumCircleDistance_ = makeLength(
      "spoilerMinimumCircleDistance",
      globalUnit_ == DisplayUnit::Inches ? 12.7 : 12.0);
  spoilerSupportRailHeight_ = new QLabel;
  spoilerSupportRailHeight_->setObjectName("spoilerSupportRailHeight");
  auto* detail = new QWidget;
  auto* detailLayout = new QVBoxLayout{detail};
  detailLayout->setContentsMargins(0, 0, 0, 0);
  spoilerLighteningHoles_ = new QCheckBox{"Lightening Holes"};
  spoilerLighteningHoles_->setObjectName("spoilerLighteningHoles");
  detailLayout->addWidget(detailRow({{"Start Rib", spoilerStartRib_}}));
  detailLayout->addWidget(detailRow({{"End Rib", spoilerEndRib_}}));
  detailLayout->addWidget(detailRow({{"Start Chord Location", spoilerChordLocation_}}));
  detailLayout->addWidget(detailRow({{"Spoiler Chordwise Length", spoilerWidth_}}));
  detailLayout->addWidget(detailRow({{"Spoiler Thickness", spoilerThickness_}}));
  detailLayout->addWidget(detailRow({{"Frame Rail Width", spoilerFrameRailWidth_}}));
  detailLayout->addWidget(detailRow({{"Support Rail Height", spoilerSupportRailHeight_}}));
  detailLayout->addWidget(spoilerLighteningHoles_);
  spoilerMinimumWoodMarginDetails_ =
      detailRow({{"Min Border Distance", spoilerMinimumWoodMargin_}});
  detailLayout->addWidget(spoilerMinimumWoodMarginDetails_);
  spoilerMinimumCircleDistanceDetails_ =
      detailRow({{"Min Circle Distance", spoilerMinimumCircleDistance_}});
  detailLayout->addWidget(spoilerMinimumCircleDistanceDetails_);
  spoilerDetails_ = detail;
  layout->addWidget(detail);
  layout->addWidget(makeWiringHolesSection("Spoilers"));
  layout->addStretch();
  connect(spoilers_, &QCheckBox::toggled, this, [this] {
    updateConditionalControls(); emitChanged();
  });
  connect(spoilerLighteningHoles_, &QCheckBox::toggled, this, [this] {
    updateConditionalControls(); emitChanged();
  });
  for (auto* input : {spoilerStartRib_, spoilerEndRib_, spoilerChordLocation_})
    connect(input, &QSpinBox::valueChanged, this, [this] {
      updateConditionalControls(); emitChanged();
    });
  connect(spoilerStartRib_, &QSpinBox::editingFinished, this, [this] {
    if (spoilers_->isChecked() && spoilerStartRib_->value() < 2 &&
        std::abs(dihedral_->value()) > 1.0e-8) {
      QMessageBox::warning(
          this, "Invalid spoiler settings",
          "Dihedral must be 0 degrees for a center spoiler.");
    }
  });
  return scrollPage(content);
}

QWidget* WingPanelEditor::makeWiringHolesSection(const QString& suffix) {
  WiringHoleWidgets row;
  auto* section = new QWidget;
  auto* layout = new QVBoxLayout{section};
  layout->setContentsMargins(0, 8, 0, 0);
  auto* separator = new QFrame;
  separator->setFrameShape(QFrame::HLine);
  layout->addWidget(separator);
  row.enabled = new QCheckBox{"Wiring Holes"};
  row.enabled->setObjectName("wiringHoles" + suffix);
  layout->addWidget(row.enabled);

  row.startRib = new RibStationSpinBox;
  row.endRib = new RibStationSpinBox;
  row.startRib->setObjectName("wiringHoleStartRib" + suffix);
  row.endRib->setObjectName("wiringHoleEndRib" + suffix);
  row.chordLocation = new QSpinBox;
  row.chordLocation->setObjectName("wiringHoleChordLocationPercent" + suffix);
  row.chordLocation->setRange(1, 95);
  row.chordLocation->setSuffix(" %");
  row.width = new LengthInput{"wiringHoleWidth" + suffix, 9.525};
  row.height = new LengthInput{"wiringHoleHeight" + suffix, 6.35};
  for (auto* input : {row.width, row.height}) {
    input->setGlobalUnit(globalUnit_);
    input->setOverrideSelectorVisible(showUnitOverrides_);
  }
  row.details = new QWidget;
  auto* details = new QVBoxLayout{row.details};
  details->setContentsMargins(0, 0, 0, 0);
  if (suffix == "Spoilers") {
    details->addWidget(detailRow({{"Start Rib", row.startRib}}));
    details->addWidget(detailRow({{"End Rib", row.endRib}}));
    details->addWidget(detailRow({{"Start Chord Location", row.chordLocation}}));
    details->addWidget(detailRow({{"Width", row.width}}));
    details->addWidget(detailRow({{"Height", row.height}}));
  } else {
    details->addWidget(detailRow({{"Start Rib", row.startRib},
                                  {"End Rib", row.endRib}}));
    details->addWidget(detailRow({{"Start Chord Location", row.chordLocation}}));
    details->addWidget(detailRow({{"Width", row.width}, {"Height", row.height}}));
  }
  layout->addWidget(row.details);

  const std::size_t index = wiringHoleWidgets_.size();
  wiringHoleWidgets_.push_back(row);
  connect(row.enabled, &QCheckBox::toggled, this, [this, index] {
    synchronizeWiringHoleControls(index);
  });
  for (auto* input : {row.startRib, row.endRib, row.chordLocation})
    connect(input, &QSpinBox::valueChanged, this, [this, index] {
      synchronizeWiringHoleControls(index);
    });
  for (auto* input : {row.width, row.height})
    connect(input, &LengthInput::valueChanged, this, [this, index] {
      synchronizeWiringHoleControls(index);
    });
  return section;
}

void WingPanelEditor::synchronizeWiringHoleControls(const std::size_t sourceIndex) {
  if (sourceIndex >= wiringHoleWidgets_.size()) return;
  const auto& source = wiringHoleWidgets_[sourceIndex];
  for (std::size_t index = 0; index < wiringHoleWidgets_.size(); ++index) {
    if (index == sourceIndex) continue;
    auto& target = wiringHoleWidgets_[index];
    const QSignalBlocker blockEnabled{target.enabled};
    const QSignalBlocker blockStart{target.startRib};
    const QSignalBlocker blockEnd{target.endRib};
    const QSignalBlocker blockChord{target.chordLocation};
    const QSignalBlocker blockWidth{target.width};
    const QSignalBlocker blockHeight{target.height};
    target.enabled->setChecked(source.enabled->isChecked());
    target.startRib->setValue(source.startRib->value());
    target.endRib->setValue(source.endRib->value());
    target.chordLocation->setValue(source.chordLocation->value());
    target.width->setValueMm(source.width->valueMm());
    target.height->setValueMm(source.height->valueMm());
    target.width->setUnitOverride(source.width->unitOverride());
    target.height->setUnitOverride(source.height->unitOverride());
  }
  updateConditionalControls();
  emitChanged();
}

void WingPanelEditor::updateWiringHoleRibRanges(const bool preserveLastRib) {
  const bool hasRib1a = showRootChord_ && addRib1a_ && addRib1a_->isChecked();
  const int maximum = std::max(2, ribCount_->value() + (hasRib1a ? 1 : 0));
  for (auto& row : wiringHoleWidgets_) {
    const int oldMaximum = row.endRib->maximum();
    const bool wasLast = preserveLastRib && row.endRib->value() == oldMaximum;
    static_cast<RibStationSpinBox*>(row.startRib)->setRib1aPresent(hasRib1a);
    static_cast<RibStationSpinBox*>(row.endRib)->setRib1aPresent(hasRib1a);
    row.startRib->setRange(1, maximum);
    row.endRib->setRange(1, maximum);
    if (wasLast) row.endRib->setValue(maximum);
  }
}

QWidget* WingPanelEditor::makeJoinerPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout{content};
  auto* modes = new QHBoxLayout;
  removablePanel_ = new QPushButton{"Removable Panel"};
  fixedPanel_ = new QPushButton{"Fixed Panel"};
  removablePanel_->setObjectName("removablePanelButton");
  fixedPanel_->setObjectName("fixedPanelButton");
  removablePanel_->setCheckable(true); fixedPanel_->setCheckable(true);
  auto* modeGroup = new QButtonGroup{content};
  modeGroup->setExclusive(true);
  modeGroup->addButton(removablePanel_); modeGroup->addButton(fixedPanel_);
  modes->addWidget(removablePanel_); modes->addWidget(fixedPanel_); modes->addStretch();
  layout->addLayout(modes);

  fixedJoinerButtons_ = new QWidget;
  fixedJoinerButtons_->setObjectName("fixedJoinerButtonRow");
  auto* fixedButtons = new QHBoxLayout{fixedJoinerButtons_};
  fixedButtons->setContentsMargins(0, 4, 0, 4);
  addFixedJoiner_ = new QPushButton{"Add Fixed Joiner"};
  auto* deleteFixed = new QPushButton{"Delete Checked"};
  addFixedJoiner_->setObjectName("addFixedJoinerButton");
  deleteFixed->setObjectName("deleteFixedJoinersButton");
  fixedButtons->addWidget(addFixedJoiner_); fixedButtons->addWidget(deleteFixed); fixedButtons->addStretch();
  layout->addWidget(fixedJoinerButtons_);

  removableJoinerButtons_ = new QWidget;
  removableJoinerButtons_->setObjectName("removableJoinerButtonRow");
  auto* removableButtons = new QHBoxLayout{removableJoinerButtons_};
  removableButtons->setContentsMargins(0, 4, 0, 4);
  addSleeveRodJoiner_ = new QPushButton{"Add Sleeve/Rod Joiner"};
  addAlignmentPin_ = new QPushButton{"Add Alignment Pin"};
  deleteJoiners_ = new QPushButton{"Delete Checked"};
  addSleeveRodJoiner_->setObjectName("addSleeveRodJoinerButton");
  addAlignmentPin_->setObjectName("addAlignmentPinButton");
  deleteJoiners_->setObjectName("deleteRemovableJoinersButton");
  removableButtons->addWidget(addSleeveRodJoiner_); removableButtons->addWidget(addAlignmentPin_);
  removableButtons->addWidget(deleteJoiners_); removableButtons->addStretch();
  layout->addWidget(removableJoinerButtons_);

  joinerEditorsLayout_ = new QVBoxLayout;
  layout->addLayout(joinerEditorsLayout_);
  layout->addStretch();
  connect(removablePanel_, &QPushButton::clicked, this,
      [this] { selectJoinerMode(0, true); });
  connect(fixedPanel_, &QPushButton::clicked, this,
      [this] { selectJoinerMode(1, true); });
  connect(addFixedJoiner_, &QPushButton::clicked, this,
      [this] {
        FixedJoinerData seed;
        if (!showUnitOverrides_ && hasFixedJoinerAddDefault_) {
          seed = fixedJoinerAddDefault_;
        } else if (!fixedJoinerEditors_.empty()) {
          const auto current = data();
          if (!current.fixedJoiners.empty())
            seed = current.fixedJoiners.back();
        }
        addFixedJoinerEditor(seed);
        emitChanged();
      });
  connect(addSleeveRodJoiner_, &QPushButton::clicked, this,
      [this] {
        RemovableJoinerData seed;
        if (!showUnitOverrides_ && hasSleeveRodAddDefault_) {
          seed = sleeveRodAddDefault_;
        } else {
          const auto current = data();
          const auto matching = std::find_if(
              current.removableJoiners.rbegin(),
              current.removableJoiners.rend(),
              [](const RemovableJoinerData& joiner) {
                return joiner.kind == 0;
              });
          if (matching != current.removableJoiners.rend())
            seed = *matching;
        }
        seed.kind = 0;
        addRemovableJoinerEditor(0, seed);
        emitChanged();
      });
  connect(addAlignmentPin_, &QPushButton::clicked, this,
      [this] {
        RemovableJoinerData seed;
        if (!showUnitOverrides_ && hasAlignmentPinAddDefault_) {
          seed = alignmentPinAddDefault_;
        } else {
          const auto current = data();
          const auto matching = std::find_if(
              current.removableJoiners.rbegin(),
              current.removableJoiners.rend(),
              [](const RemovableJoinerData& joiner) {
                return joiner.kind == 1;
              });
          if (matching != current.removableJoiners.rend())
            seed = *matching;
        }
        addRemovableJoinerEditor(1, seed);
        emitChanged();
      });
  connect(deleteFixed, &QPushButton::clicked, this,
      &WingPanelEditor::deleteSelectedJoinerEditors);
  connect(deleteJoiners_, &QPushButton::clicked, this,
      &WingPanelEditor::deleteSelectedJoinerEditors);
  selectJoinerMode(-1, false);
  return scrollPage(content);
}

void WingPanelEditor::setJoinerAddDefaults(
    const WingPanelData& defaults) {
  hasFixedJoinerAddDefault_ = !defaults.fixedJoiners.empty();
  if (hasFixedJoinerAddDefault_)
    fixedJoinerAddDefault_ = defaults.fixedJoiners.back();
  hasSleeveRodAddDefault_ = false;
  hasAlignmentPinAddDefault_ = false;
  for (const auto& joiner : defaults.removableJoiners) {
    if (joiner.kind == 0) {
      sleeveRodAddDefault_ = joiner;
      hasSleeveRodAddDefault_ = true;
    } else if (joiner.kind == 1) {
      alignmentPinAddDefault_ = joiner;
      hasAlignmentPinAddDefault_ = true;
    }
  }
}

void WingPanelEditor::selectJoinerMode(const int mode, const bool confirmDeletion) {
  if (updatingJoinerMode_ || mode == joinerMode_) return;
  const bool hasEntries = !fixedJoinerEditors_.empty() || !removableJoinerEditors_.empty();
  if (!showUnitOverrides_ && confirmDeletion && hasEntries && QMessageBox::warning(this, "Delete all joiners",
          "Changing panel type will delete all joiners. Continue?",
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    updatingJoinerMode_ = true;
    removablePanel_->setChecked(joinerMode_ == 0);
    fixedPanel_->setChecked(joinerMode_ == 1);
    updatingJoinerMode_ = false;
    return;
  }
  if (!showUnitOverrides_ && hasEntries) clearJoinerEditors();
  joinerMode_ = mode;
  updatingJoinerMode_ = true;
  auto* group = removablePanel_->group();
  group->setExclusive(false);
  removablePanel_->setChecked(mode == 0);
  fixedPanel_->setChecked(mode == 1);
  group->setExclusive(true);
  updatingJoinerMode_ = false;
  if (showUnitOverrides_) {
    if (mode == 1 && fixedJoinerEditors_.empty()) addFixedJoinerEditor();
    if (mode == 0) {
      const bool hasJoiner = std::any_of(removableJoinerEditors_.begin(), removableJoinerEditors_.end(),
          [](const auto& row) { return row.kind == 0; });
      const bool hasAlignmentPin = std::any_of(removableJoinerEditors_.begin(), removableJoinerEditors_.end(),
          [](const auto& row) { return row.kind == 1; });
      if (!hasJoiner) addRemovableJoinerEditor(0);
      if (!hasAlignmentPin) addRemovableJoinerEditor(1);
    }
  }
  fixedJoinerButtons_->setVisible(mode == 1);
  removableJoinerButtons_->setVisible(mode == 0);
  updateJoinerEditorControls();
  emitChanged();
}

void WingPanelEditor::addFixedJoinerEditor(const FixedJoinerData& data) {
  FixedJoinerWidgets row;
  auto* group = new QGroupBox;
  row.container = group;
  auto* layout = new QVBoxLayout{group};
  row.deleteSelection = new QCheckBox;
  layout->addWidget(row.deleteSelection);
  row.chordLocation = new QSpinBox;
  row.chordLocation->setRange(0, 90); row.chordLocation->setSingleStep(1);
  row.chordLocation->setSuffix("%"); row.chordLocation->setValue(data.chordLocationPercent);
  layout->addWidget(detailRow({{"Chord Location", row.chordLocation}}));
  row.wood = new QRadioButton{"Wood"}; row.carbonFiber = new QRadioButton{"CF"};
  row.steelRod = new QRadioButton{"Steel Rod"};
  row.wood->setObjectName("fixedJoinerWoodOption");
  auto* materials = new QButtonGroup{group};
  materials->addButton(row.wood, 0); materials->addButton(row.carbonFiber, 1);
  materials->addButton(row.steelRod, 2);
  layout->addWidget(detailRow({{"", row.wood}, {"", row.carbonFiber}, {"", row.steelRod}}));
  row.woodThickness = new LengthInput{"fixedJoinerWoodThickness", data.woodThickness};
  row.woodDetails = detailRow({{"Thickness", row.woodThickness}}); layout->addWidget(row.woodDetails);
  row.tube = new QRadioButton{"Tube"}; row.rod = new QRadioButton{"Rod"};
  auto* types = new QButtonGroup{group}; types->addButton(row.tube, 0); types->addButton(row.rod, 1);
  row.carbonDetails = detailRow({{"", row.tube}, {"", row.rod}}); layout->addWidget(row.carbonDetails);
  row.tubeOd = new LengthInput{"fixedJoinerTubeOd", data.carbonTubeOd};
  row.tubeId = new LengthInput{"fixedJoinerTubeId", data.carbonTubeId};
  row.tubeDetails = detailRow({{"OD", row.tubeOd}, {"ID", row.tubeId}}); layout->addWidget(row.tubeDetails);
  row.rodOd = new LengthInput{"fixedJoinerRodOd", data.carbonRodOd};
  row.rodDetails = detailRow({{"OD", row.rodOd}}); layout->addWidget(row.rodDetails);
  row.steelOd = new LengthInput{"fixedJoinerSteelOd", data.steelRodOd};
  row.steelDetails = detailRow({{"OD", row.steelOd}}); layout->addWidget(row.steelDetails);
  for (auto* input : {row.woodThickness, row.tubeOd, row.tubeId, row.rodOd, row.steelOd}) {
    input->setGlobalUnit(globalUnit_); input->setOverrideSelectorVisible(showUnitOverrides_);
    connect(input, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged);
  }
  row.woodThickness->setUnitOverride(data.woodThicknessUnit);
  row.tubeOd->setUnitOverride(data.carbonTubeOdUnit);
  row.tubeId->setUnitOverride(data.carbonTubeIdUnit);
  row.rodOd->setUnitOverride(data.carbonRodOdUnit);
  row.steelOd->setUnitOverride(data.steelRodOdUnit);
  materials->button(std::clamp(data.material, 0, 2))->setChecked(true);
  types->button(std::clamp(data.carbonType, 0, 1))->setChecked(true);
  connect(row.wood, &QRadioButton::toggled, this,
      [this, chordLocation = row.chordLocation](const bool checked) {
        if (checked && chordLocation->value() == 35) chordLocation->setValue(25);
        updateJoinerEditorControls(); emitChanged();
      });
  for (auto* button : {row.carbonFiber, row.steelRod, row.tube, row.rod})
    connect(button, &QRadioButton::toggled, this, [this] {
      updateJoinerEditorControls(); emitChanged();
    });
  connect(row.chordLocation, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  joinerEditorsLayout_->addWidget(group);
  fixedJoinerEditors_.push_back(row);
  renumberJoinerEditors(); updateJoinerEditorControls();
}

void WingPanelEditor::addRemovableJoinerEditor(const int kind,
                                                const RemovableJoinerData& data) {
  RemovableJoinerWidgets row;
  row.kind = kind;
  const bool newAlignmentPin = kind == 1 && data.kind != 1;
  auto* group = new QGroupBox;
  row.container = group;
  auto* layout = new QVBoxLayout{group};
  row.deleteSelection = new QCheckBox;
  layout->addWidget(row.deleteSelection);
  row.chordLocation = new QSpinBox;
  row.chordLocation->setRange(0, 90); row.chordLocation->setSingleStep(1);
  row.chordLocation->setSuffix("%");
  row.chordLocation->setValue(newAlignmentPin ? 70 : data.chordLocationPercent);
  layout->addWidget(detailRow({{"Chord Location", row.chordLocation}}));
  connect(row.chordLocation, &QSpinBox::valueChanged, this, &WingPanelEditor::emitChanged);
  const auto addMaterial = [&](QWidget* parent, const QString& label,
                               const QStringList& choices, const int selected,
                               const QString& key, const double od,
                               QButtonGroup*& material, LengthInput*& diameter) {
    auto* details = new QWidget;
    auto* detailsLayout = new QVBoxLayout{details};
    detailsLayout->setContentsMargins(20, 2, 0, 4);
    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel{label});
    material = new QButtonGroup{parent};
    for (int i = 0; i < choices.size(); ++i) {
      auto* button = new QRadioButton{choices[i]};
      material->addButton(button, i); options->addWidget(button);
      connect(button, &QRadioButton::toggled, this, &WingPanelEditor::emitChanged);
    }
    material->button(std::clamp(selected, 0, static_cast<int>(choices.size()) - 1))->setChecked(true);
    options->addStretch(); detailsLayout->addLayout(options);
    diameter = new LengthInput{key, od}; diameter->setGlobalUnit(globalUnit_);
    diameter->setOverrideSelectorVisible(showUnitOverrides_);
    connect(diameter, &LengthInput::valueChanged, this, &WingPanelEditor::emitChanged);
    detailsLayout->addWidget(detailRow({{"OD", diameter}}));
    return details;
  };
  if (kind == 0) {
    row.sleeve = new QRadioButton{"Sleeve"}; row.rod = new QRadioButton{"Rod"};
    row.sleeve->setObjectName("removableThisPanelSleeve");
    row.rod->setObjectName("removableThisPanelRod");
    auto* parts = new QButtonGroup{group}; parts->addButton(row.sleeve, 0); parts->addButton(row.rod, 1);
    layout->addWidget(detailRow({{"This Panel:", row.sleeve}, {"", row.rod}}));
    row.thisSleeveDetails = addMaterial(group, "Material", {"CF", "Aluminum", "Steel", "Fiberglass"},
        data.thisSleeveMaterial, "removableThisSleeveOd", data.thisSleeveOd,
        row.thisSleeveMaterial, row.thisSleeveOd);
    row.thisRodDetails = addMaterial(group, "Material", {"CF", "Steel"}, data.thisRodMaterial,
        "removableThisRodOd", data.thisRodOd, row.thisRodMaterial, row.thisRodOd);
    layout->addWidget(row.thisSleeveDetails);
    layout->addWidget(row.thisRodDetails);
    row.adjoiningSleeve = new QRadioButton{"Sleeve"};
    row.adjoiningRod = new QRadioButton{"Rod"};
    row.adjoiningSleeve->setObjectName("removableAdjoiningPanelSleeve");
    row.adjoiningRod->setObjectName("removableAdjoiningPanelRod");
    auto* adjoiningParts = new QButtonGroup{group};
    adjoiningParts->addButton(row.adjoiningSleeve, 0);
    adjoiningParts->addButton(row.adjoiningRod, 1);
    layout->addWidget(detailRow({{"Adjoining Panel:", row.adjoiningSleeve}, {"", row.adjoiningRod}}));
    row.adjoiningSleeveDetails = addMaterial(group, "Material",
        {"CF", "Aluminum", "Steel", "Fiberglass"}, data.adjoiningSleeveMaterial,
        "removableAdjoiningSleeveOd", data.adjoiningSleeveOd,
        row.adjoiningSleeveMaterial, row.adjoiningSleeveOd);
    row.adjoiningRodDetails = addMaterial(group, "Material", {"CF", "Steel"},
        data.adjoiningRodMaterial, "removableAdjoiningRodOd", data.adjoiningRodOd,
        row.adjoiningRodMaterial, row.adjoiningRodOd);
    layout->addWidget(row.adjoiningSleeveDetails);
    layout->addWidget(row.adjoiningRodDetails);
    parts->button(std::clamp(data.thisPanelPart, 0, 1))->setChecked(true);
    adjoiningParts->button(std::clamp(data.adjoiningPanelPart, 0, 1))->setChecked(true);
    for (auto* button : {row.sleeve, row.rod, row.adjoiningSleeve, row.adjoiningRod})
      connect(button, &QRadioButton::toggled, this, [this] {
        updateJoinerEditorControls(); emitChanged();
      });
  } else {
    row.alignmentSleevePin = new QRadioButton{"Sleeve/Pin"};
    row.alignmentPinHole = new QRadioButton{"Pin/Hole"};
    auto* modes = new QButtonGroup{group};
    modes->addButton(row.alignmentSleevePin, 0); modes->addButton(row.alignmentPinHole, 1);
    layout->addWidget(detailRow({{"", row.alignmentSleevePin}, {"", row.alignmentPinHole}}));
    row.alignmentSleevePinDetails = new QWidget;
    auto* sleevePinLayout = new QVBoxLayout{row.alignmentSleevePinDetails};
    sleevePinLayout->setContentsMargins(0, 0, 0, 0);
    row.sleeve = new QRadioButton{"Sleeve"}; row.rod = new QRadioButton{"Rod"};
    auto* parts = new QButtonGroup{group}; parts->addButton(row.sleeve, 0); parts->addButton(row.rod, 1);
    sleevePinLayout->addWidget(detailRow({{"This Panel:", row.sleeve}, {"", row.rod}}));
    row.thisSleeveDetails = addMaterial(group, "Material", {"CF", "Aluminum", "Steel", "Fiberglass"},
        data.thisSleeveMaterial, "alignmentThisSleeveOd",
        newAlignmentPin ? 3.0 : data.thisSleeveOd,
        row.thisSleeveMaterial, row.thisSleeveOd);
    row.thisRodDetails = addMaterial(group, "Material", {"CF", "Steel"}, data.thisRodMaterial,
        "alignmentThisRodOd", newAlignmentPin ? 2.0 : data.thisRodOd,
        row.thisRodMaterial, row.thisRodOd);
    row.adjoiningSleeveDetails = addMaterial(group, "Adjoining Panel: Sleeve Material",
        {"CF", "Aluminum", "Steel", "Fiberglass"}, data.adjoiningSleeveMaterial,
        "alignmentAdjoiningSleeveOd",
        newAlignmentPin ? 3.0 : data.adjoiningSleeveOd,
        row.adjoiningSleeveMaterial, row.adjoiningSleeveOd);
    row.adjoiningRodDetails = addMaterial(group, "Adjoining Panel: Rod Material", {"CF", "Steel"},
        data.adjoiningRodMaterial, "alignmentAdjoiningRodOd",
        newAlignmentPin ? 2.0 : data.adjoiningRodOd,
        row.adjoiningRodMaterial, row.adjoiningRodOd);
    for (auto* details : {row.thisSleeveDetails, row.thisRodDetails,
                          row.adjoiningSleeveDetails, row.adjoiningRodDetails})
      sleevePinLayout->addWidget(details);
    layout->addWidget(row.alignmentSleevePinDetails);
    row.alignmentPinHoleDetails = new QWidget;
    auto* pinHoleLayout = new QVBoxLayout{row.alignmentPinHoleDetails};
    pinHoleLayout->setContentsMargins(0, 0, 0, 0);
    row.pinHolePin = new QRadioButton{"Pin"}; row.pinHoleHole = new QRadioButton{"Hole"};
    auto* pinHoleParts = new QButtonGroup{group};
    pinHoleParts->addButton(row.pinHolePin, 0); pinHoleParts->addButton(row.pinHoleHole, 1);
    pinHoleLayout->addWidget(detailRow({{"This Panel:", row.pinHolePin}, {"", row.pinHoleHole}}));
    row.pinDetails = addMaterial(group, "Material", {"CF", "Steel"}, data.pinMaterial,
        "alignmentPinOd", data.pinOd, row.pinMaterial, row.pinOd);
    pinHoleLayout->addWidget(row.pinDetails); layout->addWidget(row.alignmentPinHoleDetails);
    modes->button(std::clamp(data.alignmentMode, 0, 1))->setChecked(true);
    parts->button(newAlignmentPin ? 0 : std::clamp(data.thisPanelPart, 0, 1))->setChecked(true);
    pinHoleParts->button(std::clamp(data.pinHoleThisPart, 0, 1))->setChecked(true);
    for (auto* button : {row.alignmentSleevePin, row.alignmentPinHole,
                         row.sleeve, row.rod, row.pinHolePin, row.pinHoleHole})
      connect(button, &QRadioButton::toggled, this, [this] {
        updateJoinerEditorControls(); emitChanged();
      });
  }
  row.thisRodOd->setUnitOverride(data.thisRodOdUnit);
  row.thisSleeveOd->setUnitOverride(data.thisSleeveOdUnit);
  row.adjoiningRodOd->setUnitOverride(data.adjoiningRodOdUnit);
  row.adjoiningSleeveOd->setUnitOverride(data.adjoiningSleeveOdUnit);
  if (row.pinOd) row.pinOd->setUnitOverride(data.pinOdUnit);
  joinerEditorsLayout_->addWidget(group);
  removableJoinerEditors_.push_back(row);
  renumberJoinerEditors(); updateJoinerEditorControls();
}

void WingPanelEditor::deleteSelectedJoinerEditors() {
  for (auto it = fixedJoinerEditors_.begin(); it != fixedJoinerEditors_.end();) {
    if (!it->deleteSelection->isChecked()) { ++it; continue; }
    delete it->container; it = fixedJoinerEditors_.erase(it);
  }
  for (auto it = removableJoinerEditors_.begin(); it != removableJoinerEditors_.end();) {
    if (!it->deleteSelection->isChecked()) { ++it; continue; }
    delete it->container; it = removableJoinerEditors_.erase(it);
  }
  renumberJoinerEditors(); updateJoinerEditorControls(); emitChanged();
}

void WingPanelEditor::clearJoinerEditors() {
  for (auto& row : fixedJoinerEditors_) delete row.container;
  for (auto& row : removableJoinerEditors_) delete row.container;
  fixedJoinerEditors_.clear(); removableJoinerEditors_.clear();
}

void WingPanelEditor::renumberJoinerEditors() {
  for (std::size_t i = 0; i < fixedJoinerEditors_.size(); ++i)
    fixedJoinerEditors_[i].deleteSelection->setText(QString{"Joiner %1"}.arg(i + 1));
  int joiner = 0;
  int alignmentPin = 0;
  for (auto& row : removableJoinerEditors_) {
    const int number = row.kind == 0 ? ++joiner : ++alignmentPin;
    row.deleteSelection->setText(row.kind == 0
        ? QString{"Joiner %1"}.arg(number)
        : QString{"Alignment Pin %1"}.arg(number));
  }
}

void WingPanelEditor::updateJoinerEditorControls() {
  QHash<int, int> matchingWoodSpars;
  for (const auto& spar : sparEditors_) {
    if (!spar.wood->isChecked()) continue;
    if (spar.top->isChecked()) matchingWoodSpars[spar.chordLocation->value()] |= 1;
    if (spar.bottom->isChecked()) matchingWoodSpars[spar.chordLocation->value()] |= 2;
  }
  bool fixedWoodAvailable = false;
  for (auto it = matchingWoodSpars.constBegin(); it != matchingWoodSpars.constEnd(); ++it)
    fixedWoodAvailable = fixedWoodAvailable || it.value() == 3;
  for (auto& row : fixedJoinerEditors_) {
    row.container->setVisible(joinerMode_ == 1);
    row.wood->setVisible(fixedWoodAvailable);
    if (!fixedWoodAvailable && row.wood->isChecked()) row.carbonFiber->setChecked(true);
    row.woodDetails->setVisible(row.wood->isChecked());
    row.carbonDetails->setVisible(row.carbonFiber->isChecked());
    row.tubeDetails->setVisible(row.carbonFiber->isChecked() && row.tube->isChecked());
    row.rodDetails->setVisible(row.carbonFiber->isChecked() && row.rod->isChecked());
    row.steelDetails->setVisible(row.steelRod->isChecked());
  }
  for (auto& row : removableJoinerEditors_) {
    row.container->setVisible(joinerMode_ == 0);
    if (row.kind == 0) {
      const bool thisSleeve = row.sleeve->isChecked();
      const bool adjoiningSleeve = row.adjoiningSleeve->isChecked();
      row.thisSleeveDetails->setVisible(thisSleeve);
      row.thisRodDetails->setVisible(!thisSleeve);
      row.adjoiningSleeveDetails->setVisible(adjoiningSleeve);
      row.adjoiningRodDetails->setVisible(!adjoiningSleeve);
    } else {
      const bool sleevePin = row.alignmentSleevePin->isChecked();
      row.alignmentSleevePinDetails->setVisible(sleevePin);
      row.alignmentPinHoleDetails->setVisible(!sleevePin);
      const bool thisSleeve = row.sleeve->isChecked();
      row.thisSleeveDetails->setVisible(thisSleeve);
      row.thisRodDetails->setVisible(!thisSleeve);
      row.adjoiningSleeveDetails->setVisible(!thisSleeve);
      row.adjoiningRodDetails->setVisible(thisSleeve);
    }
  }
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
  d.ribLighteningHoles = ribLighteningHoles_->isChecked();
  d.ribLighteningStartRib = ribLighteningStartRib_->value();
  d.ribLighteningStopRib = ribLighteningStopRib_->value();
  d.ribLighteningMinimumWoodMargin =
      ribLighteningMinimumWoodMargin_->valueMm();
  d.ribLighteningMinimumHoleDistance =
      ribLighteningMinimumHoleDistance_->valueMm();
  d.riblets = riblets_->isChecked();
  d.ribletStartRib = ribletStartRib_->value();
  d.ribletEndRib = ribletEndRib_->value();
  d.ribletsPerBay = ribletsPerBay_->value();
  const auto sparData = [](const SparEditorWidgets& row) {
    SparDefaults spar;
    spar.chordLocationPercent = row.chordLocation->value();
    spar.verticalLocation = row.top->isChecked() ? 0 : row.bottom->isChecked() ? 1 : 2;
    spar.material = row.wood->isChecked() ? 0 : 1;
    spar.type = row.tube->isChecked() ? 0 : row.rod->isChecked() ? 1 : 2;
    spar.woodHeight = row.woodHeight->valueMm();
    spar.woodWidth = row.woodWidth->valueMm();
    spar.tubeOd = row.tubeOd->valueMm();
    spar.tubeId = row.tubeId->valueMm();
    spar.rodOd = row.rodOd->valueMm();
    spar.stripWidth = row.stripWidth->valueMm();
    spar.stripThickness = row.stripThickness->valueMm();
    return spar;
  };
  d.spars.clear();
  d.spars.reserve(sparEditors_.size());
  for (const auto& row : sparEditors_) d.spars.push_back(sparData(row));
  if (showUnitOverrides_ && !sparEditors_.empty()) {
    const auto& row = sparEditors_.front();
    d.sparDefaults = d.spars.front();
    d.unitOverrides.insert("sparWoodHeight", row.woodHeight->unitOverride());
    d.unitOverrides.insert("sparWoodWidth", row.woodWidth->unitOverride());
    d.unitOverrides.insert("sparTubeOd", row.tubeOd->unitOverride());
    d.unitOverrides.insert("sparTubeId", row.tubeId->unitOverride());
    d.unitOverrides.insert("sparRodOd", row.rodOd->unitOverride());
    d.unitOverrides.insert("sparStripWidth", row.stripWidth->unitOverride());
    d.unitOverrides.insert("sparStripThickness", row.stripThickness->unitOverride());
    d.unitOverrides.insert("sparShearWebThickness", sparShearWebThickness_->unitOverride());
  }
  d.sparShearWebs = sparShearWebs_->isChecked();
  d.sparDefaults.shearWebThickness = sparShearWebThickness_->valueMm();
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
  d.leadingEdgeType = blockLe_->isChecked() ? 2 : tubeLe_->isChecked() ? 3 : rodLe_->isChecked() ? 4 : 0;
  d.leadingEdgeWidth = leWidth_->valueMm(); d.leadingEdgeHeight = leHeight_->valueMm(); d.leadingEdgeTubeOd = leTubeOd_->valueMm();
  d.leadingEdgeTubeId = leTubeId_->valueMm(); d.leadingEdgeRodOd = leRodOd_->valueMm();
  d.trailingEdgeType = sheetTe_->isChecked() ? 2 : 0;
  d.trailingEdgeWidth = teWidth_->valueMm(); d.trailingEdgeHeight = teHeight_->valueMm(); d.slottedForRibs = slottedForRibs_->isChecked();
  d.ailerons = ailerons_->isChecked(); d.aileronWidth = aileronWidth_->valueMm(); d.aileronHeight = aileronHeight_->valueMm();
  d.aileronStartRib = aileronStart_->value(); d.aileronStopRib = aileronStop_->value();
  d.aileronHingePostWidth = aileronHingePostWidth_->valueMm(); d.aileronHingePostHeight = aileronHingePostHeight_->valueMm();
  d.flaps = flaps_->isChecked(); d.flapWidth = flapWidth_->valueMm(); d.flapHeight = flapHeight_->valueMm();
  d.flapStartRib = flapStart_->value(); d.flapStopRib = flapStop_->value();
  d.flapHingePostWidth = flapHingePostWidth_->valueMm(); d.flapHingePostHeight = flapHingePostHeight_->valueMm();
  if (showRootChord_) {
    d.spoilers = spoilers_->isChecked();
    d.spoilerStartRib = spoilerStartRib_->value();
    d.spoilerEndRib = spoilerEndRib_->value();
    d.spoilerChordLocationPercent = spoilerChordLocation_->value();
    d.spoilerWidth = spoilerWidth_->valueMm();
    d.spoilerThickness = spoilerThickness_->valueMm();
    d.spoilerFrameRailWidth = spoilerFrameRailWidth_->valueMm();
    d.spoilerSupportRailHeight = globalUnit_ == DisplayUnit::Inches ? 3.175 : 3.0;
    d.spoilerLighteningHoles = spoilerLighteningHoles_->isChecked();
    d.spoilerMinimumWoodMargin = spoilerMinimumWoodMargin_->valueMm();
    d.spoilerMinimumCircleDistance =
        spoilerMinimumCircleDistance_->valueMm();
  } else {
    d.spoilers = false;
    d.spoilerLighteningHoles = false;
  }
  if (!wiringHoleWidgets_.empty()) {
    const auto& wiring = wiringHoleWidgets_.front();
    d.wiringHoles = wiring.enabled->isChecked();
    d.wiringHoleStartRib = wiring.startRib->value();
    d.wiringHoleEndRib = wiring.endRib->value();
    d.wiringHoleChordLocationPercent = wiring.chordLocation->value();
    d.wiringHoleWidth = wiring.width->valueMm();
    d.wiringHoleHeight = wiring.height->valueMm();
    d.unitOverrides.insert("wiringHoleWidth", wiring.width->unitOverride());
    d.unitOverrides.insert("wiringHoleHeight", wiring.height->unitOverride());
  }
  d.addRib1a = showRootChord_ && addRib1a_->isChecked();
  if (showJoinerPage_) {
    d.joinerPanelMode = joinerMode_;
    d.fixedJoiners.clear();
    for (const auto& row : fixedJoinerEditors_) {
      FixedJoinerData joiner;
      joiner.chordLocationPercent = row.chordLocation->value();
      joiner.material = row.wood->isChecked() ? 0 : row.carbonFiber->isChecked() ? 1 : 2;
      joiner.woodThickness = row.woodThickness->valueMm();
      joiner.carbonType = row.tube->isChecked() ? 0 : 1;
      joiner.carbonTubeOd = row.tubeOd->valueMm(); joiner.carbonTubeId = row.tubeId->valueMm();
      joiner.carbonRodOd = row.rodOd->valueMm(); joiner.steelRodOd = row.steelOd->valueMm();
      joiner.woodThicknessUnit = row.woodThickness->unitOverride();
      joiner.carbonTubeOdUnit = row.tubeOd->unitOverride();
      joiner.carbonTubeIdUnit = row.tubeId->unitOverride();
      joiner.carbonRodOdUnit = row.rodOd->unitOverride();
      joiner.steelRodOdUnit = row.steelOd->unitOverride();
      d.fixedJoiners.push_back(joiner);
    }
    d.removableJoiners.clear();
    for (const auto& row : removableJoinerEditors_) {
      RemovableJoinerData joiner;
      joiner.kind = row.kind;
      joiner.chordLocationPercent = row.chordLocation->value();
      joiner.thisPanelPart = row.sleeve->isChecked() ? 0 : 1;
      joiner.adjoiningPanelPart = row.kind == 0
          ? (row.adjoiningSleeve->isChecked() ? 0 : 1)
          : 1 - joiner.thisPanelPart;
      joiner.thisRodMaterial = row.thisRodMaterial->checkedId();
      joiner.thisRodOd = row.thisRodOd->valueMm();
      joiner.thisSleeveMaterial = row.thisSleeveMaterial->checkedId();
      joiner.thisSleeveOd = row.thisSleeveOd->valueMm();
      joiner.adjoiningRodMaterial = row.adjoiningRodMaterial->checkedId();
      joiner.adjoiningRodOd = row.adjoiningRodOd->valueMm();
      joiner.adjoiningSleeveMaterial = row.adjoiningSleeveMaterial->checkedId();
      joiner.adjoiningSleeveOd = row.adjoiningSleeveOd->valueMm();
      joiner.thisRodOdUnit = row.thisRodOd->unitOverride();
      joiner.thisSleeveOdUnit = row.thisSleeveOd->unitOverride();
      joiner.adjoiningRodOdUnit = row.adjoiningRodOd->unitOverride();
      joiner.adjoiningSleeveOdUnit = row.adjoiningSleeveOd->unitOverride();
      if (row.kind == 1) {
        joiner.alignmentMode = row.alignmentSleevePin->isChecked() ? 0 : 1;
        joiner.pinHoleThisPart = row.pinHolePin->isChecked() ? 0 : 1;
        joiner.pinMaterial = row.pinMaterial->checkedId();
        joiner.pinOd = row.pinOd->valueMm();
        joiner.pinOdUnit = row.pinOd->unitOverride();
      }
      d.removableJoiners.push_back(joiner);
    }
  }
  for (auto it = lengths_.constBegin(); it != lengths_.constEnd(); ++it) d.unitOverrides.insert(it.key(), it.value()->unitOverride());
  return d;
}

void WingPanelEditor::setData(const WingPanelData& d) {
  airfoilData_ = d;
  while (sparEditors_.size() > d.spars.size()) {
    delete sparEditors_.back().container;
    sparEditors_.pop_back();
  }
  while (sparEditors_.size() < d.spars.size()) addSparEditor();
  for (std::size_t i = 0; i < d.spars.size(); ++i) {
    applySparDefaults(sparEditors_[i]);
    applySparData(sparEditors_[i], d.spars[i]);
  }
  renumberSparEditors();
  if (sparShearWebThickness_) {
    sparShearWebThickness_->setValueMm(d.sparDefaults.shearWebThickness);
    sparShearWebThickness_->setUnitOverride(
        d.unitOverrides.value("sparShearWebThickness", UnitOverride::Global));
  }
  sparShearWebs_->setChecked(d.sparShearWebs);
  rootName_->setText(QString::fromStdString(d.rootAirfoil.name())); tipName_->setText(QString::fromStdString(d.tipAirfoil.name()));
  auto setLength = [&](LengthInput* input, double value, const QString& key) {
    input->setValueMm(value); input->setUnitOverride(d.unitOverrides.value(key, UnitOverride::Global));
  };
  setLength(span_, d.panelSpan, "panelSpan"); setLength(rootChord_, d.rootChord, "rootChord");
  setLength(tipChord_, d.tipChord, "tipChord"); setLength(sweep_, d.sweep, "sweep");
  dihedral_->setValue(d.dihedral); twist_->setValue(d.twist); setLength(ribThickness_, d.ribThickness, "ribThickness"); ribCount_->setValue(d.ribCount);
  ribLighteningHoles_->setChecked(d.ribLighteningHoles);
  ribLighteningStartRib_->setValue(d.ribLighteningStartRib);
  ribLighteningStopRib_->setValue(d.ribLighteningStopRib > 0
      ? d.ribLighteningStopRib : std::max(1, d.ribCount - 2));
  setLength(ribLighteningMinimumWoodMargin_,
            d.ribLighteningMinimumWoodMargin,
            "ribLighteningMinimumWoodMargin");
  setLength(ribLighteningMinimumHoleDistance_,
            d.ribLighteningMinimumHoleDistance,
            "ribLighteningMinimumHoleDistance");
  ribletStartRib_->setValue(d.ribletStartRib);
  ribletEndRib_->setValue(d.ribletEndRib > 0
      ? d.ribletEndRib : d.ribCount);
  ribletsPerBay_->setValue(std::clamp(d.ribletsPerBay, 1, 5));
#define SET_LENGTH(ptr, field) setLength(ptr, d.field, #field)
  topSpar_->setChecked(d.topSpar); SET_LENGTH(topSparHeight_, topSparHeight); SET_LENGTH(topSparWidth_, topSparWidth);
  bottomSpar_->setChecked(d.bottomSpar); SET_LENGTH(bottomSparHeight_, bottomSparHeight); SET_LENGTH(bottomSparWidth_, bottomSparWidth);
  shearWebs_->setChecked(d.shearWebs); SET_LENGTH(shearWebWidth_, shearWebWidth);
  cfTube_->setChecked(d.carbonSpar == 1); cfRod_->setChecked(d.carbonSpar == 2); SET_LENGTH(cfTubeOd_, cfTubeOd); SET_LENGTH(cfTubeId_, cfTubeId); SET_LENGTH(cfRodOd_, cfRodOd);
  leTopSheet_->setChecked(d.leTopSheet); SET_LENGTH(leTopSheetThickness_, leTopSheetThickness); leTopSheetStopRib_->setValue(d.leTopSheetStopRib);
  leBottomSheet_->setChecked(d.leBottomSheet); SET_LENGTH(leBottomSheetThickness_, leBottomSheetThickness); leBottomSheetStopRib_->setValue(d.leBottomSheetStopRib);
  teTopSheet_->setChecked(d.teTopSheet); SET_LENGTH(teTopSheetThickness_, teTopSheetThickness); teTopSheetStopRib_->setValue(d.teTopSheetStopRib);
  teBottomSheet_->setChecked(d.teBottomSheet); SET_LENGTH(teBottomSheetThickness_, teBottomSheetThickness); teBottomSheetStopRib_->setValue(d.teBottomSheetStopRib);
  turbulators_->setChecked(d.turbulators); turbulatorCount_->setValue(d.turbulatorCount); SET_LENGTH(turbulatorHeight_, turbulatorHeight); SET_LENGTH(turbulatorWidth_, turbulatorWidth);
  topRearSpar_->setChecked(d.topRearSpar); SET_LENGTH(topRearHeight_, topRearSparHeight); SET_LENGTH(topRearWidth_, topRearSparWidth);
  bottomRearSpar_->setChecked(d.bottomRearSpar); SET_LENGTH(bottomRearHeight_, bottomRearSparHeight); SET_LENGTH(bottomRearWidth_, bottomRearSparWidth);
  blockLe_->setChecked(d.leadingEdgeType == 1 || d.leadingEdgeType == 2); tubeLe_->setChecked(d.leadingEdgeType == 3); rodLe_->setChecked(d.leadingEdgeType == 4);
  SET_LENGTH(leWidth_, leadingEdgeWidth); SET_LENGTH(leHeight_, leadingEdgeHeight); SET_LENGTH(leTubeOd_, leadingEdgeTubeOd); SET_LENGTH(leTubeId_, leadingEdgeTubeId); SET_LENGTH(leRodOd_, leadingEdgeRodOd);
  sheetTe_->setChecked(d.trailingEdgeType == 1 || d.trailingEdgeType == 2); SET_LENGTH(teWidth_, trailingEdgeWidth); SET_LENGTH(teHeight_, trailingEdgeHeight); slottedForRibs_->setChecked(d.slottedForRibs);
  ailerons_->setChecked(d.ailerons); SET_LENGTH(aileronWidth_, aileronWidth); SET_LENGTH(aileronHeight_, aileronHeight); aileronStart_->setValue(d.aileronStartRib); aileronStop_->setValue(d.aileronStopRib); SET_LENGTH(aileronHingePostWidth_, aileronHingePostWidth); SET_LENGTH(aileronHingePostHeight_, aileronHingePostHeight);
  flaps_->setChecked(d.flaps); SET_LENGTH(flapWidth_, flapWidth); SET_LENGTH(flapHeight_, flapHeight); flapStart_->setValue(d.flapStartRib); flapStop_->setValue(d.flapStopRib); SET_LENGTH(flapHingePostWidth_, flapHingePostWidth); SET_LENGTH(flapHingePostHeight_, flapHingePostHeight);
  if (showRootChord_) {
    spoilers_->setChecked(d.spoilers);
    spoilerStartRib_->setValue(d.spoilerStartRib);
    spoilerEndRib_->setValue(d.spoilerEndRib);
    spoilerChordLocation_->setValue(d.spoilerChordLocationPercent);
    SET_LENGTH(spoilerWidth_, spoilerWidth);
    SET_LENGTH(spoilerThickness_, spoilerThickness);
    SET_LENGTH(spoilerFrameRailWidth_, spoilerFrameRailWidth);
    spoilerLighteningHoles_->setChecked(d.spoilerLighteningHoles);
    SET_LENGTH(spoilerMinimumWoodMargin_, spoilerMinimumWoodMargin);
    SET_LENGTH(
        spoilerMinimumCircleDistance_, spoilerMinimumCircleDistance);
    airfoilData_.spoilerSupportRailHeight = d.spoilerSupportRailHeight;
  }
  addRib1a_->setChecked(showRootChord_ && d.addRib1a);
  if (showJoinerPage_) {
    clearJoinerEditors();
    joinerMode_ = -2;
    if (showUnitOverrides_) {
      for (const auto& joiner : d.fixedJoiners) addFixedJoinerEditor(joiner);
      for (const auto& joiner : d.removableJoiners)
        addRemovableJoinerEditor(joiner.kind, joiner);
      selectJoinerMode(d.joinerPanelMode, false);
    } else {
      selectJoinerMode(d.joinerPanelMode, false);
      if (d.joinerPanelMode == 1)
        for (const auto& joiner : d.fixedJoiners) addFixedJoinerEditor(joiner);
      else if (d.joinerPanelMode == 0)
        for (const auto& joiner : d.removableJoiners)
          addRemovableJoinerEditor(joiner.kind, joiner);
    }
  }
  updateWiringHoleRibRanges();
  const int wiringEnd = d.wiringHoleEndRib > 0
      ? d.wiringHoleEndRib
      : ribCount_->value() + ((showRootChord_ && d.addRib1a) ? 1 : 0);
  for (auto& wiring : wiringHoleWidgets_) {
    const QSignalBlocker blockEnabled{wiring.enabled};
    const QSignalBlocker blockStart{wiring.startRib};
    const QSignalBlocker blockEnd{wiring.endRib};
    const QSignalBlocker blockChord{wiring.chordLocation};
    const QSignalBlocker blockWidth{wiring.width};
    const QSignalBlocker blockHeight{wiring.height};
    wiring.enabled->setChecked(d.wiringHoles);
    wiring.startRib->setValue(d.wiringHoleStartRib);
    wiring.endRib->setValue(wiringEnd);
    wiring.chordLocation->setValue(d.wiringHoleChordLocationPercent);
    wiring.width->setValueMm(d.wiringHoleWidth);
    wiring.height->setValueMm(d.wiringHoleHeight);
    wiring.width->setUnitOverride(
        d.unitOverrides.value("wiringHoleWidth", UnitOverride::Global));
    wiring.height->setUnitOverride(
        d.unitOverrides.value("wiringHoleHeight", UnitOverride::Global));
  }
#undef SET_LENGTH
  updateSparEditorControls();
  riblets_->setChecked(d.riblets && ribletsAvailable());
  updateConditionalControls();
  updateRibSpacing();
}

void WingPanelEditor::setGlobalUnit(const DisplayUnit unit) {
  globalUnit_ = unit;
  for (auto* input : lengths_) input->setGlobalUnit(unit);
  for (auto& row : sparEditors_)
    for (auto* input : {row.woodHeight, row.woodWidth, row.tubeOd, row.tubeId,
                        row.rodOd, row.stripWidth, row.stripThickness})
      input->setGlobalUnit(unit);
  if (sparShearWebThickness_) sparShearWebThickness_->setGlobalUnit(unit);
  for (auto& row : fixedJoinerEditors_)
    for (auto* input : {row.woodThickness, row.tubeOd, row.tubeId, row.rodOd, row.steelOd})
      input->setGlobalUnit(unit);
  for (auto& row : removableJoinerEditors_)
    for (auto* input : {row.thisSleeveOd, row.thisRodOd, row.adjoiningSleeveOd,
                        row.adjoiningRodOd, row.pinOd})
      if (input) input->setGlobalUnit(unit);
  for (auto& row : wiringHoleWidgets_)
    for (auto* input : {row.width, row.height}) input->setGlobalUnit(unit);
  if (spoilerSupportRailHeight_)
    spoilerSupportRailHeight_->setText(unit == DisplayUnit::Inches ? "1/8 in" : "3 mm");
  updateRibSpacing();
}

void WingPanelEditor::updateRibSpacing() {
  if (!ribSpacing_ || !span_ || !ribCount_) return;
  const double spacingMm = span_->valueMm() /
      static_cast<double>(std::max(1, ribCount_->value() - 1));
  const UnitOverride override = span_->unitOverride();
  const bool useInches = override == UnitOverride::Inches ||
      (override == UnitOverride::Global && globalUnit_ == DisplayUnit::Inches);
  if (useInches) {
    const double inches = spacingMm / kMmPerInch;
    ribSpacing_->setText("Rib Spacing: " + (isThirtySecondMultiple(inches)
        ? fractionalInchText(inches) : decimalInchText(inches)) + " in");
  } else {
    ribSpacing_->setText(
        QString{"Rib Spacing: %1 mm"}.arg(spacingMm, 0, 'f', 2));
  }
}

bool WingPanelEditor::validate(QString& error) {
  if (ribLighteningHoles_->isChecked() &&
      ribLighteningStartRib_->value() > ribLighteningStopRib_->value()) {
    error = "Rib Lightening Hole Start Rib must not be after Stop Rib.";
    return false;
  }
  if (ribLighteningHoles_->isChecked() &&
      (ribLighteningMinimumWoodMargin_->valueMm() <= 0.0 ||
       ribLighteningMinimumHoleDistance_->valueMm() < 0.0)) {
    error =
        "Rib lightening-hole distances must be valid positive dimensions.";
    return false;
  }
  if (riblets_->isChecked() &&
      ribletStartRib_->value() >= ribletEndRib_->value()) {
    error = "Riblet Start Rib must be less than End Rib.";
    return false;
  }
  if (!wiringHoleWidgets_.empty() && wiringHoleWidgets_.front().enabled->isChecked() &&
      wiringHoleWidgets_.front().startRib->value() >
          wiringHoleWidgets_.front().endRib->value()) {
    error = "Wiring Hole Start Rib must not be after End Rib.";
    return false;
  }
  if (showRootChord_ && spoilers_->isChecked()) {
    if (spoilerLighteningHoles_->isChecked() &&
        spoilerWidth_->valueMm() <=
            2.0 * spoilerMinimumWoodMargin_->valueMm()) {
      error =
          "Spoiler Min Border Distance must leave room for a lightening hole.";
      return false;
    }
    if (spoilerEndRib_->value() < spoilerStartRib_->value() + 3) {
      error = "Spoiler End Rib must be at least Start Rib + 3.";
      return false;
    }
    if (spoilerStartRib_->value() < 2 &&
        std::abs(dihedral_->value()) > 1.0e-8) {
      error = "Dihedral must be 0 degrees for a center spoiler.";
      return false;
    }
  }
  if (flaps_->isChecked() && flapStart_->value() >= flapStop_->value()) {
    error = "Flap Start Rib must be less than Flap Stop Rib.";
    return false;
  }
  if (flaps_->isChecked() && ailerons_->isChecked() &&
      aileronStart_->value() < flapStop_->value()) {
    const int correctedStart = flapStop_->value();
    aileronStart_->setValue(correctedStart);
    const bool correctedStop = aileronStop_->value() <= correctedStart;
    if (correctedStop)
      aileronStop_->setValue(std::min(ribCount_->value(), correctedStart + 1));
    error = QString{
        "Aileron Start Rib cannot be less than Flap Stop Rib. "
        "Aileron Start Rib was corrected to %1%2."}
        .arg(correctedStart)
        .arg(correctedStop
                 ? QString{" and Aileron Stop Rib was corrected to %1"}
                       .arg(aileronStop_->value())
                 : QString{});
    return false;
  }
  if (flaps_->isChecked() && ailerons_->isChecked() &&
      aileronStart_->value() == flapStop_->value() &&
      std::abs(aileronWidth_->valueMm() - flapWidth_->valueMm()) > 1.0e-8) {
    LengthInput* corrected = lastControlWidthEdited_ == aileronWidth_
        ? aileronWidth_ : flapWidth_;
    LengthInput* reference = corrected == aileronWidth_
        ? flapWidth_ : aileronWidth_;
    const QString correctedName = corrected == aileronWidth_
        ? QString{"Aileron Width"} : QString{"Flap Width"};
    corrected->setValueMm(reference->valueMm());
    emitChanged();
    error = QString{
        "When Flap Stop Rib equals Aileron Start Rib, Flap Width and "
        "Aileron Width must match. %1 was corrected to match the other width."}
        .arg(correctedName);
    return false;
  }
  if (ailerons_->isChecked() && aileronStart_->value() >= aileronStop_->value()) {
    error = "Aileron Start Rib must be less than Aileron Stop Rib.";
    return false;
  }
  return true;
}

void WingPanelEditor::updateConditionalControls() {
  ribLighteningHoleDetails_->setVisible(ribLighteningHoles_->isChecked());
  const bool ribletEligible = ribletsAvailable();
  riblets_->setEnabled(ribletEligible);
  if (!ribletEligible) riblets_->setChecked(false);
  ribletDetails_->setVisible(ribletEligible && riblets_->isChecked());
  topSparDetails_->setVisible(topSpar_->isChecked()); bottomSparDetails_->setVisible(bottomSpar_->isChecked());
  const bool bothSpars = topSpar_->isChecked() && bottomSpar_->isChecked();
  shearWebs_->setVisible(bothSpars); shearDetails_->setVisible(bothSpars && shearWebs_->isChecked());
  cfTubeDetails_->setVisible(cfTube_->isChecked()); cfRodDetails_->setVisible(cfRod_->isChecked());
  leTopSheetDetails_->setVisible(leTopSheet_->isChecked()); leBottomSheetDetails_->setVisible(leBottomSheet_->isChecked());
  teTopSheetDetails_->setVisible(teTopSheet_->isChecked()); teBottomSheetDetails_->setVisible(teBottomSheet_->isChecked());
  turbulatorDetails_->setVisible(turbulators_->isChecked()); topRearDetails_->setVisible(topRearSpar_->isChecked()); bottomRearDetails_->setVisible(bottomRearSpar_->isChecked());
  stockLeDetails_->setVisible(blockLe_->isChecked()); tubeLeDetails_->setVisible(tubeLe_->isChecked()); rodLeDetails_->setVisible(rodLe_->isChecked());
  stockTeDetails_->setVisible(sheetTe_->isChecked()); slottedDetails_->setVisible(sheetTe_->isChecked());
  aileronDetails_->setVisible(ailerons_->isChecked()); flapDetails_->setVisible(flaps_->isChecked());
  updateWiringHoleRibRanges();
  for (auto& wiring : wiringHoleWidgets_)
    wiring.details->setVisible(wiring.enabled->isChecked());
  if (showRootChord_) {
    spoilerDetails_->setVisible(spoilers_->isChecked());
    spoilerMinimumWoodMarginDetails_->setVisible(
        spoilerLighteningHoles_->isChecked());
    spoilerMinimumCircleDistanceDetails_->setVisible(
        spoilerLighteningHoles_->isChecked());
    constexpr int minimumStart = 1;
    spoilerStartRib_->setRange(minimumStart, std::max(minimumStart, ribCount_->value() - 3));
    spoilerEndRib_->setRange(std::min(ribCount_->value(), spoilerStartRib_->value() + 3),
                             ribCount_->value());
    if (spoilerSupportRailHeight_)
      spoilerSupportRailHeight_->setText(globalUnit_ == DisplayUnit::Inches ? "1/8 in" : "3 mm");
  }
  if (showJoinerPage_) updateJoinerEditorControls();
  const int ribs = ribCount_->value();
  ribLighteningStartRib_->setRange(1, ribs);
  ribLighteningStopRib_->setRange(1, ribs);
  const int ribletMinimum = showRootChord_ ? 2 : 1;
  ribletStartRib_->setRange(
      ribletMinimum, std::max(ribletMinimum, ribs - 1));
  ribletEndRib_->setRange(ribletMinimum, ribs);
  for (auto* input : {leTopSheetStopRib_, leBottomSheetStopRib_,
                      teTopSheetStopRib_, teBottomSheetStopRib_})
    input->setRange(2, ribs);
  const int lastInternalRib = std::max(2, ribs - 1);
  for (auto* input : {aileronStart_, flapStart_, flapStop_})
    input->setRange(2, lastInternalRib);
  aileronStop_->setRange(2, ribs);
}

void WingPanelEditor::emitChanged() { emit changed(); }

} // namespace designrc::gui
