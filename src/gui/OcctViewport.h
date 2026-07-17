#pragma once

#include <QPoint>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Shape.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

class QMouseEvent;
class QPaintEngine;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace designrc::gui {

class OcctViewport final : public QWidget {
public:
  explicit OcctViewport(QWidget* parent = nullptr);

  void displayShape(const TopoDS_Shape& shape);
  void displayMaterialShapes(const TopoDS_Shape& wood,
                             const TopoDS_Shape& carbonFiber,
                             const TopoDS_Shape& aluminum);
  void clearShape();
  void fitAll();

protected:
  QPaintEngine* paintEngine() const override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  void initializeViewer();
  void displayPendingShapes();
  void displayViewGizmo();
  void redraw();

  occ::handle<V3d_Viewer> viewer_;
  occ::handle<V3d_View> view_;
  occ::handle<AIS_InteractiveContext> context_;
  std::vector<occ::handle<AIS_Shape>> displayedShapes_;
  occ::handle<Graphic3d_TransformPers> viewGizmoPersistence_;
  std::vector<occ::handle<AIS_InteractiveObject>> viewGizmoObjects_;
  TopoDS_Shape pendingWoodShape_;
  TopoDS_Shape pendingCarbonFiberShape_;
  TopoDS_Shape pendingAluminumShape_;
  QPoint lastMousePosition_;
  bool initialized_{false};
  bool orbiting_{false};
  bool panning_{false};
};

} // namespace designrc::gui
