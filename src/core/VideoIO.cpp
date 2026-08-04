#include "VideoIO.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

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

    // Raw BGRA straight down ffmpeg's stdout: no PNG round-trip through a temp
    // folder (that used to cost a zlib compress + write + read + decompress per
    // frame, which dwarfed the actual video decoding).
    QStringList args;
    args << "-hide_banner" << "-i" << videoPath << "-vsync" << "0";
    if (maxFrames > 0) args << "-frames:v" << QString::number(maxFrames);
    args << "-f" << "rawvideo" << "-pix_fmt" << "bgra" << "-";

    QProcess p;
    p.start(ffmpegPath(), args);
    if (!p.waitForStarted(5000)) { err = "ffmpeg failed to start"; return false; }

    QString log;
    QByteArray buf;
    int w = 0, h = 0;
    qsizetype frameBytes = 0;

    // Frame size comes from ffmpeg's *output* stream header (not the input one):
    // it already accounts for rotation metadata, so phone clips are not garbled.
    auto readGeometry = [&] {
        const int out = log.indexOf(QStringLiteral("Output #0"));
        if (out < 0) return;
        static const QRegularExpression re(QStringLiteral("Video:.*?, (\\d+)x(\\d+)"));
        const auto m = re.match(log, out);
        if (!m.hasMatch()) return;
        w = m.captured(1).toInt();
        h = m.captured(2).toInt();
        frameBytes = qsizetype(w) * h * 4;
    };

    auto sliceFrames = [&] {
        if (frameBytes <= 0) return;
        while (buf.size() >= frameBytes) {
            const QImage view(reinterpret_cast<const uchar*>(buf.constData()),
                              w, h, w * 4, QImage::Format_RGB32);
            outFrames.append(view.copy());   // view aliases buf, which we reuse
            buf.remove(0, frameBytes);
        }
    };

    while (p.state() == QProcess::Running) {
        if (!p.waitForReadyRead(30000)) break;
        log += QString::fromLocal8Bit(p.readAllStandardError());
        buf += p.readAllStandardOutput();
        if (frameBytes <= 0) readGeometry();
        sliceFrames();
    }
    p.waitForFinished(5000);
    log += QString::fromLocal8Bit(p.readAllStandardError());
    buf += p.readAllStandardOutput();
    if (frameBytes <= 0) readGeometry();
    sliceFrames();

    outFps = 24.0;
    static const QRegularExpression fpsRe(QStringLiteral("([0-9]+(?:\\.[0-9]+)?) fps"));
    const auto m = fpsRe.match(log);
    if (m.hasMatch()) {
        const double f = m.captured(1).toDouble();
        if (f > 0.0 && f <= 240.0) outFps = f;
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
