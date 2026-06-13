#pragma once

#include <QImage>
#include "Params.h"

/**
 * BlendCompositor
 *
 * Composites a layer onto a base image using Photoshop-style blend
 * modes. Modes natively supported by QPainter use its optimized
 * composition paths; the remaining ones are computed per pixel
 * following the PDF / Photoshop blend specification.
 *
 * `base` and `layer` must be the same size. `base` is modified
 * in place and kept in Format_ARGB32_Premultiplied.
 */
namespace BlendCompositor {

void compositeOver(QImage& base, const QImage& layer, BlendMode mode);

} // namespace BlendCompositor
