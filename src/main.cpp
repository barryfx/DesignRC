#include "gui/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
  QApplication application{argc, argv};
  application.setApplicationName("DesignRC");
  application.setApplicationVersion(DESIGNRC_VERSION);
  application.setOrganizationName("DesignRC");
  designrc::gui::MainWindow window;
  window.showMaximized();
  return application.exec();
}
