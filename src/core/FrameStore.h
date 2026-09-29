#pragma once

#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTemporaryFile>
#include <QDir>
#include <limits>
#include <memory>

// Immutable video pixels backed by a temporary file. QImage copies retain the
// mapping; edits detach into ordinary RAM. The last image releases the file.
class FrameStore {
    struct Registry { QMutex mutex; QSet<qint64> keys; };
    static Registry& registry() { static Registry value; return value; }
    struct Backing {
        QTemporaryFile file{QDir::tempPath() + "/pointless-frames-XXXXXX"};
        uchar* mapping = nullptr;
        ~Backing() { if (mapping) file.unmap(mapping); }
    };
    struct Lease { std::shared_ptr<Backing> backing; qint64 key = 0; };
    static void release(void* pointer) {
        auto* lease = static_cast<Lease*>(pointer);
        { auto& r = registry(); QMutexLocker lock(&r.mutex); r.keys.remove(lease->key); }
        delete lease;
    }
    std::shared_ptr<Backing> m_backing = std::make_shared<Backing>();
    QSize m_size;
    int m_count = 0;
public:
    static qint64 ownedBytes(const QImage& image) {
        auto& r = registry(); QMutexLocker lock(&r.mutex);
        return r.keys.contains(image.cacheKey()) ? 0 : image.sizeInBytes();
    }
    int count() const { return m_count; }
    QString prepareExternalWrite(QString& error) {
        if (m_count || m_backing->mapping || m_backing->file.isOpen()) {
            error = "Video cache is already in use."; return {};
        }
        if (!m_backing->file.open()) {
            error = "Could not create video cache: " + m_backing->file.errorString(); return {};
        }
        const QString path = m_backing->file.fileName();
        m_backing->file.close();
        return path;
    }
    bool append(const char* bytes, QSize size, QString& error) {
        const qint64 length = qint64(size.width()) * size.height() * 4;
        if (size.isEmpty() || length > 512LL * 1024 * 1024 ||
            (m_count && size != m_size) || m_backing->mapping) {
            error = "Invalid video frame size."; return false;
        }
        if (!m_backing->file.isOpen() && !m_backing->file.open()) {
            error = "Could not create video cache: " + m_backing->file.errorString(); return false;
        }
        if (m_backing->file.write(bytes, length) != length) {
            error = "Could not write video cache. Check free disk space.\n" + m_backing->file.errorString(); return false;
        }
        m_size = size; ++m_count; return true;
    }
    bool append(const QImage& image, QString& error) {
        const QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
        if (rgba.isNull()) { error = "Could not decode a video frame."; return false; }
        return append(reinterpret_cast<const char*>(rgba.constBits()), rgba.size(), error);
    }
    QVector<QImage> finish(QString& error) {
        if (!m_count) return {};
        return mapFrames(error);
    }
    QVector<QImage> finishExternal(QSize size, QString& error) {
        if (size.isEmpty() || m_backing->mapping) {
            error = "Invalid video frame size."; return {};
        }
        if (!m_backing->file.open()) {
            error = "Could not open video cache: " + m_backing->file.errorString(); return {};
        }
        const qint64 stride = qint64(size.width()) * size.height() * 4;
        const qint64 bytes = m_backing->file.size();
        if (stride <= 0 || stride > 512LL * 1024 * 1024 || bytes <= 0 || bytes % stride) {
            error = "Video decoder returned an incomplete frame."; return {};
        }
        const qint64 count = bytes / stride;
        if (count > std::numeric_limits<int>::max()) {
            error = "Video contains too many frames."; return {};
        }
        m_size = size;
        m_count = int(count);
        return mapFrames(error);
    }
private:
    QVector<QImage> mapFrames(QString& error) {
        if (!m_backing->file.flush() ||
            !(m_backing->mapping = m_backing->file.map(0, m_backing->file.size()))) {
            error = "Could not map video cache: " + m_backing->file.errorString(); return {};
        }
        QVector<QImage> result; result.reserve(m_count);
        const qint64 stride = qint64(m_size.width()) * m_size.height() * 4;
        for (int i = 0; i < m_count; ++i) {
            auto* lease = new Lease{m_backing};
            QImage image(static_cast<const uchar*>(m_backing->mapping) + i * stride,
                         m_size.width(), m_size.height(), m_size.width() * 4,
                         QImage::Format_ARGB32, release, lease);
            lease->key = image.cacheKey();
            { auto& r = registry(); QMutexLocker lock(&r.mutex); r.keys.insert(lease->key); }
            result.append(image);
        }
        return result;
    }
};
