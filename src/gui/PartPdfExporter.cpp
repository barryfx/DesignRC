#include "gui/PartPdfExporter.h"

#include <QFont>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace designrc::gui {
namespace {

struct Bounds {
  double minimumX{std::numeric_limits<double>::max()};
  double maximumX{std::numeric_limits<double>::lowest()};
  double minimumY{std::numeric_limits<double>::max()};
  double maximumY{std::numeric_limits<double>::lowest()};
};

Bounds drawingBounds(const std::vector<domain::PartDrawing>& parts) {
  Bounds bounds;
  for (const auto& part : parts)
    for (const auto& path : part.paths)
      for (const auto point : path.points) {
        bounds.minimumX = std::min(bounds.minimumX, point.x);
        bounds.maximumX = std::max(bounds.maximumX, point.x);
        bounds.minimumY = std::min(bounds.minimumY, point.y);
        bounds.maximumY = std::max(bounds.maximumY, point.y);
      }
  if (bounds.minimumX > bounds.maximumX)
    throw std::invalid_argument("A PDF export requires at least one valid part");
  return bounds;
}

void drawParts(QPainter& painter, const std::vector<domain::PartDrawing>& parts,
               const std::function<QPointF(domain::Point2)>& pagePoint,
               const QRectF& clipRect, const double pixelsPerMm) {
  painter.save();
  painter.setClipRect(clipRect);
  QPen pen{Qt::black};
  pen.setWidthF(0.1 * pixelsPerMm);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  for (const auto& part : parts) {
    for (const auto& drawingPath : part.paths) {
      if (drawingPath.points.size() < 2) continue;
      if (drawingPath.spline && drawingPath.points.size() >= 3) {
        QPainterPath path{pagePoint(drawingPath.points.front())};
        for (std::size_t index = 0;
             index + 1 < drawingPath.points.size(); ++index) {
          const auto& previous = index == 0
              ? drawingPath.points[index] : drawingPath.points[index - 1];
          const auto& first = drawingPath.points[index];
          const auto& second = drawingPath.points[index + 1];
          const auto& next = index + 2 < drawingPath.points.size()
              ? drawingPath.points[index + 2] : second;
          const domain::Point2 control1{
              first.x + (second.x - previous.x) / 6.0,
              first.y + (second.y - previous.y) / 6.0};
          const domain::Point2 control2{
              second.x - (next.x - first.x) / 6.0,
              second.y - (next.y - first.y) / 6.0};
          path.cubicTo(
              pagePoint(control1), pagePoint(control2), pagePoint(second));
        }
        if (drawingPath.closed) path.closeSubpath();
        painter.drawPath(path);
        continue;
      }
      QPolygonF polygon;
      polygon.reserve(static_cast<qsizetype>(drawingPath.points.size()));
      for (const auto point : drawingPath.points) polygon.push_back(pagePoint(point));
      if (drawingPath.closed) painter.drawPolygon(polygon);
      else painter.drawPolyline(polygon);
    }
    const auto placement = domain::partLabelPlacement(part);
    if (!placement) continue;
    double minimumX = part.labelOutline.front().x;
    double maximumX = minimumX;
    for (const auto point : part.labelOutline) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
    }
    const double partWidth = std::max(0.1, maximumX - minimumX);
    const double textHeightMm = placement->height;
    QFont font{QStringLiteral("Arial")};
    font.setPixelSize(std::max(1, qRound(textHeightMm * pixelsPerMm)));
    painter.setFont(font);
    const auto position = pagePoint(placement->position);
    if (!clipRect.contains(position)) continue;
    const QRectF textRect{position.x() - partWidth * pixelsPerMm * 0.48,
                          position.y() - textHeightMm * pixelsPerMm,
                          partWidth * pixelsPerMm * 0.96,
                          2.0 * textHeightMm * pixelsPerMm};
    painter.drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine,
                     QString::fromStdString(part.label));
  }
  painter.restore();
}

void configureWriter(QPdfWriter& writer, const std::filesystem::path& path,
                     const int resolution) {
  writer.setResolution(resolution);
  writer.setPageMargins(QMarginsF{}, QPageLayout::Millimeter);
  writer.setTitle(QStringLiteral("DesignRC Parts"));
  writer.setCreator(QStringLiteral("DesignRC"));
  if (path.empty())
    throw std::invalid_argument("A PDF export requires an output path");
}

void writeSinglePartPdf(const domain::PartDrawing& part,
                        const std::filesystem::path& path) {
  const std::vector<domain::PartDrawing> parts{part};
  const auto bounds = drawingBounds(parts);
  constexpr double marginMm = 5.0;
  constexpr int resolution = 300;
  const double widthMm = std::max(1.0, bounds.maximumX - bounds.minimumX) +
      2.0 * marginMm;
  const double heightMm = std::max(1.0, bounds.maximumY - bounds.minimumY) +
      2.0 * marginMm;
  QPdfWriter writer{QString::fromStdWString(path.wstring())};
  configureWriter(writer, path, resolution);
  writer.setPageSize(QPageSize{QSizeF{widthMm, heightMm},
      QPageSize::Millimeter, QStringLiteral("DesignRC Part"),
      QPageSize::ExactMatch});
  QPainter painter{&writer};
  if (!painter.isActive())
    throw std::runtime_error("Unable to create PDF file: " + path.string());
  const double pixelsPerMm = static_cast<double>(resolution) / 25.4;
  const auto pagePoint = [&](const domain::Point2 point) {
    return QPointF{(point.x - bounds.minimumX + marginMm) * pixelsPerMm,
                   (bounds.maximumY - point.y + marginMm) * pixelsPerMm};
  };
  drawParts(painter, parts, pagePoint,
      QRectF{0.0, 0.0, widthMm * pixelsPerMm, heightMm * pixelsPerMm},
      pixelsPerMm);
  painter.end();
}

void writePosterPartsPdf(const std::vector<domain::PartDrawing>& parts,
                         const std::filesystem::path& path) {
  if (parts.empty())
    throw std::invalid_argument("An All Parts PDF requires at least one part");
  constexpr int resolution = 300;
  constexpr double marginMm = 5.0;
  const auto bounds = drawingBounds(parts);
  const double drawingWidthMm =
      std::max(1.0, bounds.maximumX - bounds.minimumX);
  const double drawingHeightMm =
      std::max(1.0, bounds.maximumY - bounds.minimumY);
  const double pageWidthMm = drawingWidthMm + 2.0 * marginMm;
  const double pageHeightMm = drawingHeightMm + 2.0 * marginMm;
  QPdfWriter writer{QString::fromStdWString(path.wstring())};
  configureWriter(writer, path, resolution);
  writer.setPageSize(QPageSize{
      QSizeF{pageWidthMm, pageHeightMm}, QPageSize::Millimeter,
      QStringLiteral("DesignRC Parts Poster"), QPageSize::ExactMatch});
  QPainter painter{&writer};
  if (!painter.isActive())
    throw std::runtime_error("Unable to create PDF file: " + path.string());
  const double pixelsPerMm = static_cast<double>(resolution) / 25.4;
  const auto pagePoint = [&](const domain::Point2 point) {
    return QPointF{
        (point.x - bounds.minimumX + marginMm) * pixelsPerMm,
        (bounds.maximumY - point.y + marginMm) * pixelsPerMm};
  };
  drawParts(
      painter, parts, pagePoint,
      QRectF{0.0, 0.0, pageWidthMm * pixelsPerMm,
             pageHeightMm * pixelsPerMm},
      pixelsPerMm);
  painter.end();
}

} // namespace

void exportPartPdf(const domain::PartDrawing& part,
                   const std::filesystem::path& path) {
  writeSinglePartPdf(part, path);
}

void exportPartsPdf(const std::vector<domain::PartDrawing>& parts,
                    const std::filesystem::path& path) {
  writePosterPartsPdf(domain::arrangePartDrawings(parts), path);
}

} // namespace designrc::gui
