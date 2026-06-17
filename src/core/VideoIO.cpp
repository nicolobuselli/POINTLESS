#include "VideoIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

QString findFfmpeg()
{
    // 1. Bundled next to the application (the shipping case).
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& cand : { appDir + "/ffmpeg.exe",
                                 appDir + "/ffmpeg/ffmpeg.exe" })
        if (QFileInfo::exists(cand)) return cand;

    // 2. On the system PATH.
    return QStandardPaths::findExecutable("ffmpeg");
}

bool runFfmpeg(const QStringList& args, QString& log, int timeoutMs = -1)
{
    const QString ff = VideoIO::ffmpegPath();
    if (ff.isEmpty()) { log = "ffmpeg not found"; return false; }

    QProcess p;
    p.start(ff, args);
    if (!p.waitForStarted(5000)) { log = "ffmpeg failed to start"; return false; }
    p.waitForFinished(timeoutMs);
    log = QString::fromLocal8Bit(p.readAllStandardError());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

} // namespace

QString VideoIO::ffmpegPath()
{
    static const QString p = findFfmpeg();
    return p;
}

bool VideoIO::decode(const QString& videoPath, QVector<QImage>& outFrames,
                     double& outFps, QString& err, int maxFrames)
{
    if (ffmpegPath().isEmpty()) { err = "ffmpeg not found"; return false; }

    QTemporaryDir tmp;
    if (!tmp.isValid()) { err = "could not create a temporary folder"; return false; }

    const QString pattern = tmp.path() + "/f_%06d.png";
    QStringList args;
    args << "-hide_banner" << "-i" << videoPath << "-vsync" << "0";
    if (maxFrames > 0) args << "-frames:v" << QString::number(maxFrames);
    args << pattern;

    QString log;
    // ffmpeg returns non-zero when capped with -frames:v on some builds, so we
    // judge success by whether frames were actually produced, not the exit code.
    runFfmpeg(args, log, -1);

    outFps = 24.0;
    const QRegularExpression re(QStringLiteral("([0-9]+(?:\\.[0-9]+)?) fps"));
    const auto m = re.match(log);
    if (m.hasMatch()) {
        const double f = m.captured(1).toDouble();
        if (f > 0.0 && f <= 240.0) outFps = f;
    }

    QDir d(tmp.path());
    const QStringList files = d.entryList({ "f_*.png" }, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        QImage im(d.filePath(f));
        if (!im.isNull()) outFrames.append(im.convertToFormat(QImage::Format_RGB32));
    }
    if (outFrames.isEmpty()) {
        err = "No frames were decoded.\n\n" + log.right(600);
        return false;
    }
    return true;
}

bool VideoIO::encodePngDir(const QString& dir, const QString& pattern, double fps,
                           const QString& outPath, QString& err)
{
    if (ffmpegPath().isEmpty()) { err = "ffmpeg not found"; return false; }
    if (fps <= 0.0) fps = 24.0;

    QStringList args;
    args << "-hide_banner" << "-y"
         << "-framerate" << QString::number(fps, 'f', 3)
         << "-i" << (dir + "/" + pattern)
         // H.264 + yuv420p for broad compatibility; force even dimensions.
         << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2"
         << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p"
         << "-movflags" << "+faststart"
         << outPath;

    QString log;
    if (!runFfmpeg(args, log, -1)) {
        err = "ffmpeg failed to encode the video.\n\n" + log.right(600);
        return false;
    }
    return true;
}
