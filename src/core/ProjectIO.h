#pragma once

#include "Params.h"
#include "Animation.h"
#include <QString>
#include <QHash>
#include <QImage>

// ============================================================
//  ProjectIO — save/load one composition to a self-contained
//  .ultra JSON file (frame, layers, parents, animation, and the
//  still-image library, each image embedded as base64 PNG).
//
//  Video clips aren't supported: callers should strip any layer,
//  parent group, and animation track that depends on a video
//  media entry before calling save() (see MainWindow::saveProject),
//  and media itself only ever holds stills.
// ============================================================
namespace ProjectIO {

struct MediaEntry {
    QString name;
    QImage  image;
};

struct ProjectData {
    QString                title;
    SessionParams          params;
    Animation               anim;
    QHash<int, MediaEntry>  media;   // key = mediaId
};

bool save(const QString& path, const ProjectData& data, QString* error = nullptr);
bool load(const QString& path, ProjectData* out, QString* error = nullptr);

} // namespace ProjectIO
