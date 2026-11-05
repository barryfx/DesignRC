#pragma once

#include "gui/WingPanelEditor.h"

namespace designrc::gui {
struct SparAlignmentReference {
  QString key;
  QString label;
};
[[nodiscard]] QString sparAlignmentKey(const WingPanelData& panel, const QString& id);
void reidentifySparReferencePanel(WingPanelData& panel);
[[nodiscard]] std::vector<SparAlignmentReference> sparAlignmentReferences(
    const std::vector<WingPanelData>& panels, std::size_t currentPanel,
    DisplayUnit unit = DisplayUnit::Millimeters);
// Resolve references on a copy before geometry generation. Manual tip positions
// remain separately available for saving and restoring manual mode.
// Outside-range resolution is only for detecting and correcting collisions.
void resolveSparParallelLocations(std::vector<WingPanelData>& panels,
    DisplayUnit unit = DisplayUnit::Millimeters, bool allowOutsideChordRange = false);
// One aggregated, advisory dialog; false means generation was cancelled.
bool confirmSparJoinerAlignment(QWidget* parent, const std::vector<WingPanelData>& panels,
    DisplayUnit unit = DisplayUnit::Millimeters);
}
