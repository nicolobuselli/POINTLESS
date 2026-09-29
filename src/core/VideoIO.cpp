#include "VideoIO.h"
#include "FrameStore.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QElapsedTimer>

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

} // namespace

QString VideoIO::ffmpegPath()
{
    static const QString p = findFfmpeg();
    return p;
}

bool VideoIO::decode(const QString& videoPath, QVector<QImage>& outFrames,
                     double& outFps, QString& err, int maxFrames, std::atomic_bool* cancel, qint64 maxBytes)
{
    if (ffmpegPath().isEmpty()) { err = "ffmpeg not found"; return false; }

    outFrames.clear();
    err.clear();
    FrameStore decoded;
    bool failed = false;
    // Let ffmpeg write raw BGRA directly into FrameStore's temporary backing
    // file. Routing multi-gigabyte clips through QProcess/QByteArray first made
    // the application copy every byte and could leave a spurious partial-buffer
    // state even after ffmpeg had successfully written every frame.
    const QString cachePath = decoded.prepareExternalWrite(err);
    if (cachePath.isEmpty()) return false;
    QStringList args;
    args << "-hide_banner" << "-y" << "-i" << videoPath
         << "-map" << "0:v:0" << "-vsync" << "0";
    if (maxFrames > 0) args << "-frames:v" << QString::number(maxFrames + 1);
    args << "-f" << "rawvideo" << "-pix_fmt" << "bgra" << cachePath;

    QProcess p;
    p.setReadChannel(QProcess::StandardError);
    p.start(ffmpegPath(), args);
    if (!p.waitForStarted(5000)) { err = "ffmpeg failed to start"; return false; }

    QString log;
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
        if (frameBytes <= 0 || frameBytes > maxBytes || w > 32768 || h > 32768) { err = "Video frame exceeds the import memory budget."; failed = true; }
    };

    QElapsedTimer idle; idle.start();
    while (p.state() == QProcess::Running) {
        if (cancel && cancel->load()) { err = "Import canceled."; failed = true; break; }
        if (!p.waitForReadyRead(100)) {
            if (p.state() == QProcess::Running && idle.elapsed() > 30000) {
                err = "Video decoder timed out."; failed = true; break;
            }
            continue;
        }
        idle.restart();
        log += QString::fromLocal8Bit(p.readAllStandardError());
        if (frameBytes <= 0) readGeometry();
        if (failed) break;
    }
    if (failed) { p.kill(); p.waitForFinished(5000); return false; }
    if (p.state() != QProcess::NotRunning && !p.waitForFinished(5000)) { p.kill(); p.waitForFinished(); err = "Decoder did not finish."; return false; }
    log += QString::fromLocal8Bit(p.readAllStandardError());
    if (frameBytes <= 0) readGeometry();

    outFps = 24.0;
    static const QRegularExpression fpsRe(QStringLiteral("([0-9]+(?:\\.[0-9]+)?) fps"));
    const auto m = fpsRe.match(log);
    if (m.hasMatch()) {
        const double f = m.captured(1).toDouble();
        if (f > 0.0 && f <= 240.0) outFps = f;
    }

    if (failed || frameBytes <= 0 || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (!failed) err = "Video decoding failed.\n\n" + log.right(600);
        return false;
    }
    outFrames = decoded.finishExternal(QSize(w, h), err);
    if (maxFrames > 0 && outFrames.size() > maxFrames) {
        outFrames.clear();
        err = "Video exceeds the requested frame count. Nothing was imported.";
        return false;
    }
    return !outFrames.isEmpty();
}

bool VideoIO::encodeFrames(QSize size, double fps, int count,
    const std::function<QImage(int)>& frameAt, const QString& path,
    QString& error, std::atomic_bool& cancel, std::atomic_int& progress)
{
    if (size.isEmpty() || count < 1 || ffmpegPath().isEmpty()) { error = "Invalid video settings or missing ffmpeg."; return false; }
    QTemporaryFile temporary(path + ".XXXXXX.mp4");
    if (!temporary.open()) { error = temporary.errorString(); return false; }
    const QString staging = temporary.fileName(); temporary.close();
    QProcess encoder;
    encoder.start(ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgba", "-s", QString("%1x%2").arg(size.width()).arg(size.height()),
        "-framerate", QString::number(fps, 'f', 6), "-i", "pipe:0", "-an",
        "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", "-f", "mp4", staging});
    if (!encoder.waitForStarted(5000)) { error = encoder.errorString(); return false; }
    auto stop = [&] { encoder.kill(); encoder.waitForFinished(5000); return false; };
    for (int i = 0; i < count; ++i) {
        if (cancel.load()) { error = "Export canceled."; return stop(); }
        QImage image = frameAt(i).convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull() || image.size() != size) { error = "Could not render a video frame."; return stop(); }
        const char* bytes = reinterpret_cast<const char*>(image.constBits());
        qint64 remaining = image.sizeInBytes();
        QElapsedTimer stalled; stalled.start();
        while (remaining > 0 || encoder.bytesToWrite() > 0) {
            if (cancel.load()) { error = "Export canceled."; return stop(); }
            if (encoder.state() == QProcess::NotRunning) { error = QString::fromUtf8(encoder.readAllStandardError()); return false; }
            if (remaining > 0 && encoder.bytesToWrite() < 1024 * 1024) {
                const qint64 n = encoder.write(bytes, qMin(remaining, qint64(1024 * 1024)));
                if (n < 0) { error = encoder.errorString(); return stop(); }
                bytes += n; remaining -= n;
            }
            if (encoder.waitForBytesWritten(100)) stalled.restart();
            if (stalled.elapsed() > 30000) { error = "Encoder stopped accepting frames."; return stop(); }
        }
        progress.store(i + 1);
    }
    encoder.closeWriteChannel();
    QElapsedTimer finish; finish.start();
    while (encoder.state() != QProcess::NotRunning) {
        encoder.waitForFinished(100);
        if (cancel.load()) { error = "Export canceled."; return stop(); }
        if (finish.elapsed() > 120000) { error = "Encoder timed out."; return stop(); }
    }
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0) {
        error = QString::fromUtf8(encoder.readAllStandardError()); return false;
    }
    QFile input(staging); QSaveFile output(path);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) { error = "Could not finalize video output."; return false; }
    while (!input.atEnd()) {
        if (cancel.load()) { error = "Export canceled."; return false; }
        const auto chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) { error = "Could not write video output."; return false; }
    }
    if (!output.commit()) { error = output.errorString(); return false; }
    return true;
}
