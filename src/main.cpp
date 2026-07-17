#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QFont>
#include <QScreen>
#include <QRegularExpression>
#include <QHash>
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

// The design-system palette: every color the stylesheet uses, named once.
// style.qss references these as @tokenName — change a color here and every
// rule that uses it follows, instead of hunting hex codes across the file.
static QString substitutePaletteTokens(QString css)
{
    static const QHash<QString, QString> kPalette = {
        // Base surfaces
        { "bgWindow",    "#1E1E1E" },
        { "bgPanel",     "#272727" },
        { "surface2",    "#3B3B3B" },   // flat dark chrome: title bar, hairlines, checked tab, menus, popups
        { "surface3",    "#313131" },   // palette item / trash-idle bg
        { "popupBorder", "#5D5D5D" },   // borders on floating popups/menus/panels (not the box system below)
        { "vlineColor",  "#161616" },

        // Text
        { "textBody",  "#E3E3E3" },
        { "textTitle", "#EEEEEE" },
        { "textLabel", "#B2B2B2" },
        { "textValue", "#A6A6A6" },
        { "textDim",   "#8E8E8E" },

        // Unified box system (every dropdown / number / text / svg box)
        { "boxFill",        "transparent" },
        { "boxStroke",      "#3D3D3D" },
        { "boxStrokeHover", "#616161" },
        { "boxStrokeActive", "#45556C" },  // border while editing a number / dropdown open / pressed

        // Special boxes (own colors instead of the default box system)
        { "resetStroke",      "#864141" },
        { "resetStrokeHover", "#AA6565" },
        { "activeLayerFill",  "#434343" },
        { "modeFill",         "#46556C" },
        { "modeStroke",       "#2F3C50" },
        { "modeStrokeHover",  "#536074" },

        // Accent (orange CTA)
        { "accent",      "#FD5A1F" },
        { "accentHover", "#FD6B35" },
        { "accentPress", "#E04E16" },

        // Selection blue
        { "selBlue",      "#568AD9" },
        { "selBlueHover", "#5E92E0" },

        // Danger red
        { "danger",      "#FD231F" },
        { "dangerHover", "#FF3A36" },

        // Timeline auto-key orange — kept distinct from the accent orange
        { "keyOrange", "#FF6A00" },

        // Scrollbar
        { "scrollHandle",      "#8E8E8E" },
        { "scrollHandleHover", "#A6A6A6" },
    };
    static const QRegularExpression re(R"(@([A-Za-z][A-Za-z0-9]*))");
    QString out;
    out.reserve(css.size());
    int last = 0;
    auto it = re.globalMatch(css);
    while (it.hasNext()) {
        const auto m = it.next();
        out += css.mid(last, m.capturedStart() - last);
        out += kPalette.value(m.captured(1), m.captured(0));
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
        app.setStyleSheet(scaleStyleSheet(substitutePaletteTokens(QString::fromUtf8(qss.readAll()))));
    }

    MainWindow w;
    w.showMaximized();

    return app.exec();
}
