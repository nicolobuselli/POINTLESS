#include <QApplication>
#include <QFile>
#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ULTRA_Ditherer");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("ULTRA_Ditherer");

    // Load stylesheet
    QFile qss(":/style.qss");
    if (qss.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    MainWindow w;
    w.showMaximized();

    return app.exec();
}
