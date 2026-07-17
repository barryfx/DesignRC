#include "gui/OcctViewport.h"

#include <Aspect_DisplayConnection.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Graphic3d_CLight.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_TypeOfLightSource.hxx>
#include <Graphic3d_TypeOfShadingModel.hxx>
#include <Graphic3d_TransModeFlags.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_LineAspect.hxx>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <Quantity_Color.hxx>
#include <V3d_TypeOfVisualization.hxx>
#include <WNT_Window.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <NCollection_Vec2.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>

namespace designrc::gui {
namespace {

constexpr int kGizmoScreenOffset = 112;

} // namespace

OcctViewport::OcctViewport(QWidget* parent) : QWidget{parent} {
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_PaintOnScreen);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumSize(640, 480);
  setStyleSheet("background: white;");
}

QPaintEngine* OcctViewport::paintEngine() const { return nullptr; }

void OcctViewport::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  initializeViewer();
  QTimer::singleShot(0, this, [this] {
    if (view_.IsNull()) return;
    view_->MustBeResized();
    view_->Invalidate();
    redraw();
  });
}

void OcctViewport::paintEvent(QPaintEvent*) {
  initializeViewer();
  redraw();
}

void OcctViewport::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (!view_.IsNull()) view_->MustBeResized();
}

void OcctViewport::initializeViewer() {
  if (initialized_) return;
  initialized_ = true;

  const auto connection = occ::handle<Aspect_DisplayConnection>{new Aspect_DisplayConnection};
  const auto driver = occ::handle<OpenGl_GraphicDriver>{new OpenGl_GraphicDriver{connection}};
  viewer_ = occ::handle<V3d_Viewer>{new V3d_Viewer{driver}};
  viewer_->SetDefaultTypeOfView(V3d_ORTHOGRAPHIC);

  // Use lighting symmetric about the wing center plane. OCCT's default
  // directional lights illuminate mirrored surfaces at different diffuse and
  // specular intensities, making identical wood and aluminum materials appear
  // different on the left and right wing halves.
  const auto ambient = occ::handle<Graphic3d_CLight>{
      new Graphic3d_CLight{Graphic3d_TypeOfLightSource_Ambient}};
  ambient->SetIntensity(0.28F);
  viewer_->AddLight(ambient);
  viewer_->SetLightOn(ambient);

  const auto addDirectional = [this](const gp_Dir& direction) {
    const auto light = occ::handle<Graphic3d_CLight>{
        new Graphic3d_CLight{Graphic3d_TypeOfLightSource_Directional}};
    light->SetDirection(direction);
    light->SetIntensity(0.42F);
    viewer_->AddLight(light);
    viewer_->SetLightOn(light);
  };
  addDirectional(gp_Dir{-0.35, -0.55, -1.0});
  addDirectional(gp_Dir{-0.35,  0.55, -1.0});

  context_ = occ::handle<AIS_InteractiveContext>{new AIS_InteractiveContext{viewer_}};
  context_->SetDisplayMode(AIS_Shaded, false);
  view_ = viewer_->CreateView();
  const auto window = occ::handle<WNT_Window>{
      new WNT_Window{reinterpret_cast<Aspect_Handle>(winId()), Quantity_NOC_WHITE}};
  view_->SetWindow(window);
  view_->SetBackgroundColor(Quantity_NOC_WHITE);
  if (!window->IsMapped()) window->Map();

  view_->SetProj(V3d_XposYnegZpos);
  displayViewGizmo();
  if (!pendingWoodShape_.IsNull() || !pendingCarbonFiberShape_.IsNull() ||
      !pendingAluminumShape_.IsNull())
    displayPendingShapes();
  view_->MustBeResized();
  redraw();
}

void OcctViewport::displayShape(const TopoDS_Shape& shape) {
  displayMaterialShapes(shape, TopoDS_Shape{}, TopoDS_Shape{});
}

void OcctViewport::displayMaterialShapes(const TopoDS_Shape& wood,
                                         const TopoDS_Shape& carbonFiber,
                                         const TopoDS_Shape& aluminum) {
  pendingWoodShape_ = wood;
  pendingCarbonFiberShape_ = carbonFiber;
  pendingAluminumShape_ = aluminum;
  if (context_.IsNull()) return;
  displayPendingShapes();
  fitAll();
}

void OcctViewport::displayPendingShapes() {
  for (const auto& displayed : displayedShapes_) context_->Remove(displayed, false);
  displayedShapes_.clear();

  enum class Appearance { Wood, CarbonFiber, Aluminum };
  const auto display = [&](const TopoDS_Shape& shape, const Appearance appearance) {
    if (shape.IsNull() || !TopExp_Explorer{shape, TopAbs_FACE}.More()) return;
    auto object = occ::handle<AIS_Shape>{new AIS_Shape{shape}};
    // Geometry is triangulated on background workers before publication.
    object->Attributes()->SetAutoTriangulation(false);
    object->Attributes()->SetupOwnShadingAspect();
    if (appearance == Appearance::Wood) {
      object->Attributes()->SetShadingModel(Graphic3d_TOSM_UNLIT, true);
      Graphic3d_MaterialAspect material{Graphic3d_NOM_PLASTIC};
      material.SetShininess(0.25F);
      object->SetMaterial(material);
      object->SetColor(Quantity_Color{212.0 / 255.0, 189.0 / 255.0,
                                      165.0 / 255.0, Quantity_TOC_RGB});
      object->Attributes()->SetFaceBoundaryDraw(true);
      object->Attributes()->SetFaceBoundaryAspect(
          occ::handle<Prs3d_LineAspect>{new Prs3d_LineAspect{
              Quantity_Color{105.0 / 255.0, 80.0 / 255.0, 60.0 / 255.0,
                             Quantity_TOC_RGB},
              Aspect_TOL_SOLID, 1.0}});
    } else if (appearance == Appearance::CarbonFiber) {
      Graphic3d_MaterialAspect material{Graphic3d_NOM_PLASTIC};
      material.SetShininess(0.85F);
      object->SetMaterial(material);
      object->SetColor(Quantity_Color{0.015, 0.015, 0.015, Quantity_TOC_RGB});
    } else {
      object->Attributes()->SetShadingModel(Graphic3d_TOSM_UNLIT, true);
      Graphic3d_MaterialAspect material{Graphic3d_NOM_ALUMINIUM};
      material.SetShininess(0.9F);
      object->SetMaterial(material);
      object->SetColor(Quantity_Color{0.68, 0.70, 0.74, Quantity_TOC_RGB});
    }
    // Selection is not currently used; skip its expensive face hierarchy.
    context_->Display(object, AIS_Shaded, -1, false);
    displayedShapes_.push_back(object);
  };
  display(pendingWoodShape_, Appearance::Wood);
  display(pendingCarbonFiberShape_, Appearance::CarbonFiber);
  display(pendingAluminumShape_, Appearance::Aluminum);
  // Publish all material presentations before FitAll queries their combined
  // bounds. With several newly displayed AIS objects, deferred viewer updates
  // can otherwise leave the first automatic fit using incomplete extents.
  context_->UpdateCurrentViewer();
}

void OcctViewport::clearShape() {
  pendingWoodShape_.Nullify();
  pendingCarbonFiberShape_.Nullify();
  pendingAluminumShape_.Nullify();
  if (context_.IsNull()) return;
  for (const auto& displayed : displayedShapes_) context_->Remove(displayed, false);
  displayedShapes_.clear();
  redraw();
}

void OcctViewport::fitAll() {
  if (view_.IsNull()) return;
  view_->FitAll(0.05, false);
  view_->ZFitAll();
  redraw();
}

void OcctViewport::mousePressEvent(QMouseEvent* event) {
  lastMousePosition_ = event->position().toPoint();
  if (!view_.IsNull() && event->button() == Qt::LeftButton) {
    orbiting_ = true;
    view_->StartRotation(lastMousePosition_.x(), lastMousePosition_.y());
    event->accept();
    return;
  }
  if (event->button() == Qt::RightButton) {
    panning_ = true;
    event->accept();
    return;
  }
  if (event->button() == Qt::MiddleButton) {
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void OcctViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPoint position = event->position().toPoint();
  if (!view_.IsNull() && orbiting_) {
    view_->Rotation(position.x(), position.y());
    redraw();
  } else if (!view_.IsNull() && panning_) {
    view_->Pan(position.x() - lastMousePosition_.x(), lastMousePosition_.y() - position.y());
    redraw();
  }
  lastMousePosition_ = position;
}

void OcctViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) orbiting_ = false;
  if (event->button() == Qt::RightButton) panning_ = false;
  if (event->button() == Qt::MiddleButton) {
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void OcctViewport::wheelEvent(QWheelEvent* event) {
  if (view_.IsNull() || event->angleDelta().y() == 0) return;
  const QPoint position = event->position().toPoint();
  view_->StartZoomAtPoint(position.x(), position.y());
  view_->ZoomAtPoint(position.x(), position.y(), position.x(),
                     position.y() - event->angleDelta().y() / 4);
  redraw();
  event->accept();
}

void OcctViewport::redraw() {
  if (!view_.IsNull()) view_->Redraw();
}

void OcctViewport::displayViewGizmo() {
  if (context_.IsNull()) return;
  viewGizmoPersistence_ = occ::handle<Graphic3d_TransformPers>{new Graphic3d_TransformPers{
      Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
      NCollection_Vec2<int>{kGizmoScreenOffset, kGizmoScreenOffset}}};

  const auto prepare = [this](const occ::handle<AIS_Shape>& shape) {
    shape->SetTransformPersistence(viewGizmoPersistence_);
    shape->SetZLayer(Graphic3d_ZLayerId_Topmost);
  };
  const auto addDecoration = [this, &prepare](const TopoDS_Shape& shape,
                                             const Quantity_Color& color) {
    auto object = occ::handle<AIS_Shape>{new AIS_Shape{shape}};
    object->SetColor(color);
    prepare(object);
    context_->Display(object, AIS_Shaded, -1, false);
    viewGizmoObjects_.push_back(object);
  };
  const auto addAxis = [&addDecoration](const gp_Dir& direction, const Quantity_Color& color) {
    const gp_XYZ origin{0.0, 0.0, 0.0};
    const gp_XYZ vector{direction.X(), direction.Y(), direction.Z()};
    addDecoration(BRepPrimAPI_MakeCylinder(
        gp_Ax2{gp_Pnt{origin}, direction}, 3.5, 54.0).Shape(), color);
    addDecoration(BRepPrimAPI_MakeCone(
        gp_Ax2{gp_Pnt{origin + vector * 54.0}, direction},
        8.0, 0.0, 10.0).Shape(), color);
  };
  addAxis(gp_Dir{1.0, 0.0, 0.0}, Quantity_Color{0.82, 0.12, 0.08, Quantity_TOC_RGB});
  addAxis(gp_Dir{0.0, 1.0, 0.0}, Quantity_Color{0.10, 0.55, 0.18, Quantity_TOC_RGB});
  addAxis(gp_Dir{0.0, 0.0, 1.0}, Quantity_Color{0.12, 0.32, 0.90, Quantity_TOC_RGB});
  context_->UpdateCurrentViewer();
}

} // namespace designrc::gui
