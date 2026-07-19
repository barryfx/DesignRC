#pragma once

#include "domain/WingStructure.h"

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

namespace designrc::gui {

struct WingPanelData;

struct TechnicalDrawingPath {
  QPainterPath path;
  QColor stroke;
  QColor fill{Qt::transparent};
  double lineWidthMm{0.25};
};

struct TechnicalDrawingText {
  QPointF position;
  QString text;
  double heightMm{4.0};
  double rotationDegrees{};
};

struct TechnicalDrawingDocument {
  QRectF pageBoundsMm;
  std::vector<TechnicalDrawingPath> paths;
  std::vector<TechnicalDrawingText> texts;

  [[nodiscard]] bool empty() const { return paths.empty(); }
};

[[nodiscard]] TechnicalDrawingDocument buildFlattenedWingPlan(
    const std::vector<domain::StructuredWing>& panels,
    const std::vector<double>& ribThicknesses,
    const std::vector<WingPanelData>& panelParameters,
    bool useInches,
    const QString& projectFileName);

} // namespace designrc::gui
