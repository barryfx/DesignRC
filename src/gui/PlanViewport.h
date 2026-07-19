#pragma once

#include "gui/TechnicalDrawing.h"

#include <QGraphicsView>
#include <QString>

class QGraphicsScene;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace designrc::gui {

class PlanViewport final : public QGraphicsView {
public:
  explicit PlanViewport(QWidget* parent = nullptr);

  void setDocument(const TechnicalDrawingDocument& document);
  void clearPlan();
  void fitAll();
  [[nodiscard]] bool exportPdf(const QString& path, QString& error) const;

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  QGraphicsScene* scene_{};
  TechnicalDrawingDocument document_;
  bool fitOnNextResize_{false};
};

} // namespace designrc::gui
