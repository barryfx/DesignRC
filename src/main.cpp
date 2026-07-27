#include "gui/MainWindow.h"

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QScreen>
#include <QSplashScreen>
#include <QTimer>
#include <QtGlobal>

int main(int argc, char* argv[]) {
#if defined(Q_OS_WIN)
  // A Debug build may be launched from the project or out directory rather
  // than with the executable directory as its working directory. Point Qt at
  // the platform plug-in deployed beside the executable before QApplication
  // begins plug-in discovery.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM_PLUGIN_PATH") && argc > 0) {
    const QDir executableDirectory{QFileInfo{QString::fromLocal8Bit(argv[0])}.absolutePath()};
    const QString platformDirectory = executableDirectory.filePath("platforms");
    if (QFileInfo::exists(QDir{platformDirectory}.filePath("qwindowsd.dll")) ||
        QFileInfo::exists(QDir{platformDirectory}.filePath("qwindows.dll")))
      qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformDirectory.toLocal8Bit());
  }
#endif
#if defined(Q_OS_LINUX)
  // OCCT's Xw_Window embeds into an X11 window. WSLg and Wayland desktops
  // provide that window through XWayland when Qt uses its XCB backend.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
  QApplication application{argc, argv};
  QApplication::setStyle("Fusion");
  application.setApplicationName("DesignRC");
  application.setApplicationVersion(DESIGNRC_VERSION);
  application.setOrganizationName("DesignRC");
#if defined(Q_OS_WIN)
  application.setWindowIcon(QIcon(":/graphics/designrc_smaller.ico"));
#else
  application.setWindowIcon(QIcon(":/graphics/designrc_icon.png"));
#endif
  if (application.arguments().contains("--joiner-backend-regression"))
    return designrc::gui::runJoinerBackendRegression();

  QSplashScreen splash{QPixmap(":/graphics/designrc_splash.png")};
  QScreen* splashScreen = QGuiApplication::screenAt(QCursor::pos());
  if (splashScreen == nullptr)
    splashScreen = QGuiApplication::primaryScreen();
  if (splashScreen != nullptr) {
    const QRect availableArea = splashScreen->availableGeometry();
    splash.move(availableArea.center() - splash.rect().center());
  }

  QElapsedTimer splashTimer;
  splashTimer.start();
  splash.show();
  application.processEvents();

  designrc::gui::MainWindow window;
  const int remainingSplashTime =
      qMax(0, 3000 - static_cast<int>(splashTimer.elapsed()));
  if (remainingSplashTime > 0) {
    QEventLoop delay;
    QTimer::singleShot(remainingSplashTime, &delay, &QEventLoop::quit);
    delay.exec();
  }
  window.showMaximized();
  splash.finish(&window);
  return application.exec();
}
