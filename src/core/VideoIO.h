#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

// ============================================================
//  VideoIO — mp4 import/export by driving a bundled ffmpeg.exe
//  as a subprocess (Qt Multimedia cannot encode a frame sequence).
//  Decode: video → frames (QImage). Encode: bounded raw-frame pipe → mp4.
// ============================================================

namespace VideoIO {

// Absolute path to ffmpeg, or empty if not found (looked up next to the
// application first, then on the system PATH).
QString ffmpegPath();
inline bool available() { return !ffmpegPath().isEmpty(); }

// Decode a video into frames at native resolution. fps is read from the
// stream (falls back to 24). Frames are capped to maxFrames. Returns false
// and fills err on failure. Pixels live in a temporary disk mapping retained by
// the QImages; maxBytes limits a single frame, not the duration of the clip.
bool decode(const QString& videoPath, QVector<QImage>& outFrames,
            double& outFps, QString& err, int maxFrames = 0,
            std::atomic_bool* cancel = nullptr, qint64 maxBytes = 512LL * 1024 * 1024);

// Bounded raw-frame pipe; output replaces the destination only after success.
bool encodeFrames(QSize size, double fps, int count,
                  const std::function<QImage(int)>& frameAt, const QString& path,
                  QString& error, std::atomic_bool& cancel, std::atomic_int& progress);

} // namespace VideoIO
