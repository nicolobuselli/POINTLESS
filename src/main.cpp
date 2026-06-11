#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ULTRA_Ditherer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("ULTRA_Ditherer");

    QFontDatabase::addApplicationFont(":/fonts/FunnelDisplay.ttf");

    // Load stylesheet
    QFile qss(":/style.qss");
    if (qss.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    MainWindow w;
    w.show();

    return app.exec();
}
