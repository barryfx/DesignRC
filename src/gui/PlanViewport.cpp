#include "gui/PlanViewport.h"

#include <QBrush>
#include <QFont>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace designrc::gui {

PlanViewport::PlanViewport(QWidget* parent) : QGraphicsView{parent},
    scene_{new QGraphicsScene{this}} {
  setScene(scene_);
  setBackgroundBrush(QColor{232, 232, 232});
  setRenderHint(QPainter::Antialiasing, true);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setFrameShape(QFrame::NoFrame);
}

void PlanViewport::setDocument(const TechnicalDrawingDocument& document) {
  scene_->clear();
  document_ = document;
  if (document_.empty()) return;

  auto* page = scene_->addRect(document.pageBoundsMm,
      QPen{QColor{185, 185, 185}, 0.0}, QBrush{Qt::white});
  page->setZValue(-100.0);

  for (const auto& drawingPath : document.paths) {
    QPen pen{drawingPath.stroke};
    pen.setWidthF(drawingPath.lineWidthMm >= 0.45 ? 1.4 : 1.0);
    pen.setCosmetic(true);
    pen.setJoinStyle(Qt::MiterJoin);
    auto* item = scene_->addPath(drawingPath.path, pen, QBrush{drawingPath.fill});
    item->setZValue(drawingPath.fill.alpha() == 0 ? 2.0 : 1.0);
  }
  for (const auto& drawingText : document.texts) {
    QFont font;
    font.setFamily("Arial");
    font.setPointSizeF(drawingText.heightMm * 72.0 / 25.4);
    auto* item = scene_->addSimpleText(drawingText.text, font);
    item->setBrush(QColor{55, 55, 55});
    item->setPos(drawingText.position);
    item->setRotation(drawingText.rotationDegrees);
    item->setZValue(3.0);
  }
  scene_->setSceneRect(document.pageBoundsMm);
  fitOnNextResize_ = true;
  fitAll();
  fitOnNextResize_ = true;
}

void PlanViewport::clearPlan() {
  scene_->clear();
  document_ = {};
  scene_->setSceneRect({});
  resetTransform();
  fitOnNextResize_ = false;
}

void PlanViewport::fitAll() {
  if (scene_->items().empty()) return;
  fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
  fitOnNextResize_ = false;
}

bool PlanViewport::exportPdf(const QString& path, QString& error) const {
  if (document_.empty() || document_.pageBoundsMm.isEmpty()) {
    error = "Generate a valid plan before exporting it.";
    return false;
  }

  QPdfWriter writer{path};
  writer.setTitle("DesignRC Full-Scale Wing Plan");
  writer.setCreator("DesignRC");
  writer.setResolution(600);
  const QPageSize pageSize{document_.pageBoundsMm.size(), QPageSize::Millimeter,
                           "DesignRC Plan", QPageSize::ExactMatch};
  if (!pageSize.isValid() || !writer.setPageSize(pageSize) ||
      !writer.setPageMargins(QMarginsF{}, QPageLayout::Millimeter)) {
    error = "The full-scale plan page size is not supported by the PDF writer.";
    return false;
  }

  QPainter painter{&writer};
  if (!painter.isActive()) {
    error = QString{"Unable to create PDF file: %1"}.arg(path);
    return false;
  }
  const QRect target = writer.pageLayout().paintRectPixels(writer.resolution());
  scene_->render(&painter, QRectF{target}, document_.pageBoundsMm,
                 Qt::IgnoreAspectRatio);
  painter.end();
  if (!QFileInfo::exists(path) || QFileInfo{path}.size() == 0) {
    error = QString{"Unable to finish PDF file: %1"}.arg(path);
    return false;
  }
  return true;
}

void PlanViewport::resizeEvent(QResizeEvent* event) {
  QGraphicsView::resizeEvent(event);
  if (fitOnNextResize_) fitAll();
}

void PlanViewport::showEvent(QShowEvent* event) {
  QGraphicsView::showEvent(event);
  QTimer::singleShot(0, this, [this] { fitAll(); });
}

void PlanViewport::wheelEvent(QWheelEvent* event) {
  const double factor = std::pow(1.0015, event->angleDelta().y());
  const double targetScale = transform().m11() * factor;
  if (targetScale >= 0.02 && targetScale <= 100.0) scale(factor, factor);
  event->accept();
}

} // namespace designrc::gui
