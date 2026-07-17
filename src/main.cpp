#include "gui/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
  QApplication application{argc, argv};
  application.setApplicationName("DesignRC");
  application.setOrganizationName("DesignRC");
  designrc::gui::MainWindow window;
  window.show();
  return application.exec();
}

