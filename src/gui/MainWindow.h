#pragma once

#include "domain/WingDesign.h"
#include "domain/WingStructure.h"
#include "gui/WingPanelEditor.h"

#include <QMainWindow>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class QLabel;
class QSpinBox;
class QTabWidget;
class QCloseEvent;
class QProgressBar;
class QPushButton;
class QThread;

namespace designrc::gui {

class OcctViewport;
class PlanViewport;

class MainWindow final : public QMainWindow {
public:
  explicit MainWindow(QWidget* parent = nullptr);

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  void buildMenus();
  void rebuildPanelTabs(const std::vector<WingPanelData>& panels);
  void changePanelCount(int count);
  void markPreviewPending();
  void generatePlan();
  void exportPlanPdf();
  void invalidatePlan();
  void regeneratePreview();
  void regeneratePreviewSynchronous();
  void regeneratePreviewLegacy();
  void exportRibs();
  void newProject();
  void openProject();
  bool saveProject();
  bool saveProjectAs();
  bool writeProject(const QString& path);
  void updateWindowTitle();
  void openDefaults();
  void openHelp();
  void showAbout();
  void copyFocusedText();
  void pasteFocusedText();
  [[nodiscard]] std::vector<WingPanelData> panelData() const;
  [[nodiscard]] std::vector<WingPanelData> defaultPanelData(DisplayUnit unit) const;
  [[nodiscard]] QJsonObject projectJson(const std::vector<WingPanelData>& panels,
                                        DisplayUnit unit) const;
  bool loadProjectJson(const QJsonObject& object);

  OcctViewport* viewport_{};
  PlanViewport* planViewport_{};
  QTabWidget* graphicsTabs_{};
  QSpinBox* panelCount_{};
  QTabWidget* panelTabs_{};
  QLabel* metrics_{};
  QPushButton* updateButton_{};
  QPushButton* generatePlanButton_{};
  QPushButton* exportPlanButton_{};
  QPushButton* cancelUpdateButton_{};
  QProgressBar* updateProgress_{};
  QThread* updateThread_{};
  std::shared_ptr<std::atomic_bool> updateCancellation_;
  std::vector<WingPanelEditor*> panelEditors_;
  std::vector<domain::RibDefinition> currentRibs_;
  domain::StructuredWing currentStructuredWing_;
  std::vector<domain::StructuredWing> currentStructuredPanels_;
  std::vector<double> currentRibThicknesses_;
  std::vector<WingPanelData> currentPlanParameters_;
  std::vector<double> currentDihedralAngles_;
  QString currentFile_;
  DisplayUnit globalUnit_{DisplayUnit::Millimeters};
  bool changingPanelCount_{false};
  bool projectModified_{false};
  std::uint64_t designRevision_{0};
};

} // namespace designrc::gui
