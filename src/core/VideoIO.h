#pragma once

#include <QImage>
#include <QString>
#include <QVector>

// ============================================================
//  VideoIO — mp4 import/export by driving a bundled ffmpeg.exe
//  as a subprocess (Qt Multimedia cannot encode a frame sequence).
//  Decode: video → frames (QImage). Encode: numbered PNGs → mp4.
// ============================================================

namespace VideoIO {

// Absolute path to ffmpeg, or empty if not found (looked up next to the
// application first, then on the system PATH).
QString ffmpegPath();
inline bool available() { return !ffmpegPath().isEmpty(); }

// Decode a video into frames at native resolution. fps is read from the
// stream (falls back to 24). Frames are capped to maxFrames. Returns false
// and fills err on failure.
bool decode(const QString& videoPath, QVector<QImage>& outFrames,
            double& outFps, QString& err, int maxFrames = 1200);

// Encode numbered PNG frames (e.g. pattern "f_%06d.png") from dir into an
// H.264 mp4 (video only). Returns false and fills err on failure.
bool encodePngDir(const QString& dir, const QString& pattern, double fps,
                  const QString& outPath, QString& err);

} // namespace VideoIO
