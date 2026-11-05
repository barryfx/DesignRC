#include "gui/SparAlignment.h"
#include "domain/WingDesign.h"
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <stdexcept>

namespace designrc::gui {
namespace {
using domain::Point2;
struct Line { Point2 first, last; };
domain::SparParameters parameters(const SparDefaults& s) {
  return {s.chordLocationPercent, s.verticalLocation, s.material, s.type,
      s.woodHeight, s.woodWidth, s.tubeOd, s.tubeId, s.rodOd,
      s.stripWidth, s.stripThickness, s.tipChordLocationPercent};
}
QString key(const QString& panel, const QString& part) {
  return panel + "/" + part;
}
class References {
  struct Node {
    QString label;
    std::function<Line()> calculate;
    int state{};
    Line value;
  };
  std::vector<WingPanelData> panels_;
  std::vector<std::vector<domain::RibDefinition>> ribs_;
  std::map<QString, Node> nodes_;
  std::vector<QString> order_;
  std::map<std::size_t, domain::StructuredWing> controlWings_;
  DisplayUnit unit_;
  bool allowOutsideChordRange_{};

  void add(std::size_t panel, const QString& part, const QString& label, std::function<Line()> calculate) {
    const auto inserted = nodes_.emplace(key(panels_[panel].referenceId, part),
        Node{QString{"Panel %1 — %2"}.arg(panel + 1).arg(label), std::move(calculate)});
    if (!inserted.second) throw std::invalid_argument("Duplicate geometry reference IDs in project");
    order_.push_back(inserted.first->first);
  }
  Line get(const QString& id) {
    auto found = nodes_.find(id);
    if (found == nodes_.end())
      throw std::invalid_argument("Parallel to reference is no longer available. Select another reference or Manual.");
    auto& node = found->second;
    if (node.state == 1)
      throw std::invalid_argument("Circular Parallel to references: a spar cannot depend on itself.");
    if (node.state == 2) return node.value;
    node.state = 1;
    try { node.value = node.calculate(); node.state = 2; }
    catch (...) { node.state = 0; throw; }
    return node.value;
  }
  Line fractionLine(std::size_t p, double root, double tip, std::size_t first = 0,
                    std::size_t last = static_cast<std::size_t>(-1), int vertical = 2, double height = 1) {
    const auto& ribs = ribs_[p];
    if (last == static_cast<std::size_t>(-1)) last = ribs.size() - 1;
    domain::SparParameters s;
    s.verticalLocation = vertical;
    s.material = 0;
    s.woodHeight = height;
    return {domain::sparPlanPoint(ribs.at(first), s, root), domain::sparPlanPoint(ribs.at(last), s, tip)};
  }
  std::size_t station(std::size_t p, int number) const {
    if (number < 1 || number > panels_[p].ribCount)
      throw std::invalid_argument("Parallel to reference has an invalid rib range");
    return static_cast<std::size_t>(number - 1 + (panels_[p].addRib1a && number >= 2 ? 1 : 0));
  }
  Line hinge(std::size_t p, bool aileron) {
    const auto& d = panels_[p];
    const auto first = station(p, aileron ? d.aileronStartRib : d.flapStartRib);
    const auto last = station(p, aileron ? d.aileronStopRib : d.flapStopRib);
    if (first >= last) throw std::invalid_argument("Parallel to control needs distinct start and stop ribs");
    const auto& a = ribs_[p][first];
    const auto& b = ribs_[p][last];
    const double width = aileron ? d.aileronWidth : d.flapWidth;
    double x0 = a.leadingEdgeOffset + a.chord - width;
    double x1 = b.leadingEdgeOffset + b.chord - width;
    if (aileron ? d.aileronPerpendicularToRibs : d.flapPerpendicularToRibs) x1 = x0;
    else if (!d.planform.empty() && (aileron ? d.aileronHingeParallelY : d.flapHingeParallelY)) {
      double maximum = std::max(d.planform.trailingX(a.planformStation), d.planform.trailingX(b.planformStation));
      for (const auto v : d.planform.trailing)
        if (v.y >= a.planformStation && v.y <= b.planformStation) maximum = std::max(maximum, v.x);
      x0 = x1 = maximum - width;
    }
    return fractionLine(p, 100 * (x0 - a.leadingEdgeOffset) / a.chord,
        100 * (x1 - b.leadingEdgeOffset) / b.chord, first, last);
  }
  const domain::StructuredWing& controlWing(std::size_t p) {
    if (const auto it = controlWings_.find(p); it != controlWings_.end()) return it->second;
    const auto& d = panels_[p];
    domain::StructureParameters s;
    const auto number = [&](int n) { return n + (d.addRib1a && n >= 2 ? 1 : 0); };
    s.ribThickness = d.ribThickness;
    s.rib1aPresent = d.addRib1a;
    s.ailerons = d.ailerons; s.flaps = d.flaps;
    s.aileronWidth = d.aileronWidth; s.flapWidth = d.flapWidth;
    s.aileronHeight = d.aileronHeight; s.flapHeight = d.flapHeight;
    s.aileronHingePostWidth = d.aileronHingePostWidth; s.flapHingePostWidth = d.flapHingePostWidth;
    s.aileronHingePostHeight = d.aileronHingePostHeight; s.flapHingePostHeight = d.flapHingePostHeight;
    s.aileronStartRib = number(d.aileronStartRib); s.aileronStopRib = number(d.aileronStopRib);
    s.flapStartRib = number(d.flapStartRib); s.flapStopRib = number(d.flapStopRib);
    s.aileronPerpendicularToRibs = d.aileronPerpendicularToRibs; s.flapPerpendicularToRibs = d.flapPerpendicularToRibs;
    s.aileronHingeParallelY = d.aileronHingeParallelY; s.flapHingeParallelY = d.flapHingeParallelY;
    s.builtUpAileron = d.builtUpAileron; s.builtUpFlap = d.builtUpFlap;
    s.controlSurfaceGap = unit_ == DisplayUnit::Inches ? 25.4 / 16 : 1.5;
    s.trailingEdgeType = d.trailingEdgeType; s.trailingEdgeWidth = d.trailingEdgeWidth;
    s.trailingEdgeHeight = d.trailingEdgeHeight;
    s.topTeSheeting = d.topTeSheeting; s.bottomTeSheeting = d.bottomTeSheeting;
    s.topTeSheetingWidth = d.topTeSheetingWidth; s.bottomTeSheetingWidth = d.bottomTeSheetingWidth;
    s.topTeSheetingThickness = d.topTeSheetingThickness; s.bottomTeSheetingThickness = d.bottomTeSheetingThickness;
    return controlWings_.emplace(p, domain::applyWingStructure(ribs_[p], s)).first->second;
  }
public:
  References(const std::vector<WingPanelData>& panels, DisplayUnit unit, bool allowOutsideChordRange = false)
      : panels_(panels), unit_(unit), allowOutsideChordRange_(allowOutsideChordRange) {
    for (std::size_t p = 1; p < panels_.size(); ++p)
      panels_[p].rootAirfoil = panels_[p - 1].tipAirfoil;
    const auto error = matchPanelRootChords(panels_);
    if (!error.isEmpty()) throw std::invalid_argument(error.toStdString());
    std::vector<double> angles, twists;
    for (const auto& d : panels_) { angles.push_back(d.dihedral); twists.push_back(d.twist); }
    const auto assembly = domain::calculatePanelAssemblyAngles(angles);
    const auto twistRanges = domain::calculatePanelTwistRanges(twists);
    for (std::size_t p = 0; p < panels_.size(); ++p) {
      const auto& d = panels_[p];
      domain::WingParameters w;
      w.rootChord = d.rootChord; w.tipChord = d.tipChord; w.sweep = d.sweep;
      w.halfSpan = d.panelSpan; w.ribCount = d.ribCount; w.ribThickness = d.ribThickness;
      w.planform = d.planform;
      w.rootTwistDegrees = twistRanges[p].rootTwistDegrees;
      w.tipTwistDegrees = twistRanges[p].tipTwistDegrees;
      auto ribs = domain::generateRibs(w, d.rootAirfoil, d.tipAirfoil);
      if (d.addRib1a) ribs.insert(ribs.begin() + 1,
          domain::ribAtStation(w, d.rootAirfoil, d.tipAirfoil, .5 / (d.ribCount - 1)));
      const double angle = assembly[p].panelInclinationDegrees * std::numbers::pi / 180;
      for (std::size_t i = 0; i < ribs.size(); ++i) {
        const double span = ribs[i].spanPosition;
        ribs[i].spanPosition = std::cos(angle) * span;
        ribs[i].dihedralHeight = std::sin(angle) * span;
        ribs[i].ribPlaneAngleDegrees = i == 0 ? assembly[p].rootRibAngleDegrees :
            i + 1 == ribs.size() ? assembly[p].tipRibAngleDegrees : assembly[p].intermediateRibAngleDegrees;
      }
      ribs_.push_back(std::move(ribs));
    }
    for (std::size_t p = 0; p < panels_.size(); ++p) {
      const auto& d = panels_[p];
      add(p, "edge/le", "Leading Edge", [=, this] { return fractionLine(p, 0, 0); });
      add(p, "edge/te", "Trailing Edge", [=, this] { return fractionLine(p, 100, 100); });
      for (std::size_t i = 0; i < d.spars.size(); ++i) {
        const auto s = d.spars[i];
        add(p, "spar/" + s.referenceId, QString{"Spar %1"}.arg(i + 1), [=, this] {
          auto parameter = parameters(s);
          double tip = s.tipChordLocationPercent < 0 ? s.chordLocationPercent : s.tipChordLocationPercent;
          if (!s.parallelTo.isEmpty()) {
            if (!s.parallelTo.startsWith(d.referenceId + "/"))
              throw std::invalid_argument("Parallel to references must be in the current panel");
            const auto line = get(s.parallelTo);
            tip = domain::sparTipParallelTo(ribs_[p], parameter,
                {line.last.x - line.first.x, line.last.y - line.first.y}, allowOutsideChordRange_);
          }
          return Line{domain::sparPlanPoint(ribs_[p].front(), parameter, s.chordLocationPercent),
              domain::sparPlanPoint(ribs_[p].back(), parameter, tip)};
        });
      }
      for (const auto aileron : {true, false}) {
        if (!(aileron ? d.ailerons : d.flaps)) continue;
        const QString part = aileron ? "aileron" : "flap", name = aileron ? "Aileron" : "Flap";
        add(p, part, name + " hinge", [=, this] { return hinge(p, aileron); });
        add(p, part + "/te", name + " trailing edge", [=, this] {
          return fractionLine(p, 100, 100, station(p, aileron ? d.aileronStartRib : d.flapStartRib),
              station(p, aileron ? d.aileronStopRib : d.flapStopRib));
        });
        const auto& construction = aileron ? d.builtUpAileron : d.builtUpFlap;
        if (!construction.enabled) continue;
        for (std::size_t i = 0; i < construction.spars.size(); ++i) {
          const QString id = QString::fromStdString(construction.spars[i].referenceId);
          add(p, part + "/spar/" + (id.isEmpty() ? QString::number(i) : id),
              name + QString{" Spar %1"}.arg(i + 1), [=, this] {
            const auto& wing = controlWing(p);
            const auto control = std::find_if(wing.controlSurfaces.begin(), wing.controlSurfaces.end(),
                [&](const auto& c) { return c.name == name.toStdString(); });
            if (control == wing.controlSurfaces.end() || !control->builtUpWing)
              throw std::invalid_argument("Parallel to control spar is unavailable");
            const auto& built = *control->builtUpWing;
            const auto memberIt = std::find_if(built.members.begin(), built.members.end(),
                [&](const auto& m) { return m.name == "Spar " + std::to_string(i + 1); });
            if (memberIt == built.members.end()) throw std::invalid_argument("Parallel to control spar is unavailable");
            const auto& member = *memberIt;
            // The generated center fractions include the moving hinge gap and bevel.
            const auto point = [&](std::size_t r) {
              auto rib = built.ribs[r].rib;
              // Retain the generated member's height as well as its chord position.
              const auto translation = domain::ribTwistTranslation(rib);
              const auto c = member.centers[r];
              const double t = rib.twistDegrees * std::numbers::pi / 180;
              const double a = rib.ribPlaneAngleDegrees * std::numbers::pi / 180;
              const double z = std::sin(t) * c.x + std::cos(t) * c.y + translation.y;
              return Point2{rib.leadingEdgeOffset + std::cos(t) * c.x - std::sin(t) * c.y + translation.x,
                  rib.spanPosition - std::sin(a) * z};
            };
            return Line{point(0), point(built.ribs.size() - 1)};
          });
        }
      }
      const auto circular = [=, this] { return Line{{0, 0}, {0, d.panelSpan}}; };
      if (d.joinerPanelMode == 1)
        for (std::size_t i = 0; i < d.fixedJoiners.size(); ++i) {
          const auto j = d.fixedJoiners[i];
          add(p, "fixed/" + j.referenceId, QString{"Fixed Joiner %1"}.arg(i + 1), [=, this] {
            if (j.material != 0) return circular();
            const auto end = static_cast<std::size_t>(j.ribBays);
            if (end >= ribs_[p].size()) throw std::invalid_argument("Parallel to wood joiner has an invalid rib span");
            return fractionLine(p, j.chordLocationPercent, j.chordLocationPercent, 0, end);
          });
        }
      if (d.joinerPanelMode == 0)
        for (std::size_t i = 0; i < d.removableJoiners.size(); ++i) {
          const auto& j = d.removableJoiners[i];
          add(p, "removable/" + j.referenceId,
              QString{j.kind == 0 ? "Removable Joiner %1" : "Alignment Pin %1"}.arg(i + 1), circular);
        }
      if (d.turbulators)
        for (int i = 1; i <= d.turbulatorCount; ++i)
          add(p, QString{"turbulator/%1"}.arg(i), QString{"Turbulator %1"}.arg(i), [=, this] {
            const double percent = 25.0 * i / (d.turbulatorCount + 1);
            return fractionLine(p, percent, percent, 0, ribs_[p].size() - 1, 0, d.turbulatorHeight);
          });
      if (d.spoilers) add(p, "spoiler", "Spoiler and spanwise frame rails", [=, this] {
        return fractionLine(p, d.spoilerChordLocationPercent, d.spoilerChordLocationPercent,
            station(p, d.spoilerStartRib), station(p, d.spoilerEndRib));
      });
      for (const bool top : {true, false}) {
        const bool front = top ? d.leTopSheet : d.leBottomSheet;
        const bool rear = top ? d.teTopSheet : d.teBottomSheet;
        const QString side = top ? "top" : "bottom", title = top ? "Top" : "Bottom";
        if (front || rear) {
          const auto boundary = [=, this](bool isRear) {
            const bool toSpar = isRear || (top ? d.leTopSheetUpToSpar : d.leBottomSheetUpToSpar);
            if (toSpar) {
              const SparDefaults* best = nullptr;
              for (const auto& s : panels_[p].spars)
                if (s.verticalLocation == (top ? 0 : 1) && (!best ||
                    std::abs(s.chordLocationPercent - 25) < std::abs(best->chordLocationPercent - 25))) best = &s;
              if (best) return get(sparAlignmentKey(panels_[p], best->referenceId));
              return fractionLine(p, 25, 25);
            }
            const double percent = top ? d.leTopSheetStopChordPercent : d.leBottomSheetStopChordPercent;
            const int stop = top ? d.leTopSheetStopRib : d.leBottomSheetStopRib;
            return fractionLine(p, percent, percent, 0, station(p, stop));
          };
          if (front) add(p, "sheet/front/" + side, "Front " + title + " Sheeting (aft edge)", [boundary] { return boundary(false); });
          if (rear) add(p, "sheet/rear/" + side, "Rear " + title + " Sheeting (forward edge)", [boundary] { return boundary(true); });
        }
        if (top ? d.topTeSheeting : d.bottomTeSheeting)
          add(p, "sheet/te/" + side, title + " TE Sheeting", [=, this] { return fractionLine(p, 100, 100); });
      }
      if (d.sparShearWebs)
        for (std::size_t i = 0; i < d.spars.size(); ++i)
          if (d.spars[i].material == 0 && d.spars[i].verticalLocation == 0)
            add(p, "web/" + d.spars[i].referenceId, QString{"Spar %1 Shear Webs"}.arg(i + 1),
                [=, this] { return get(sparAlignmentKey(panels_[p], d.spars[i].referenceId)); });
    }
  }
  std::vector<SparAlignmentReference> list(std::size_t panel) const {
    std::vector<SparAlignmentReference> result;
    const QString prefix = panels_.at(panel).referenceId + "/";
    for (const auto& id : order_)
      if (id.startsWith(prefix)) result.push_back({id, nodes_.at(id).label.section(" — ", 1)});
    return result;
  }
  double tip(std::size_t p, const SparDefaults& spar) {
    if (!spar.parallelTo.startsWith(panels_[p].referenceId + "/"))
      throw std::invalid_argument("Parallel to references must be in the current panel");
    const auto line = get(spar.parallelTo);
    return domain::sparTipParallelTo(ribs_[p], parameters(spar),
        {line.last.x - line.first.x, line.last.y - line.first.y}, allowOutsideChordRange_);
  }
  bool parallel(std::size_t p, const SparDefaults& spar, const QString& joiner) {
    const auto a = get(sparAlignmentKey(panels_[p], spar.referenceId));
    const auto b = get(joiner);
    const Point2 u{a.last.x - a.first.x, a.last.y - a.first.y};
    const Point2 v{b.last.x - b.first.x, b.last.y - b.first.y};
    const double lengths = std::hypot(u.x, u.y) * std::hypot(v.x, v.y);
    if (lengths < 1e-8) throw std::invalid_argument("Joiner has no usable spanwise direction");
    return std::abs(u.x * v.y - u.y * v.x) / lengths <= std::sin(.05 * std::numbers::pi / 180);
  }
};
}
QString sparAlignmentKey(const WingPanelData& panel, const QString& id) { return key(panel.referenceId, "spar/" + id); }
void reidentifySparReferencePanel(WingPanelData& panel) {
  const QString previous = panel.referenceId + "/";
  panel.referenceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  for (auto& spar : panel.spars)
    if (spar.parallelTo.startsWith(previous))
      spar.parallelTo = panel.referenceId + "/" + spar.parallelTo.mid(previous.size());
}
std::vector<SparAlignmentReference> sparAlignmentReferences(const std::vector<WingPanelData>& panels,
    std::size_t currentPanel, DisplayUnit unit) {
  return References(panels, unit).list(currentPanel);
}
void resolveSparParallelLocations(std::vector<WingPanelData>& panels, DisplayUnit unit, bool allowOutsideChordRange) {
  const bool linked = std::any_of(panels.begin(), panels.end(), [](const auto& p) {
    return std::any_of(p.spars.begin(), p.spars.end(), [](const auto& s) { return !s.parallelTo.isEmpty(); });
  });
  if (!linked) return;
  References references(panels, unit, allowOutsideChordRange);
  std::vector<std::vector<double>> tips(panels.size());
  for (std::size_t p = 0; p < panels.size(); ++p)
    for (const auto& spar : panels[p].spars)
      tips[p].push_back(spar.parallelTo.isEmpty() ? spar.tipChordLocationPercent : references.tip(p, spar));
  for (std::size_t p = 0; p < panels.size(); ++p)
    for (std::size_t s = 0; s < panels[p].spars.size(); ++s) {
      auto& spar = panels[p].spars[s];
      if (!spar.parallelTo.isEmpty() && spar.manualTipChordLocationPercent < 0)
        spar.manualTipChordLocationPercent = spar.tipChordLocationPercent;
      spar.tipChordLocationPercent = tips[p][s];
    }
}
bool confirmSparJoinerAlignment(QWidget* parent, const std::vector<WingPanelData>& panels, DisplayUnit unit) {
  QStringList warnings;
  try {
    References references(panels, unit, true);
    for (std::size_t p = 0; p < panels.size(); ++p) {
      const auto& panel = panels[p];
      const auto check = [&](double root, const QString& id, std::size_t number) {
        QStringList spars;
        for (std::size_t s = 0; s < panel.spars.size(); ++s)
          if (std::abs(panel.spars[s].chordLocationPercent - root) < 1e-6 &&
              !references.parallel(p, panel.spars[s], id))
            spars.append(QString::number(s + 1));
        if (spars.isEmpty()) return;
        QString text = QString{"Warning:  One or more spar %1 has the same root chord location as joiner %2, but is not parallel to the joiner.  Select Parallel to Joiner %2 to make it align."}
            .arg(spars.join(", ")).arg(number);
        if (panels.size() > 1) text.prepend(QString{"Panel %1\n"}.arg(p + 1));
        warnings.append(text);
      };
      if (panel.joinerPanelMode == 1)
        for (std::size_t j = 0; j < panel.fixedJoiners.size(); ++j)
          check(panel.fixedJoiners[j].chordLocationPercent,
              key(panel.referenceId, "fixed/" + panel.fixedJoiners[j].referenceId), j + 1);
      if (panel.joinerPanelMode == 0)
        for (std::size_t j = 0; j < panel.removableJoiners.size(); ++j)
          if (panel.removableJoiners[j].kind == 0)
            check(panel.removableJoiners[j].chordLocationPercent,
                key(panel.referenceId, "removable/" + panel.removableJoiners[j].referenceId), j + 1);
    }
  } catch (const std::exception&) {
    // Normal generation validation reports invalid geometry and references.
    return true;
  }
  if (warnings.isEmpty()) return true;
  QMessageBox dialog{QMessageBox::Warning, "Spar/joiner alignment", warnings.join("\n\n"),
      QMessageBox::NoButton, parent};
  dialog.setObjectName("sparJoinerAlignmentWarning");
  auto* proceed = dialog.addButton("Continue", QMessageBox::AcceptRole);
  auto* cancel = dialog.addButton("Cancel", QMessageBox::RejectRole);
  dialog.setDefaultButton(cancel);
  dialog.setEscapeButton(cancel);
  dialog.exec();
  return dialog.clickedButton() == proceed;
}
}
