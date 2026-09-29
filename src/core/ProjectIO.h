#pragma once

#include "Params.h"
#include "Animation.h"
#include <QString>
#include <QHash>
#include <QImage>
#include <atomic>

// ============================================================
//  ProjectIO — save/load one composition to a self-contained
//  .less JSON file (frame, layers, parents, animation, and the
//  image/video library, embedded as PNG frames with source frame rate.
//  Version 2 adds video; version 1 still-image projects remain readable.
// ============================================================
namespace ProjectIO {

struct MediaEntry {
    QString name;
    QImage  image;
    QVector<QImage> frames;
    double fps = 24.0;
};

struct ProjectData {
    QString                title;
    SessionParams          params;
    Animation               anim;
    QHash<int, MediaEntry>  media;   // key = mediaId
};

bool save(const QString& path, const ProjectData& data, QString* error = nullptr, std::atomic_bool* cancel = nullptr);
bool load(const QString& path, ProjectData* out, QString* error = nullptr, std::atomic_bool* cancel = nullptr);

} // namespace ProjectIO
