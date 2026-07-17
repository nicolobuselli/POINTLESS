#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFont>
#include <QScreen>
#include <QTimer>
#include <QRegularExpression>
#include <QHash>
#include "ui/MainWindow.h"
#include "ui/UiScale.h"

#ifdef Q_OS_WIN
#include <QSettings>
#include <shlobj.h>

// Registers ULTRATOOL as the handler for .ultra files under the CURRENT
// USER's registry hive (HKCU — no admin rights needed), pointing at
// wherever this exe currently is, and gives the file type this exe's own
// icon. Idempotent: re-checks every launch, only writes (and pokes the
// shell to refresh) when something's actually out of date — e.g. the exe
// was moved since the last run.
static void registerFileAssociation()
{
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString wantedCmd  = "\"" + exePath + "\" \"%1\"";
    const QString wantedIcon = "\"" + exePath + "\",0";
    const QString kProgId = "ULTRATOOL.Project";

    QSettings ext("HKEY_CURRENT_USER\\Software\\Classes\\.ultra", QSettings::NativeFormat);
    QSettings progid("HKEY_CURRENT_USER\\Software\\Classes\\" + kProgId, QSettings::NativeFormat);
    QSettings icon("HKEY_CURRENT_USER\\Software\\Classes\\" + kProgId + "\\DefaultIcon", QSettings::NativeFormat);
    QSettings cmd("HKEY_CURRENT_USER\\Software\\Classes\\" + kProgId + "\\shell\\open\\command", QSettings::NativeFormat);

    if (ext.value(".").toString() == kProgId
     && icon.value(".").toString() == wantedIcon
     && cmd.value(".").toString() == wantedCmd)
        return;   // already correct — skip the writes and the shell refresh

    ext.setValue(".", kProgId);
    progid.setValue(".", "ULTRATOOL Project");
    icon.setValue(".", wantedIcon);
    cmd.setValue(".", wantedCmd);
    for (QSettings* s : { &ext, &progid, &icon, &cmd }) s->sync();

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif

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
    app.setApplicationName("ULTRATOOL");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("ULTRATOOL");

#ifdef Q_OS_WIN
    registerFileAssociation();
#endif

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

    // Double-clicking a .ultra file (once registerFileAssociation() has run
    // at least once) launches us with its path as argv[1]. Deferred to the
    // next event-loop tick: called this early, the window hasn't been shown/
    // laid out yet, so widgets like the library grid still report a stale
    // (pre-layout) size and size themselves wrong.
    const QStringList args = app.arguments();
    if (args.size() > 1 && args[1].endsWith(".ultra", Qt::CaseInsensitive)) {
        const QString startupPath = args[1];
        QTimer::singleShot(150, &w, [&w, startupPath]() { w.openProjectFromPath(startupPath); });
    }

    return app.exec();
}
