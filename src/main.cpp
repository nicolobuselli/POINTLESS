#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QFont>
#include <QScreen>
#include <QRegularExpression>
#include "ui/MainWindow.h"
#include "ui/UiScale.h"

// Replace s(N) tokens in the stylesheet with round(N * scale) px values, so
// every dimension scales with the design reference width.
static QString scaleStyleSheet(QString css)
{
    static const QRegularExpression re(R"(s\((-?\d+(?:\.\d+)?)\))");
    QString out;
    out.reserve(css.size());
    int last = 0;
    auto it = re.globalMatch(css);
    while (it.hasNext()) {
        const auto m = it.next();
        out += css.mid(last, m.capturedStart() - last);
        out += QString::number(Ui::px(m.captured(1).toDouble()));
        last = m.capturedEnd();
    }
    out += css.mid(last);
    return out;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ULTRA_Ditherer");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("ULTRA_Ditherer");

    // Global UI scale: design is drawn at Ui::kDesignWidth; match it to the
    // screen the window will fill. A small bump (kUiBump) makes type a touch
    // larger than the 1:1 Figma mapping, per design review.
    constexpr double kUiBump = 1.28;   // nudged up one more px step (design review)
    if (QScreen* s = app.primaryScreen())
        Ui::setScale(kUiBump * s->availableGeometry().width() / Ui::kDesignWidth);

    // Bundle the Funnel Display font (variable: Light→ExtraBold) so the UI
    // renders identically on any machine, font installed or not.
    QFontDatabase::addApplicationFont(":/fonts/FunnelDisplay.ttf");
    {
        QFont base("Funnel Display");
        base.setStyleStrategy(QFont::PreferAntialias);
        app.setFont(base);
    }

    // Load + scale stylesheet
    QFile qss(":/style.qss");
    if (qss.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(scaleStyleSheet(QString::fromUtf8(qss.readAll())));
    }

    MainWindow w;
    w.showMaximized();

    return app.exec();
}
