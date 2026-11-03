#include "gui/PlanViewport.h"
#include "domain/DxfExporter.h"

#include <QFontMetricsF>
#include <filesystem>

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
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace designrc::gui {
namespace {

QPainterPath drawingTextPath(const TechnicalDrawingText& text) {
  QFont font{"Arial"};
  font.setPointSizeF(text.heightMm * 72.0 / 25.4);
  const QFontMetricsF metrics{font};
  QPainterPath glyphs;
  double baseline = metrics.ascent();
  for (const auto& line : text.text.split('\n')) {
    glyphs.addText(QPointF{0.0, baseline}, font, line);
    baseline += metrics.height();
  }
  QTransform transform;
  transform.translate(text.position.x(), text.position.y());
  transform.rotate(text.rotationDegrees);
  return transform.map(glyphs);
}

QString svgNumber(const double value) {
  return QString::number(value, 'g', 15);
}

QString svgPathData(const QPainterPath& path) {
  QString data;
  QPointF start;
  for (int i = 0; i < path.elementCount(); ++i) {
    const auto element = path.elementAt(i);
    if (element.isMoveTo()) {
      start = {element.x, element.y};
      data += "M" + svgNumber(element.x) + " " + svgNumber(element.y) + " ";
    } else if (element.isLineTo()) {
      if (QPointF{element.x, element.y} == start &&
          (i + 1 == path.elementCount() || path.elementAt(i + 1).isMoveTo())) {
        data += "Z ";
      } else {
        data += "L" + svgNumber(element.x) + " " + svgNumber(element.y) + " ";
      }
    } else if (element.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
      const auto second = path.elementAt(++i);
      const auto end = path.elementAt(++i);
      data += "C" + svgNumber(element.x) + " " + svgNumber(element.y) + " " +
          svgNumber(second.x) + " " + svgNumber(second.y) + " " +
          svgNumber(end.x) + " " + svgNumber(end.y) + " ";
    }
  }
  return data.trimmed();
}

} // namespace

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

bool PlanViewport::exportSvg(const QString& path, QString& error) const {
  if (document_.empty() || document_.pageBoundsMm.isEmpty()) {
    error = "Generate a valid plan before exporting it.";
    return false;
  }
  QSaveFile file{path};
  if (!file.open(QIODevice::WriteOnly)) {
    error = file.errorString();
    return false;
  }
  QXmlStreamWriter xml{&file};
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement("svg");
  xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
  xml.writeAttribute("version", "1.1");
  const auto bounds = document_.pageBoundsMm;
  xml.writeAttribute("width", svgNumber(bounds.width()) + "mm");
  xml.writeAttribute("height", svgNumber(bounds.height()) + "mm");
  xml.writeAttribute("viewBox", svgNumber(bounds.left()) + " " + svgNumber(bounds.top()) +
      " " + svgNumber(bounds.width()) + " " + svgNumber(bounds.height()));
  xml.writeTextElement("title", "DesignRC Full-Scale Wing Plan");
  xml.writeTextElement("desc", "Full-scale plan in millimeters. Labels are outlined geometry.");
  xml.writeStartElement("rect");
  xml.writeAttribute("x", svgNumber(bounds.left()));
  xml.writeAttribute("y", svgNumber(bounds.top()));
  xml.writeAttribute("width", svgNumber(bounds.width()));
  xml.writeAttribute("height", svgNumber(bounds.height()));
  xml.writeAttribute("fill", "#ffffff");
  xml.writeEndElement();
  const auto writePath = [&](const QPainterPath& source, const QColor& stroke,
                              const QColor& fill, const double width) {
    xml.writeStartElement("path");
    xml.writeAttribute("d", svgPathData(source));
    xml.writeAttribute("stroke", stroke.alpha() == 0 ? "none" : stroke.name(QColor::HexRgb));
    if (stroke.alpha() != 0 && stroke.alpha() != 255)
      xml.writeAttribute("stroke-opacity", svgNumber(stroke.alphaF()));
    xml.writeAttribute("stroke-width", svgNumber(width));
    xml.writeAttribute("stroke-linejoin", "miter");
    xml.writeAttribute("fill", fill.alpha() == 0 ? "none" : fill.name(QColor::HexRgb));
    if (fill.alpha() != 0 && fill.alpha() != 255)
      xml.writeAttribute("fill-opacity", svgNumber(fill.alphaF()));
    xml.writeAttribute("fill-rule", source.fillRule() == Qt::OddEvenFill ? "evenodd" : "nonzero");
    xml.writeEndElement();
  };
  xml.writeStartElement("g");
  xml.writeAttribute("id", "plan-geometry");
  // Match scene stacking: filled components first, then open reference lines.
  for (const bool filled : {true, false})
    for (const auto& drawingPath : document_.paths)
      if ((drawingPath.fill.alpha() != 0) == filled)
        writePath(drawingPath.path, drawingPath.stroke, drawingPath.fill, drawingPath.lineWidthMm);
  xml.writeEndElement();
  xml.writeStartElement("g");
  xml.writeAttribute("id", "plan-annotation");
  for (const auto& text : document_.texts)
    writePath(drawingTextPath(text), Qt::transparent, QColor{55, 55, 55}, 0.0);
  xml.writeEndElement();
  xml.writeEndElement();
  xml.writeEndDocument();
  if (xml.hasError() || !file.commit()) {
    error = "Unable to finish SVG file: " + path;
    return false;
  }
  return true;
}

bool PlanViewport::exportDxf(const QString& path, QString& error) const {
  if (document_.empty() || document_.pageBoundsMm.isEmpty()) {
    error = "Generate a valid plan before exporting it.";
    return false;
  }
  try {
    std::vector<domain::PartDrawingPath> paths;
    const auto appendPath = [&](const QPainterPath& source, const std::string& layer,
                                const QColor& color) {
      // Flatten Bezier curves to fine line segments. Keep subpaths separate so
      // open leaders and holes never acquire artificial connecting edges.
      constexpr double precision = 100.0;
      const auto polygons = source.toSubpathPolygons(QTransform::fromScale(precision, precision));
      for (const auto& polygon : polygons) {
        if (polygon.size() < 2) continue;
        domain::PartDrawingPath output;
        output.layer = layer;
        output.rgb = color.rgb() & 0x00ffffffU;
        output.closed = polygon.front() == polygon.back();
        for (const auto& point : polygon)
          // Qt uses downward Y; DXF uses upward Y. Put the page at (0,0).
          output.points.push_back({point.x() / precision - document_.pageBoundsMm.left(),
                                   document_.pageBoundsMm.bottom() - point.y() / precision});
        if (output.closed) output.points.pop_back();
        paths.push_back(std::move(output));
      }
    };
    for (const auto& drawingPath : document_.paths)
      appendPath(drawingPath.path, "PLAN_GEOMETRY", drawingPath.stroke);
    // Match the viewport's font, baseline and rotation; export glyph outlines
    // as geometry so CAD imports do not depend on installed fonts.
    for (const auto& text : document_.texts)
      appendPath(drawingTextPath(text), "PLAN_ANNOTATION", QColor{55, 55, 55});
    domain::exportDrawingDxf(paths, std::filesystem::path{path.toStdWString()});
    return true;
  } catch (const std::exception& exception) {
    error = QString::fromUtf8(exception.what());
    return false;
  }
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
