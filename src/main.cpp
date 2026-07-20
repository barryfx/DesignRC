#include "gui/MainWindow.h"

#include <QApplication>
#include <QtGlobal>

int main(int argc, char* argv[]) {
#if defined(Q_OS_LINUX)
  // OCCT's Xw_Window embeds into an X11 window. WSLg and Wayland desktops
  // provide that window through XWayland when Qt uses its XCB backend.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
  QApplication application{argc, argv};
  application.setApplicationName("DesignRC");
  application.setApplicationVersion(DESIGNRC_VERSION);
  application.setOrganizationName("DesignRC");
  designrc::gui::MainWindow window;
  window.showMaximized();
  return application.exec();
}
