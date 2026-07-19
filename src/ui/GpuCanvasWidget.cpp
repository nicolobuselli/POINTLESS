#include "GpuCanvasWidget.h"
#include "../core/AsciiRenderer.h"
#include "../core/DitherRenderer.h"
#include "../core/DotGridRenderer.h"
#include "../core/GridGenerator.h"

#include <rhi/qrhi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMatrix4x4>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Fullscreen quad: x, y, u, v (two triangles). V=0 at the top-left so QImage
// row 0 lands at uv y 0; the clip-space correction matrix in each vertex
// shader normalizes backend Y conventions.
const float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,

     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,
};

// std140 layout of composite.vert/frag's `buf` (see the shaders).
struct CompUbo {
    float mvp[16];
    float invPlacement[16];
    float sizes[4];
    qint32 modeFlags[4];   // x = BlendMode, y = gpuAdjust
    float adjA[4];         // brightness, contrast, gamma, saturation
    float adjB[4];         // levelsBlack, levelsMid, levelsWhite, grainAmp
    float adjC[4];         // posterize, threshold, invert, unused
};

QShader loadShader(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll())
                                       : QShader();
}

const char* backendName(QRhi::Implementation impl)
{
    switch (impl) {
        case QRhi::D3D11:  return "D3D11";
        case QRhi::D3D12:  return "D3D12";
        case QRhi::OpenGLES2: return "OpenGL";
        case QRhi::Vulkan: return "Vulkan";
        case QRhi::Metal:  return "Metal";
        case QRhi::Null:   return "Null";
    }
    return "Unknown";
}

void writeMat(float* dst, const QMatrix4x4& m)
{
    std::memcpy(dst, m.constData(), 16 * sizeof(float));
}

// std140 mirror of dot.vert's `buf`: 16 mvp floats + parameter block.
// Float offsets: 16 dims · 20 p0 · 24 p1 · 28 p2 · 32 shapeIds[8] ·
// 40 toneLevel[8] · 48 toneColor[8×4] · 80 locFieldC[4×4] · 96 locScale ·
// 100 locOn · 104 maskPts[5×4] · 124 grid0 · 128 grid1 · 132 gridT ·
// 136 gridD → 140 floats.
constexpr quint32 kDotUboFloats = 140;
constexpr quint32 kDotUboBytes  = kDotUboFloats * sizeof(float);

// Returns the instance count to draw (GridGpuLayout::count).
int fillDotParams(float* u, const GpuLayer& l)
{
    const DotGridSettings& d = l.dotSettings;
    const float w  = float(l.contentSize.width());
    const float h  = float(l.contentSize.height());
    const float sp = qMax(2.0f, d.grid.spacing);

    u[16] = w; u[17] = h; u[18] = sp;
    u[19] = (sp * 0.5f) * qMax(0.01f, d.grid.diameter);          // baseR
    u[20] = d.gamma; u[21] = d.weight; u[22] = d.jitter; u[23] = d.opacity;

    const bool imageColors = (d.tonal.mode == ToneMode::ImageColors);
    const int  nShapes = int(qMin<size_t>(d.shapes.size(), 8));
    const int  nTones  = imageColors ? 0 : int(qMin<size_t>(d.tonal.tones.size(), 8));
    u[24] = d.cornerRadius;
    u[25] = float(d.multiThreshold - 128);
    u[26] = float(qMax(1, nShapes));
    u[27] = float(nTones);
    u[28] = imageColors ? 1.0f : 0.0f;

    for (int i = 0; i < nShapes; ++i)
        u[32 + i] = DotGridRenderer::gpuShapeId(d.shapes[i].shape);
    for (int i = 0; i < nTones; ++i) {
        const ToneEntry& te = d.tonal.tones[i];
        u[40 + i] = float(te.level);
        u[48 + i * 4 + 0] = float(te.color.redF());
        u[48 + i * 4 + 1] = float(te.color.greenF());
        u[48 + i * 4 + 2] = float(te.color.blueF());
        u[48 + i * 4 + 3] = qBound(0.0f, te.opacity, 1.0f);
    }

    const LocField lf[4] = {
        locField(d.loc, LocParam::DgDiameter, w, h),
        locField(d.loc, LocParam::DgGamma,    w, h),
        locField(d.loc, LocParam::DgWeight,   w, h),
        locField(d.loc, LocParam::DgJitter,   w, h),
    };
    for (int i = 0; i < 4; ++i) {
        u[80 + i * 4 + 0] = lf[i].cx;
        u[80 + i * 4 + 1] = lf[i].cy;
        u[80 + i * 4 + 2] = lf[i].rIn;
        u[80 + i * 4 + 3] = lf[i].rOut;
        u[96 + i]  = lf[i].scale;
        u[100 + i] = lf[i].on ? 1.0f : 0.0f;
    }
    const LocMask lm = locMask(d.loc, w, h);
    const int nMask = int(qMin<size_t>(lm.pts.size(), 5));
    u[29] = float(nMask);
    for (int i = 0; i < nMask; ++i) {
        u[104 + i * 4 + 0] = lm.pts[i].cx;
        u[104 + i * 4 + 1] = lm.pts[i].cy;
        u[104 + i * 4 + 2] = lm.pts[i].rIn;
        u[104 + i * 4 + 3] = lm.pts[i].rOut;
    }

    // Grid layout: dot.vert rebuilds every sample position from
    // gl_InstanceIndex with these (GridGenerator::computeGpuLayout).
    const GridGpuLayout gl = GridGenerator::computeGpuLayout(
        d.grid, l.contentSize.width(), l.contentSize.height());
    float margin = sp;                                    // GridGenerator per-type margin
    if (d.grid.type == GridType::Wave)        margin = sp * 1.9f;   // sp + amp
    if (d.grid.type == GridType::Phyllotaxis) margin = sp * 0.8f;   // seed scale c
    u[124] = float(gl.type); u[125] = float(gl.i0);
    u[126] = float(gl.j0);   u[127] = float(gl.cols);
    u[128] = float(gl.ringN);
    u[129] = std::log2(sp);                               // mip lod ≈ cell box average
    u[130] = margin;
    u[132] = gl.m11; u[133] = gl.m12; u[134] = gl.m21; u[135] = gl.m22;
    u[136] = gl.dx;  u[137] = gl.dy;  u[138] = w * 0.5f; u[139] = h * 0.5f;
    return gl.count;
}

// std140 mirror of halftone.vert/.frag's `buf`: 16 mvp floats + 16 dims/pA/
// pB/pC + 4 paper + 32 scrA + 32 scrB + 32 inks = 132 floats. Screen setup
// mirrors HalftoneRenderer::render's job construction (KEEP IN SYNC).
constexpr quint32 kHalfUboFloats = 132;
constexpr quint32 kHalfUboBytes  = kHalfUboFloats * sizeof(float);

void fillHalftoneParams(float* u, const GpuLayer& l)
{
    const HalftoneSettings& p = l.halftoneSettings;
    const float sp = qMax(2.0f, p.spacing);
    u[16] = float(l.contentSize.width());
    u[17] = float(l.contentSize.height());
    u[18] = sp;
    u[19] = std::log2(sp);                     // mip lod ≈ cell box average
    u[20] = p.gamma; u[21] = p.softness; u[22] = p.gridNoise; u[23] = p.grain;
    u[24] = p.opacity;
    u[26] = float(int(p.dotShape));

    const QColor paper = p.paper.isValid() ? p.paper : QColor(Qt::white);
    u[32] = float(paper.redF()); u[33] = float(paper.greenF());
    u[34] = float(paper.blueF()); u[35] = 1.0f;

    const bool cmyk = (p.tonal.mode == ToneMode::ImageColors) || p.tonal.tones.empty();
    u[27] = cmyk ? 1.0f : 0.0f;
    int n = 0;
    if (cmyk) {
        n = 4;
        const float  angles[4] = { p.angleC, p.angleM, p.angleY, p.angleK };
        const QColor inks[4]   = { p.inkC, p.inkM, p.inkY, p.inkK };
        const float  floods[4] = { p.floodC, p.floodM, p.floodY, p.floodK };
        const float  gains[4]  = { p.gainC, p.gainM, p.gainY, p.gainK };
        for (int i = 0; i < 4; ++i) {
            u[36 + i * 4 + 0] = angles[i];
            u[36 + i * 4 + 1] = qBound(-1.0f, floods[i], 1.0f);
            u[36 + i * 4 + 2] = qBound(-1.0f, gains[i], 1.0f);
            u[100 + i * 4 + 0] = float(inks[i].redF());
            u[100 + i * 4 + 1] = float(inks[i].greenF());
            u[100 + i * 4 + 2] = float(inks[i].blueF());
            u[100 + i * 4 + 3] = float(inks[i].alphaF());
        }
    } else {
        std::vector<ToneEntry> tones = p.tonal.tones;
        std::sort(tones.begin(), tones.end(),
                  [](const ToneEntry& a, const ToneEntry& b) { return a.level < b.level; });
        n = int(qMin<size_t>(tones.size(), 8));
        const float angles[4] = { p.angleK, p.angleC, p.angleM, p.angleY };
        for (int i = 0; i < n; ++i) {
            u[36 + i * 4 + 0] = (i < 4) ? angles[i]
                              : std::fmod(p.angleK + float(i) * 37.5f, 180.0f);
            u[36 + i * 4 + 3] = float(tones[size_t(i)].level);
            u[68 + i * 4 + 0] = (i > 0)     ? float(tones[size_t(i) - 1].level) : -1.0f;
            u[68 + i * 4 + 1] = (i < n - 1) ? float(tones[size_t(i) + 1].level) : 256.0f;
            const QColor c = tones[size_t(i)].color;
            u[100 + i * 4 + 0] = float(c.redF());
            u[100 + i * 4 + 1] = float(c.greenF());
            u[100 + i * 4 + 2] = float(c.blueF());
            u[100 + i * 4 + 3] = qBound(0.0f, tones[size_t(i)].opacity, 1.0f);
        }
        u[28] = (n == 1) ? 1.0f : 0.0f;        // single tone: classic screen
    }
    u[25] = float(n);
}

// std140 mirror of dither.vert/.frag's `buf`: 16 mvp + 20 scalars + 256
// toneColor + 64 toneLevel + 20 locFieldC + 20 locSO + 20 maskPts = 416
// floats (KEEP IN SYNC with dither.frag AND DitherRenderer::renderOrdered/
// renderThreshold).
constexpr quint32 kDitherUboFloats = 416;
constexpr quint32 kDitherUboBytes  = kDitherUboFloats * sizeof(float);

void fillDitherParams(float* u, const GpuLayer& l, int maskW, int maskH)
{
    const DitherSettings& s = l.ditherSettings;
    const float cw = float(l.contentSize.width());
    const float ch = float(l.contentSize.height());
    const int   ps = qBound(1, s.pixelSize, 100);
    // CPU cell grid: work = input.scaled(size/ps) → floor division, min 1.
    const float cellsW = qMax(1.0f, std::floor(cw / float(ps)));
    const float cellsH = qMax(1.0f, std::floor(ch / float(ps)));

    u[16] = cw; u[17] = ch; u[18] = cellsW; u[19] = cellsH;
    u[20] = std::log2(qMax(1.0f, cw / cellsW));           // mip lod ≈ cell average
    u[21] = qBound(0, s.strength,  100) / 100.0f;
    u[22] = qBound(0, s.threshold, 100) / 100.0f;
    u[23] = qBound(0.0f, s.opacity, 1.0f);

    const bool thresholdAlg = (s.algorithm == DitherAlgorithm::Threshold);
    const bool lineHatch    = (s.algorithm == DitherAlgorithm::LineHatch);
    u[24] = thresholdAlg ? 2.0f : (lineHatch ? 1.0f : 0.0f);
    const bool imageColors = (s.tonal.mode == ToneMode::ImageColors);
    u[25] = imageColors ? 1.0f : 0.0f;
    u[27] = float(qBound(2, s.levels, 16));

    u[28] = s.lineAngle;
    u[29] = float(qBound(2, s.lineSpacing, 64));
    u[30] = float(qMax(1, maskW));
    u[31] = float(qMax(1, maskH));

    int nTones = 0;
    if (!imageColors) {
        const std::vector<ToneEntry> tones = DitherRenderer::gpuTones(s);
        nTones = int(qMin<size_t>(tones.size(), 64));
        for (int i = 0; i < nTones; ++i) {
            const ToneEntry& te = tones[size_t(i)];
            u[36 + i * 4 + 0] = float(te.color.redF());
            u[36 + i * 4 + 1] = float(te.color.greenF());
            u[36 + i * 4 + 2] = float(te.color.blueF());
            u[36 + i * 4 + 3] = qBound(0.0f, te.opacity, 1.0f);
            u[292 + i] = float(te.level);
        }
        if (nTones == 0) {                       // no tones → black ink
            u[36] = u[37] = u[38] = 0.0f; u[39] = 1.0f;
        }
    }
    u[26] = float(nTones);

    // Loc fields + spotlight mask in CELL coordinates (the CPU's work res).
    const LocField lf[5] = {
        locField(s.loc, LocParam::DiStrength,    cellsW, cellsH),
        locField(s.loc, LocParam::DiThreshold,   cellsW, cellsH),
        locField(s.loc, LocParam::DiLevels,      cellsW, cellsH),
        locField(s.loc, LocParam::DiLineAngle,   cellsW, cellsH),
        locField(s.loc, LocParam::DiLineSpacing, cellsW, cellsH),
    };
    for (int i = 0; i < 5; ++i) {
        u[356 + i * 4 + 0] = lf[i].cx;
        u[356 + i * 4 + 1] = lf[i].cy;
        u[356 + i * 4 + 2] = lf[i].rIn;
        u[356 + i * 4 + 3] = lf[i].rOut;
        u[376 + i * 4 + 0] = lf[i].scale;
        u[376 + i * 4 + 1] = lf[i].on ? 1.0f : 0.0f;
    }
    const LocMask lm = locMask(s.loc, cellsW, cellsH);
    const int nMask = int(qMin<size_t>(lm.pts.size(), 5));
    u[32] = float(nMask);
    for (int i = 0; i < nMask; ++i) {
        u[396 + i * 4 + 0] = lm.pts[size_t(i)].cx;
        u[396 + i * 4 + 1] = lm.pts[size_t(i)].cy;
        u[396 + i * 4 + 2] = lm.pts[size_t(i)].rIn;
        u[396 + i * 4 + 3] = lm.pts[size_t(i)].rOut;
    }
}

// std140 mirror of ascii_grid.vert/.frag's `buf`: 16 mvp + 4 dims + 4 p0 +
// 4 p1 + 4 p2 + 4 p3 + 128 coverage + 128 sortedIdx + 8 toneLevel + 32
// toneColor + 8 locFieldC + 8 locSO + 20 maskPts + 16 grid = 384 floats
// (KEEP IN SYNC with ascii_grid.vert AND AsciiRenderer::render's non-square
// branch). Returns the instance count (GridGpuLayout).
constexpr quint32 kAsciiGridUboFloats = 384;
constexpr quint32 kAsciiGridUboBytes  = kAsciiGridUboFloats * sizeof(float);

int fillAsciiGridParams(float* u, const GpuLayer& l, const AsciiGpuAtlas& atlas)
{
    const AsciiSettings& s = l.asciiSettings;
    const float w = float(l.contentSize.width());
    const float h = float(l.contentSize.height());
    const float cellW = float(qMax(1, atlas.cellW));
    const float cellH = float(qMax(1, atlas.cellH));
    // CPU: gs.spacing = float(cellH) — the lattice pitch for non-square grids.
    const float sp = qMax(2.0f, cellH);

    u[16] = w; u[17] = h; u[18] = sp;
    u[19] = std::log2(qMax(1.0f, std::sqrt(cellW * cellH)));   // lod
    u[20] = cellW; u[21] = cellH;
    u[22] = qMax(0.01f, s.gamma);
    u[23] = qBound(0.0f, s.opacity, 1.0f);

    const bool imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const int  nTones = imageColors ? 0 : int(qMin<size_t>(s.tonal.tones.size(), 8));
    u[24] = float(atlas.nChars);
    u[25] = s.orderedDither ? 1.0f : 0.0f;
    u[26] = imageColors ? 1.0f : 0.0f;
    u[27] = float(nTones);

    u[29] = float(qMax(1, atlas.atlasCols));
    u[30] = float(atlas.glyphW); u[31] = float(atlas.glyphH);
    u[32] = float(atlas.image.width()); u[33] = float(atlas.image.height());
    u[34] = float(atlas.spaceIndex);
    u[35] = float(qBound(0, s.stipple, 100));

    const int nCov = qMin(atlas.nChars, 128);
    for (int i = 0; i < nCov; ++i) {
        u[36  + i] = atlas.coverage[size_t(i)];
        u[164 + i] = float(atlas.sortedIdx[size_t(i)]);
    }
    for (int i = 0; i < nTones; ++i) {
        const ToneEntry& te = s.tonal.tones[size_t(i)];
        u[292 + i] = float(te.level);
        u[300 + i * 4 + 0] = float(te.color.redF());
        u[300 + i * 4 + 1] = float(te.color.greenF());
        u[300 + i * 4 + 2] = float(te.color.blueF());
        u[300 + i * 4 + 3] = qBound(0.0f, te.opacity, 1.0f);
    }

    const LocField lf[2] = {
        locField(s.loc, LocParam::AsGamma,   w, h),
        locField(s.loc, LocParam::AsStipple, w, h),
    };
    for (int i = 0; i < 2; ++i) {
        u[332 + i * 4 + 0] = lf[i].cx;
        u[332 + i * 4 + 1] = lf[i].cy;
        u[332 + i * 4 + 2] = lf[i].rIn;
        u[332 + i * 4 + 3] = lf[i].rOut;
        u[340 + i * 4 + 0] = lf[i].scale;
        u[340 + i * 4 + 1] = lf[i].on ? 1.0f : 0.0f;
    }
    const LocMask lm = locMask(s.loc, w, h);
    const int nMask = int(qMin<size_t>(lm.pts.size(), 5));
    u[28] = float(nMask);
    for (int i = 0; i < nMask; ++i) {
        u[348 + i * 4 + 0] = lm.pts[size_t(i)].cx;
        u[348 + i * 4 + 1] = lm.pts[size_t(i)].cy;
        u[348 + i * 4 + 2] = lm.pts[size_t(i)].rIn;
        u[348 + i * 4 + 3] = lm.pts[size_t(i)].rOut;
    }

    GridSettings gs;
    gs.type    = s.gridShape;
    gs.spacing = sp;
    const GridGpuLayout gl = GridGenerator::computeGpuLayout(
        gs, l.contentSize.width(), l.contentSize.height());
    float margin = sp;
    if (gs.type == GridType::Wave)        margin = sp * 1.9f;
    if (gs.type == GridType::Phyllotaxis) margin = sp * 0.8f;
    u[368] = float(gl.type); u[369] = float(gl.i0);
    u[370] = float(gl.j0);   u[371] = float(gl.cols);
    u[372] = float(gl.ringN);
    u[373] = margin;
    u[376] = gl.m11; u[377] = gl.m12; u[378] = gl.m21; u[379] = gl.m22;
    u[380] = gl.dx;  u[381] = gl.dy;  u[382] = w * 0.5f; u[383] = h * 0.5f;
    return gl.count;
}

// std140 mirror of mosaic.vert's `buf`: 16 mvp + 16 dims/p0/p1/p2 + 8
// toneLevel + 32 toneColor + 12 locFieldC + 4 locScale + 4 locOn + 20
// maskPts + 16 grid = 128 floats (KEEP IN SYNC with mosaic.vert AND
// MosaicRenderer::render). Returns the instance count (GridGpuLayout).
constexpr quint32 kMosUboFloats = 128;
constexpr quint32 kMosUboBytes  = kMosUboFloats * sizeof(float);

int fillMosaicParams(float* u, const GpuLayer& l)
{
    const MosaicSettings& m = l.mosaicSettings;
    const float w  = float(l.contentSize.width());
    const float h  = float(l.contentSize.height());
    const float sp = qMax(2.0f, m.spacing);
    const float cellW = qMax(2.0f, m.cellW());
    const float cellH = qMax(2.0f, m.cellH());

    u[16] = w; u[17] = h; u[18] = sp;
    u[19] = std::log2(qMax(1.0f, std::sqrt(cellW * cellH)));   // lod ≈ rect average
    u[20] = float(qMax(1, qRound(cellW)));
    u[21] = float(qMax(1, qRound(cellH)));
    u[22] = m.gridRotation * 3.14159265358979f / 180.0f;
    u[23] = qBound(0.0f, m.opacity, 1.0f);

    const bool imageColors = (m.tonal.mode == ToneMode::ImageColors);
    const int  nTones = imageColors ? 0 : int(qMin<size_t>(m.tonal.tones.size(), 8));
    u[24] = qBound(0.0f, m.cornerRadius, 100.0f) / 100.0f;
    u[25] = imageColors ? 1.0f : 0.0f;
    u[26] = float(nTones);
    const bool inkPaper = (!imageColors && m.tonal.tones.size() == 1);
    u[27] = inkPaper
        ? (qBound(0, m.tonal.tones[0].level, 255) + 255.0f) * 0.5f / 255.0f
        : 2.0f;

    for (int i = 0; i < nTones; ++i) {
        const ToneEntry& te = m.tonal.tones[size_t(i)];
        u[32 + i] = float(te.level);
        u[40 + i * 4 + 0] = float(te.color.redF());
        u[40 + i * 4 + 1] = float(te.color.greenF());
        u[40 + i * 4 + 2] = float(te.color.blueF());
        u[40 + i * 4 + 3] = qBound(0.0f, te.opacity, 1.0f);
    }

    const LocField lf[3] = {
        locField(m.loc, LocParam::MsSpacing,   w, h),
        locField(m.loc, LocParam::MsWidthPct,  w, h),
        locField(m.loc, LocParam::MsHeightPct, w, h),
    };
    for (int i = 0; i < 3; ++i) {
        u[72 + i * 4 + 0] = lf[i].cx;
        u[72 + i * 4 + 1] = lf[i].cy;
        u[72 + i * 4 + 2] = lf[i].rIn;
        u[72 + i * 4 + 3] = lf[i].rOut;
        u[84 + i] = lf[i].scale;
        u[88 + i] = lf[i].on ? 1.0f : 0.0f;
    }
    const LocMask lm = locMask(m.loc, w, h);
    const int nMask = int(qMin<size_t>(lm.pts.size(), 5));
    u[28] = float(nMask);
    for (int i = 0; i < nMask; ++i) {
        u[92 + i * 4 + 0] = lm.pts[size_t(i)].cx;
        u[92 + i * 4 + 1] = lm.pts[size_t(i)].cy;
        u[92 + i * 4 + 2] = lm.pts[size_t(i)].rIn;
        u[92 + i * 4 + 3] = lm.pts[size_t(i)].rOut;
    }

    GridSettings gs;
    gs.type     = m.gridShape;
    gs.spacing  = sp;
    gs.rotation = m.gridRotation;
    const GridGpuLayout gl = GridGenerator::computeGpuLayout(
        gs, l.contentSize.width(), l.contentSize.height());
    float margin = sp;                                    // GridGenerator per-type margin
    if (gs.type == GridType::Wave)        margin = sp * 1.9f;
    if (gs.type == GridType::Phyllotaxis) margin = sp * 0.8f;
    u[112] = float(gl.type); u[113] = float(gl.i0);
    u[114] = float(gl.j0);   u[115] = float(gl.cols);
    u[116] = float(gl.ringN);
    u[117] = margin;
    u[120] = gl.m11; u[121] = gl.m12; u[122] = gl.m21; u[123] = gl.m22;
    u[124] = gl.dx;  u[125] = gl.dy;  u[126] = w * 0.5f; u[127] = h * 0.5f;
    return gl.count;
}

// Threshold-matrix identity — re-upload only when this changes.
quint64 ditherMaskKey(const DitherSettings& s)
{
    return (quint64(quint32(qHash(s.patternPath))) << 16)
         | quint64(quint32(int(s.algorithm)) << 8)
         | quint64(quint32(s.bayerSize) & 0xFFu);
}

// std140 mirror of ascii.vert/.frag's `buf`: 16 mvp + 28 scalars + 128
// coverage + 128 sortedIdx + 8 toneLevel + 32 toneColor + 20 locFieldC +
// 20 locSO + 20 maskPts = 396 floats (KEEP IN SYNC with ascii.frag AND
// AsciiRenderer::render's square-grid loop). Loc field order: 0 gamma,
// 1 edges, 2 hatching, 3 stipple, 4 contour.
constexpr quint32 kAsciiUboFloats = 396;
constexpr quint32 kAsciiUboBytes  = kAsciiUboFloats * sizeof(float);

quint64 asciiAtlasKey(const AsciiSettings& s)
{
    return (quint64(quint32(qHash(s.fontFamily))) << 32)
         ^ (quint64(quint32(qHash(s.effectiveCharset()))) << 8)
         ^  quint64(quint32(s.fontWeight * 131 + s.cellSize));
}

void fillAsciiParams(float* u, const GpuLayer& l, const AsciiGpuAtlas& atlas)
{
    const AsciiSettings& s = l.asciiSettings;
    const float w = float(l.contentSize.width());
    const float h = float(l.contentSize.height());
    const float cellW = float(qMax(1, atlas.cellW));
    const float cellH = float(qMax(1, atlas.cellH));

    u[16] = w; u[17] = h; u[18] = cellW; u[19] = cellH;
    u[20] = std::ceil(w / cellW); u[21] = std::ceil(h / cellH);
    u[22] = std::log2(qMax(1.0f, std::sqrt(cellW * cellH)));
    u[23] = qBound(0.0f, s.opacity, 1.0f);

    u[24] = float(atlas.glyphW); u[25] = float(atlas.glyphH);
    u[26] = float(qMax(1, atlas.atlasCols)); u[27] = float(atlas.image.width());

    const bool imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const int  nTones = imageColors ? 0 : int(qMin<size_t>(s.tonal.tones.size(), 8));
    u[28] = float(atlas.nChars);
    u[29] = qMax(0.01f, s.gamma);
    u[30] = s.orderedDither ? 1.0f : 0.0f;
    u[31] = imageColors ? 1.0f : 0.0f;
    u[32] = float(qBound(0, s.stipple, 100));
    u[33] = float(atlas.image.height());
    u[34] = float(nTones);
    // pC: edges/hatching/contour (0..100). maskCount lives in pB.w (u[35]).
    u[36] = float(qBound(0, s.edges,    100));
    u[37] = float(qBound(0, s.hatching, 100));
    u[38] = float(qBound(0, s.contour,  100));

    const int nCov = qMin(atlas.nChars, 128);
    for (int i = 0; i < nCov; ++i) {
        u[40  + i] = atlas.coverage[size_t(i)];
        u[168 + i] = float(atlas.sortedIdx[size_t(i)]);
    }

    for (int i = 0; i < nTones; ++i) {
        const ToneEntry& te = s.tonal.tones[size_t(i)];
        u[296 + i] = float(te.level);
        u[304 + i * 4 + 0] = float(te.color.redF());
        u[304 + i * 4 + 1] = float(te.color.greenF());
        u[304 + i * 4 + 2] = float(te.color.blueF());
        u[304 + i * 4 + 3] = qBound(0.0f, te.opacity, 1.0f);
    }

    const LocField lf[5] = {
        locField(s.loc, LocParam::AsGamma,    w, h),
        locField(s.loc, LocParam::AsEdges,    w, h),
        locField(s.loc, LocParam::AsHatching, w, h),
        locField(s.loc, LocParam::AsStipple,  w, h),
        locField(s.loc, LocParam::AsContour,  w, h),
    };
    for (int i = 0; i < 5; ++i) {
        u[336 + i * 4 + 0] = lf[i].cx;
        u[336 + i * 4 + 1] = lf[i].cy;
        u[336 + i * 4 + 2] = lf[i].rIn;
        u[336 + i * 4 + 3] = lf[i].rOut;
        u[356 + i * 4 + 0] = lf[i].scale;
        u[356 + i * 4 + 1] = lf[i].on ? 1.0f : 0.0f;
    }
    const LocMask lm = locMask(s.loc, w, h);
    const int nMask = int(qMin<size_t>(lm.pts.size(), 5));
    u[35] = float(nMask);
    for (int i = 0; i < nMask; ++i) {
        u[376 + i * 4 + 0] = lm.pts[size_t(i)].cx;
        u[376 + i * 4 + 1] = lm.pts[size_t(i)].cy;
        u[376 + i * 4 + 2] = lm.pts[size_t(i)].rIn;
        u[376 + i * 4 + 3] = lm.pts[size_t(i)].rOut;
    }
}

// std140 mirror of adjust.vert/.frag's `buf` (KEEP IN SYNC).
struct AdjUbo {
    float  mvp[16];
    float  sizes[4];      // outW, outH, texAW, texAH
    qint32 opFlags[4];    // x = op, y = blur radius, z = finish flags
    float  adjA[4];       // brightness, contrast, gamma (actual), saturation 0..200
    float  adjB[4];       // levelsBlack, levelsMid (actual), levelsWhite, grainAmp 0..1
    float  adjC[4];       // posterize, threshold, invert, amount (edge/unsharp)
};

// Fill the shared adjustment values (ops read what they need per pass).
void fillAdjustValues(AdjUbo& u, const Adjustments& a)
{
    u.adjA[0] = float(a.brightness);
    u.adjA[1] = float(a.contrast);
    u.adjA[2] = float(a.gamma) / 100.0f;
    u.adjA[3] = float(qBound(0, 100 + a.saturation, 200));
    u.adjB[0] = float(a.levelsBlack);
    u.adjB[1] = float(a.levelsMid) / 100.0f;
    u.adjB[2] = float(a.levelsWhite);
    u.adjB[3] = float(a.grain) * 1.6f / 255.0f;
    u.adjC[0] = float(a.posterize);
    u.adjC[1] = float(a.threshold);
    u.adjC[2] = a.invert ? 1.0f : 0.0f;
    u.adjC[3] = 0.0f;
}

// Pass plan for one layer's adjust chain — mirrors ImageAdjuster::apply's op
// order (KEEP IN SYNC): [resize+]point ops → blur → edge → sharpen → finish
// (grain/posterize/threshold). Ping indices rotate through 3 targets so each
// pass reads from the previous one; -1 = raw source, -2 = out target.
std::vector<GpuCanvasWidget::AdjPass> planAdjustPasses(const Adjustments& a)
{
    using AdjPass = GpuCanvasWidget::AdjPass;
    std::vector<AdjPass> ps;
    int cur = 0;
    ps.push_back({ 0, 0, 0.0f, -1, -1, 0 });          // point ops (+ implicit resize)
    if (a.blur > 0) {                                  // ImageAdjuster step 7
        // ponytail: passes count crosses a structural threshold at blur==
        // 35/70 (rebuilds the SRB set that one frame — a momentary hitch,
        // measured cheaper than doubling the steady-state cost by padding
        // to a fixed ceiling every frame, which is what regressed here).
        const int r      = qMax(1, qRound(a.blur * 0.35f));
        const int passes = 2 + a.blur / 35;
        for (int i = 0; i < passes; ++i) {
            const int h = (cur + 1) % 3, v = (cur + 2) % 3;
            ps.push_back({ 1, r, 0.0f, cur, -1, h });
            ps.push_back({ 2, r, 0.0f, h,   -1, v });
            cur = v;
        }
    }
    if (a.edgeEnhancement > 0) {                       // step 8
        const int d = (cur + 1) % 3;
        ps.push_back({ 3, 0, a.edgeEnhancement / 100.0f * 0.30f, cur, -1, d });
        cur = d;
    }
    if (a.sharpenStrength > 0) {                       // step 9 (unsharp mask)
        const int h = (cur + 1) % 3, d = (cur + 2) % 3;
        const int r = qMax(1, a.sharpenRadius);
        ps.push_back({ 1, r, 0.0f, cur, -1, h });
        ps.push_back({ 4, r, a.sharpenStrength / 100.0f * 1.5f, h, cur, d });
        cur = d;
    }
    ps.push_back({ 5, 0, 0.0f, cur, -1, -2 });         // grain/posterize/threshold + finish
    return ps;
}

// (layer id, raster size) → texture-cache key. Id lives alone in the high
// 32 bits so cleanup can recover it; w/h are well under 16 bits each.
quint64 layerTexKey(int id, const QSize& s)
{
    return (quint64(quint32(id)) << 32)
         | (quint64(quint32(s.width()) & 0xFFFFu) << 16)
         |  quint64(quint32(s.height()) & 0xFFFFu);
}

} // namespace

GpuCanvasWidget::GpuCanvasWidget(QWidget* parent)
    : QRhiWidget(parent)
{
}

GpuCanvasWidget::~GpuCanvasWidget() = default;

void GpuCanvasWidget::showImage(const QImage& img)
{
    m_image = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    m_imageDirty = true;
    m_packageMode = false;
    update();
}

void GpuCanvasWidget::showPackage(const GpuFramePackage& pkg)
{
    m_pkg = pkg;
    m_packageMode = pkg.valid && !pkg.frame.isEmpty();
    m_pkgDirty = true;
    update();
}

void GpuCanvasWidget::setViewRect(const QRectF& rectWidgetCoords)
{
    if (m_viewRect == rectWidgetCoords) return;
    m_viewRect = rectWidgetCoords;
    update();
}

void GpuCanvasWidget::initialize(QRhiCommandBuffer* cb)
{
    Q_UNUSED(cb);
    if (m_rhi != rhi()) {
        // New QRhi (first init or backend restart): drop every resource.
        m_blitPipeline.reset();
        m_compPipeline.reset();
        m_dotPipeline.reset();
        m_dotRes.clear();
        m_halfPipeline.reset();
        m_halfRes.clear();
        m_ditherPipeline.reset();
        m_ditherRes.clear();
        m_mosPipeline.reset();
        m_mosRes.clear();
        m_asciiPipeline.reset();
        m_asciiRes.clear();
        m_asciiGridPipeline.reset();
        m_asciiGridRes.clear();
        m_adjPipeF16.reset();
        m_adjPipe8.reset();
        m_adjRes.clear();
        m_adjRpF16.reset();
        m_adjRp8.reset();
        m_mipSampler.reset();
        m_dotRp.reset();
        m_blitSrb.reset();
        m_compSrbs.clear();
        m_compUbufs.clear();
        m_accumRt[0].reset(); m_accumRt[1].reset();
        m_accumRp.reset();
        m_accum[0].reset();  m_accum[1].reset();
        m_accumSize = {};
        m_layerTex.clear();
        m_imageTex.reset();
        m_vbuf.reset();
        m_blitUbuf.reset();
        m_dummyTex.reset();
        m_sampler.reset();
        m_rhi = rhi();
        m_vbufDirty = true;
        m_imageDirty = !m_image.isNull();
        m_frameTex.clear();
        m_srbSig.clear();
        m_blitSrbTex = nullptr;
    }

    if (!m_initialized && m_rhi) {
        QFile log(QDir(QCoreApplication::applicationDirPath()).filePath("gpu_spike.log"));
        if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
            log.write("QRhi backend: ");
            log.write(backendName(m_rhi->backend()));
            log.write("\ndriver: ");
            log.write(m_rhi->driverInfo().deviceName.constData());
            log.write("\n");
        }
        m_initialized = true;
    }

    if (m_blitPipeline)
        return;

    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
    m_vbuf->create();

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();

    // Mip-aware sampler for the halftone source (textureLod cell averages).
    m_mipSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                         QRhiSampler::Linear,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_mipSampler->create();

    m_blitUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    m_blitUbuf->create();

    // 1×1 placeholder so layout-only SRBs never hold a null texture.
    m_dummyTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_dummyTex->create();

    // The blit SRB is (re)built in render() once the present texture exists.
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 4 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_blitPipeline.reset(m_rhi->newGraphicsPipeline());
    m_blitPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/blit.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/blit.frag.qsb")) },
    });
    m_blitPipeline->setVertexInputLayout(inputLayout);
    // Placeholder SRB purely for the pipeline's binding LAYOUT; the real SRB
    // (with live textures) is swapped in per frame — layouts match. Must stay
    // alive through pipeline->create().
    std::unique_ptr<QRhiShaderResourceBindings> layoutSrb(m_rhi->newShaderResourceBindings());
    layoutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, m_blitUbuf.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, m_dummyTex.get(), m_sampler.get()),
    });
    layoutSrb->create();
    m_blitPipeline->setShaderResourceBindings(layoutSrb.get());
    m_blitPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_blitPipeline->create();
}

void GpuCanvasWidget::ensureAccumTargets(const QSize& size)
{
    if (m_accumSize == size && m_accum[0])
        return;

    m_compPipeline.reset();
    m_accumRt[0].reset(); m_accumRt[1].reset();
    m_accumRp.reset();

    for (int i = 0; i < 2; ++i) {
        m_accum[i].reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                           QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        m_accum[i]->create();
        m_accumRt[i].reset(m_rhi->newTextureRenderTarget({ m_accum[i].get() }));
    }
    m_accumRp.reset(m_accumRt[0]->newCompatibleRenderPassDescriptor());
    for (int i = 0; i < 2; ++i) {
        m_accumRt[i]->setRenderPassDescriptor(m_accumRp.get());
        m_accumRt[i]->create();
    }
    m_accumSize = size;

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 4 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_compPipeline.reset(m_rhi->newGraphicsPipeline());
    m_compPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/composite.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/composite.frag.qsb")) },
    });
    m_compPipeline->setVertexInputLayout(inputLayout);
    if (m_compUbufs.empty()) {
        auto* b = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CompUbo));
        b->create();
        m_compUbufs.emplace_back(b);
    }
    std::unique_ptr<QRhiShaderResourceBindings> layoutSrb(m_rhi->newShaderResourceBindings());
    layoutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_compUbufs[0].get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, m_dummyTex.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage, m_dummyTex.get(), m_sampler.get()),
    });
    layoutSrb->create();
    m_compPipeline->setShaderResourceBindings(layoutSrb.get());
    m_compPipeline->setRenderPassDescriptor(m_accumRp.get());
    m_compPipeline->create();
}

// Phase 4: per-layer adjust chain. Uploads the raw source (once per
// cacheKey), sizes the work/out targets to l.contentSize, plans the passes
// and writes their UBOs. Returns the chain's output texture — linear fp16
// mipmapped (f16Out, dot/halftone source) or premultiplied RGBA8 (composite
// raster). render() executes `plan` before anything samples the output.
QRhiTexture* GpuCanvasWidget::ensureAdjustRes(const GpuLayer& l, bool f16Out,
                                              QRhiResourceUpdateBatch* rub)
{
    AdjRes& ar = m_adjRes[l.id];
    const QSize srcSize = l.image.size();
    const QSize outSize = l.contentSize.isEmpty() ? srcSize : l.contentSize;
    bool texDirty = false;

    if (!ar.rawTex || ar.srcSize != srcSize) {
        ar.rawTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, srcSize, 1,
                                          QRhiTexture::MipMapped
                                        | QRhiTexture::UsedWithGenerateMips));
        ar.rawTex->create();
        ar.rawKey  = -1;
        ar.srcSize = srcSize;
        texDirty   = true;
    }
    if (ar.rawKey != l.image.cacheKey()) {
        const QImage img = (l.image.format() == QImage::Format_RGBA8888)
                         ? l.image
                         : l.image.convertToFormat(QImage::Format_RGBA8888);
        rub->uploadTexture(ar.rawTex.get(), img);
        rub->generateMips(ar.rawTex.get());   // mips = smooth sizePct downscale
        ar.rawKey = l.image.cacheKey();
    }

    std::vector<AdjPass> plan = planAdjustPasses(l.adj);
    int nPing = 0;
    for (const AdjPass& p : plan)
        nPing = qMax(nPing, qMax(p.src, qMax(p.aux, p.dst)) + 1);

    for (int i = 0; i < nPing; ++i) {
        if (ar.ping[i] && ar.ping[i]->pixelSize() == outSize) continue;
        ar.pingRt[i].reset();
        ar.ping[i].reset(m_rhi->newTexture(QRhiTexture::RGBA16F, outSize, 1,
                                           QRhiTexture::RenderTarget));
        ar.ping[i]->create();
        ar.pingRt[i].reset(m_rhi->newTextureRenderTarget({ ar.ping[i].get() }));
        if (!m_adjRpF16)
            m_adjRpF16.reset(ar.pingRt[i]->newCompatibleRenderPassDescriptor());
        ar.pingRt[i]->setRenderPassDescriptor(m_adjRpF16.get());
        ar.pingRt[i]->create();
        texDirty = true;
    }

    if (!ar.outTex || ar.outTex->pixelSize() != outSize || ar.f16Out != f16Out) {
        ar.outRt.reset();
        ar.outTex.reset(f16Out
            ? m_rhi->newTexture(QRhiTexture::RGBA16F, outSize, 1,
                                QRhiTexture::RenderTarget | QRhiTexture::MipMapped
                              | QRhiTexture::UsedWithGenerateMips)
            : m_rhi->newTexture(QRhiTexture::RGBA8, outSize, 1,
                                QRhiTexture::RenderTarget));
        ar.outTex->create();
        ar.outRt.reset(m_rhi->newTextureRenderTarget({ ar.outTex.get() }));
        if (f16Out) {
            if (!m_adjRpF16)
                m_adjRpF16.reset(ar.outRt->newCompatibleRenderPassDescriptor());
            ar.outRt->setRenderPassDescriptor(m_adjRpF16.get());
        } else {
            if (!m_adjRp8)
                m_adjRp8.reset(ar.outRt->newCompatibleRenderPassDescriptor());
            ar.outRt->setRenderPassDescriptor(m_adjRp8.get());
        }
        ar.outRt->create();
        ar.f16Out  = f16Out;
        ar.outSize = outSize;
        texDirty   = true;
    }

    while (ar.ubos.size() < plan.size()) {
        auto* b = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                   sizeof(AdjUbo));
        b->create();
        ar.ubos.emplace_back(b);
    }

    std::vector<int> sig;
    sig.reserve(plan.size() * 4);
    for (const AdjPass& p : plan) {
        sig.push_back(p.op); sig.push_back(p.src);
        sig.push_back(p.aux); sig.push_back(p.dst);
    }
    if (texDirty || sig != ar.planSig || ar.srbs.size() != plan.size()) {
        ar.srbs.clear();
        for (size_t i = 0; i < plan.size(); ++i) {
            const AdjPass& p = plan[i];
            QRhiTexture* texA = (p.src < 0) ? ar.rawTex.get() : ar.ping[p.src].get();
            QRhiSampler* smpA = (p.src < 0) ? m_mipSampler.get() : m_sampler.get();
            QRhiTexture* texB = (p.aux >= 0) ? ar.ping[p.aux].get() : m_dummyTex.get();
            auto* srb = m_rhi->newShaderResourceBindings();
            srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage
                     | QRhiShaderResourceBinding::FragmentStage, ar.ubos[i].get()),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage, texA, smpA),
                QRhiShaderResourceBinding::sampledTexture(
                    2, QRhiShaderResourceBinding::FragmentStage, texB, m_sampler.get()),
            });
            srb->create();
            ar.srbs.emplace_back(srb);
        }
        ar.planSig = std::move(sig);
    }

    for (size_t i = 0; i < plan.size(); ++i) {
        const AdjPass& p = plan[i];
        AdjUbo u{};
        writeMat(u.mvp, m_rhi->clipSpaceCorrMatrix());
        const QSize aSize = (p.src < 0) ? srcSize : outSize;
        u.sizes[0] = float(outSize.width());
        u.sizes[1] = float(outSize.height());
        u.sizes[2] = float(aSize.width());
        u.sizes[3] = float(aSize.height());
        u.opFlags[0] = p.op;
        u.opFlags[1] = p.radius;
        u.opFlags[2] = (p.dst == -2) ? (f16Out ? 1 : 2) : 0;
        u.opFlags[3] = 0;
        fillAdjustValues(u, l.adj);
        if (p.op == 3 || p.op == 4) u.adjC[3] = p.amount;
        rub->updateDynamicBuffer(ar.ubos[i].get(), 0, sizeof(AdjUbo), &u);
    }
    ar.plan = std::move(plan);
    return ar.outTex.get();
}

void GpuCanvasWidget::ensureLayerTextures(QRhiResourceUpdateBatch* rub)
{
    // Upload changed/new layer rasters; textures live per (id, size) so the
    // fast and full-res variants of a layer coexist. Conversion to the upload
    // format only happens when an upload is actually due — the worker already
    // delivers RGBA8888_Premultiplied on the package path, so this is rare.
    QSet<int> aliveIds;
    m_frameTex.clear();
    for (const GpuLayer& l : m_pkg.layers) {
        aliveIds.insert(l.id);

        if (l.halftoneScreen) {
            // Uniform-driven halftone: the adjust chain's fp16 linear output
            // (with mips) in, content texture out. Param drags — adjustments
            // included — only rewrite UBOs.
            QRhiTexture* srcTex = ensureAdjustRes(l, true, rub);
            HalfRes& hr = m_halfRes[l.id];
            if (!hr.tex || hr.size != l.contentSize) {
                hr.rt.reset();
                hr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                hr.tex->create();
                hr.rt.reset(m_rhi->newTextureRenderTarget({ hr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(hr.rt->newCompatibleRenderPassDescriptor());
                hr.rt->setRenderPassDescriptor(m_dotRp.get());
                hr.rt->create();
                hr.size = l.contentSize;
            }
            bool srbDirty = (hr.srcBound != srcTex);
            if (!hr.ubo) {
                hr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kHalfUboBytes));
                hr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !hr.srb) {
                hr.srb.reset(m_rhi->newShaderResourceBindings());
                hr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage
                         | QRhiShaderResourceBinding::FragmentStage, hr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage,
                        srcTex, m_mipSampler.get()),
                });
                hr.srb->create();
                hr.srcBound = srcTex;
            }
            float ubo[kHalfUboFloats] = {};
            writeMat(ubo, m_rhi->clipSpaceCorrMatrix());
            fillHalftoneParams(ubo, l);
            rub->updateDynamicBuffer(hr.ubo.get(), 0, kHalfUboBytes, ubo);
            m_frameTex.push_back(hr.tex.get());
            continue;
        }

        if (l.dotScreen) {
            // Uniform-driven Dot Grid: the adjust chain's fp16 linear output
            // (with mips) in, content texture out. dot.vert reconstructs the
            // lattice from gl_InstanceIndex, so EVERY control drag —
            // adjustments included — only rewrites UBOs.
            QRhiTexture* srcTex = ensureAdjustRes(l, true, rub);
            DotRes& dr = m_dotRes[l.id];
            if (!dr.tex || dr.size != l.contentSize) {
                dr.rt.reset();
                dr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                dr.tex->create();
                dr.rt.reset(m_rhi->newTextureRenderTarget({ dr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(dr.rt->newCompatibleRenderPassDescriptor());
                dr.rt->setRenderPassDescriptor(m_dotRp.get());
                dr.rt->create();
                dr.size = l.contentSize;
            }
            bool srbDirty = (dr.srcBound != srcTex);
            if (!dr.ubo) {
                dr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kDotUboBytes));
                dr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !dr.srb) {
                dr.srb.reset(m_rhi->newShaderResourceBindings());
                dr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage, dr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::VertexStage,
                        srcTex, m_mipSampler.get()),
                });
                dr.srb->create();
                dr.srcBound = srcTex;
            }
            QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
            mvp.ortho(0.0f, float(l.contentSize.width()),
                      float(l.contentSize.height()), 0.0f, -1.0f, 1.0f);
            float ubo[kDotUboFloats] = {};
            writeMat(ubo, mvp);
            dr.dotCount = fillDotParams(ubo, l);
            rub->updateDynamicBuffer(dr.ubo.get(), 0, kDotUboBytes, ubo);
            m_frameTex.push_back(dr.tex.get());
            continue;
        }

        if (l.asciiInstanced) {
            // Non-square ASCII: instanced glyph billboards, same resource
            // shape as Dot Grid/Mosaic but with an extra atlas texture bound
            // in the fragment stage. Every slider = UBO rewrite only.
            QRhiTexture* srcTex = ensureAdjustRes(l, true, rub);
            const AsciiGpuAtlas& atlas = AsciiRenderer::gpuAtlas(l.asciiSettings);
            AsciiGridRes& gr = m_asciiGridRes[l.id];
            if (!gr.tex || gr.size != l.contentSize) {
                gr.rt.reset();
                gr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                gr.tex->create();
                gr.rt.reset(m_rhi->newTextureRenderTarget({ gr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(gr.rt->newCompatibleRenderPassDescriptor());
                gr.rt->setRenderPassDescriptor(m_dotRp.get());
                gr.rt->create();
                gr.size = l.contentSize;
            }
            bool srbDirty = (gr.srcBound != srcTex);
            const quint64 ak = asciiAtlasKey(l.asciiSettings);
            if (!gr.atlasTex || gr.atlasKey != ak
                || gr.atlasTex->pixelSize() != atlas.image.size()) {
                if (!gr.atlasTex || gr.atlasTex->pixelSize() != atlas.image.size()) {
                    gr.atlasTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, atlas.image.size()));
                    gr.atlasTex->create();
                    srbDirty = true;
                }
                rub->uploadTexture(gr.atlasTex.get(), atlas.image);
                gr.atlasKey = ak;
            }
            if (!gr.ubo) {
                gr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kAsciiGridUboBytes));
                gr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !gr.srb) {
                gr.srb.reset(m_rhi->newShaderResourceBindings());
                gr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage
                         | QRhiShaderResourceBinding::FragmentStage, gr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::VertexStage,
                        srcTex, m_mipSampler.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        2, QRhiShaderResourceBinding::FragmentStage,
                        gr.atlasTex.get(), m_sampler.get()),
                });
                gr.srb->create();
                gr.srcBound = srcTex;
            }
            QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
            mvp.ortho(0.0f, float(l.contentSize.width()),
                      float(l.contentSize.height()), 0.0f, -1.0f, 1.0f);
            float ubo[kAsciiGridUboFloats] = {};
            writeMat(ubo, mvp);
            gr.count = fillAsciiGridParams(ubo, l, atlas);
            rub->updateDynamicBuffer(gr.ubo.get(), 0, kAsciiGridUboBytes, ubo);
            m_frameTex.push_back(gr.tex.get());
            continue;
        }

        if (l.asciiScreen) {
            // Uniform-driven ASCII coverage ramp: the adjust chain's fp16
            // source in, content texture out; the glyph atlas rides an RGBA8
            // texture keyed by font/charset/cell. Every slider = UBO rewrite.
            QRhiTexture* srcTex = ensureAdjustRes(l, true, rub);
            const AsciiGpuAtlas& atlas = AsciiRenderer::gpuAtlas(l.asciiSettings);
            AsciiRes& ar = m_asciiRes[l.id];
            if (!ar.tex || ar.size != l.contentSize) {
                ar.rt.reset();
                ar.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                ar.tex->create();
                ar.rt.reset(m_rhi->newTextureRenderTarget({ ar.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(ar.rt->newCompatibleRenderPassDescriptor());
                ar.rt->setRenderPassDescriptor(m_dotRp.get());
                ar.rt->create();
                ar.size = l.contentSize;
            }
            bool srbDirty = (ar.srcBound != srcTex);
            const quint64 ak = asciiAtlasKey(l.asciiSettings);
            if (!ar.atlasTex || ar.atlasKey != ak
                || ar.atlasTex->pixelSize() != atlas.image.size()) {
                if (!ar.atlasTex || ar.atlasTex->pixelSize() != atlas.image.size()) {
                    ar.atlasTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, atlas.image.size()));
                    ar.atlasTex->create();
                    srbDirty = true;
                }
                rub->uploadTexture(ar.atlasTex.get(), atlas.image);
                ar.atlasKey = ak;
            }
            if (!ar.ubo) {
                ar.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kAsciiUboBytes));
                ar.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !ar.srb) {
                ar.srb.reset(m_rhi->newShaderResourceBindings());
                ar.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage
                         | QRhiShaderResourceBinding::FragmentStage, ar.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage,
                        srcTex, m_mipSampler.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        2, QRhiShaderResourceBinding::FragmentStage,
                        ar.atlasTex.get(), m_sampler.get()),
                });
                ar.srb->create();
                ar.srcBound = srcTex;
            }
            float ubo[kAsciiUboFloats] = {};
            writeMat(ubo, m_rhi->clipSpaceCorrMatrix());
            fillAsciiParams(ubo, l, atlas);
            rub->updateDynamicBuffer(ar.ubo.get(), 0, kAsciiUboBytes, ubo);
            m_frameTex.push_back(ar.tex.get());
            continue;
        }

        if (l.mosaicScreen) {
            // Uniform-driven Mosaic tile fill: same resources shape as the
            // Dot Grid pass — mosaic.vert rebuilds the lattice and samples
            // the adjust chain's source per tile. Every slider = UBO only.
            QRhiTexture* srcTex = ensureAdjustRes(l, true, rub);
            DotRes& mr = m_mosRes[l.id];
            if (!mr.tex || mr.size != l.contentSize) {
                mr.rt.reset();
                mr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                mr.tex->create();
                mr.rt.reset(m_rhi->newTextureRenderTarget({ mr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(mr.rt->newCompatibleRenderPassDescriptor());
                mr.rt->setRenderPassDescriptor(m_dotRp.get());
                mr.rt->create();
                mr.size = l.contentSize;
            }
            bool srbDirty = (mr.srcBound != srcTex);
            if (!mr.ubo) {
                mr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kMosUboBytes));
                mr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !mr.srb) {
                mr.srb.reset(m_rhi->newShaderResourceBindings());
                mr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage, mr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::VertexStage,
                        srcTex, m_mipSampler.get()),
                });
                mr.srb->create();
                mr.srcBound = srcTex;
            }
            QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
            mvp.ortho(0.0f, float(l.contentSize.width()),
                      float(l.contentSize.height()), 0.0f, -1.0f, 1.0f);
            float ubo[kMosUboFloats] = {};
            writeMat(ubo, mvp);
            mr.dotCount = fillMosaicParams(ubo, l);
            rub->updateDynamicBuffer(mr.ubo.get(), 0, kMosUboBytes, ubo);
            m_frameTex.push_back(mr.tex.get());
            continue;
        }

        if (l.ditherScreen) {
            // Uniform-driven ordered/threshold dither: the adjust chain's
            // fp16 linear output in, content texture out; the threshold
            // matrix rides a small R32F texture keyed by (algorithm,
            // bayerSize, pattern). Every slider only rewrites UBOs.
            QRhiTexture* srcTex = ensureAdjustRes(l, true, rub);
            DitherRes& xr = m_ditherRes[l.id];
            if (!xr.tex || xr.size != l.contentSize) {
                xr.rt.reset();
                xr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                xr.tex->create();
                xr.rt.reset(m_rhi->newTextureRenderTarget({ xr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(xr.rt->newCompatibleRenderPassDescriptor());
                xr.rt->setRenderPassDescriptor(m_dotRp.get());
                xr.rt->create();
                xr.size = l.contentSize;
            }
            bool srbDirty = (xr.srcBound != srcTex);
            const quint64 mk = ditherMaskKey(l.ditherSettings);
            const DitherGpuMask mask = DitherRenderer::gpuMask(l.ditherSettings);
            if (mask.w > 0
                && (!xr.maskTex || xr.maskKey != mk
                    || xr.maskTex->pixelSize() != QSize(mask.w, mask.h))) {
                if (!xr.maskTex || xr.maskTex->pixelSize() != QSize(mask.w, mask.h)) {
                    xr.maskTex.reset(m_rhi->newTexture(QRhiTexture::R32F,
                                                       QSize(mask.w, mask.h)));
                    xr.maskTex->create();
                    srbDirty = true;
                }
                QRhiTextureSubresourceUploadDescription sd(
                    mask.t.data(), quint32(mask.t.size() * sizeof(float)));
                rub->uploadTexture(xr.maskTex.get(),
                                   QRhiTextureUploadDescription({ 0, 0, sd }));
                xr.maskKey = mk;
            }
            if (!xr.ubo) {
                xr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kDitherUboBytes));
                xr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !xr.srb) {
                xr.srb.reset(m_rhi->newShaderResourceBindings());
                xr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage
                         | QRhiShaderResourceBinding::FragmentStage, xr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage,
                        srcTex, m_mipSampler.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        2, QRhiShaderResourceBinding::FragmentStage,
                        xr.maskTex ? xr.maskTex.get() : m_dummyTex.get(),
                        m_sampler.get()),
                });
                xr.srb->create();
                xr.srcBound = srcTex;
            }
            float ubo[kDitherUboFloats] = {};
            writeMat(ubo, m_rhi->clipSpaceCorrMatrix());
            fillDitherParams(ubo, l, mask.w, mask.h);
            rub->updateDynamicBuffer(xr.ubo.get(), 0, kDitherUboBytes, ubo);
            m_frameTex.push_back(xr.tex.get());
            continue;
        }

        if (l.adjustChain) {
            // Original layer with neighborhood ops: the full adjust chain
            // outputs a premultiplied RGBA8 raster the composite pass samples
            // like any other layer texture (composite's gpuAdjust stays off).
            m_frameTex.push_back(ensureAdjustRes(l, false, rub));
            continue;
        }

        LayerTex& lt = m_layerTex[layerTexKey(l.id, l.image.size())];
        if (!lt.tex) {
            lt.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.image.size()));
            lt.tex->create();
            lt.key = -1;
        }
        if (lt.key != l.image.cacheKey()) {
            const QImage img = l.image.format() == QImage::Format_RGBA8888_Premultiplied
                             ? l.image
                             : l.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            rub->uploadTexture(lt.tex.get(), img);
            lt.key = l.image.cacheKey();
        }
        m_frameTex.push_back(lt.tex.get());
    }
    for (auto it = m_layerTex.begin(); it != m_layerTex.end();) {
        if (!aliveIds.contains(int(it->first >> 32))) it = m_layerTex.erase(it);
        else ++it;
    }
    for (auto it = m_dotRes.begin(); it != m_dotRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_dotRes.erase(it);
        else ++it;
    }
    for (auto it = m_halfRes.begin(); it != m_halfRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_halfRes.erase(it);
        else ++it;
    }
    for (auto it = m_adjRes.begin(); it != m_adjRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_adjRes.erase(it);
        else ++it;
    }
    for (auto it = m_ditherRes.begin(); it != m_ditherRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_ditherRes.erase(it);
        else ++it;
    }
    for (auto it = m_mosRes.begin(); it != m_mosRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_mosRes.erase(it);
        else ++it;
    }
    for (auto it = m_asciiRes.begin(); it != m_asciiRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_asciiRes.erase(it);
        else ++it;
    }
    for (auto it = m_asciiGridRes.begin(); it != m_asciiGridRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_asciiGridRes.erase(it);
        else ++it;
    }
}

void GpuCanvasWidget::ensureHalfPipeline()
{
    if (m_halfPipeline || !m_dotRp) return;
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_halfRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_halfPipeline.reset(m_rhi->newGraphicsPipeline());
    m_halfPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/halftone.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/halftone.frag.qsb")) },
    });
    m_halfPipeline->setVertexInputLayout(il);   // opaque fullscreen write, no blend
    m_halfPipeline->setShaderResourceBindings(layoutSrb);
    m_halfPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_halfPipeline->create();
}

// Two identical adjust pipelines, one per target format (fp16 work/out
// targets vs the RGBA8 out target of the composite-raster case).
void GpuCanvasWidget::ensureAdjPipelines()
{
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_adjRes)
        if (!kv.second.srbs.empty()) { layoutSrb = kv.second.srbs[0].get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    auto make = [&](QRhiRenderPassDescriptor* rp) {
        auto* p = m_rhi->newGraphicsPipeline();
        p->setShaderStages({
            { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/adjust.vert.qsb")) },
            { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/adjust.frag.qsb")) },
        });
        p->setVertexInputLayout(il);   // opaque fullscreen write, no blend
        p->setShaderResourceBindings(layoutSrb);
        p->setRenderPassDescriptor(rp);
        p->create();
        return p;
    };
    if (!m_adjPipeF16 && m_adjRpF16) m_adjPipeF16.reset(make(m_adjRpF16.get()));
    if (!m_adjPipe8   && m_adjRp8)   m_adjPipe8.reset(make(m_adjRp8.get()));
}

void GpuCanvasWidget::ensureAsciiPipeline()
{
    if (m_asciiPipeline || !m_dotRp) return;
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_asciiRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_asciiPipeline.reset(m_rhi->newGraphicsPipeline());
    m_asciiPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/ascii.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/ascii.frag.qsb")) },
    });
    m_asciiPipeline->setVertexInputLayout(il);   // opaque fullscreen write, no blend
    m_asciiPipeline->setShaderResourceBindings(layoutSrb);
    m_asciiPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_asciiPipeline->create();
}

void GpuCanvasWidget::ensureAsciiGridPipeline()
{
    if (m_asciiGridPipeline || !m_dotRp) return;
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_asciiGridRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });   // quad verts; instances are procedural
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_asciiGridPipeline.reset(m_rhi->newGraphicsPipeline());
    m_asciiGridPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/ascii_grid.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/ascii_grid.frag.qsb")) },
    });
    m_asciiGridPipeline->setVertexInputLayout(il);
    QRhiGraphicsPipeline::TargetBlend tb;   // premultiplied over (glyphs can overlap)
    tb.enable   = true;
    tb.srcColor = QRhiGraphicsPipeline::One;
    tb.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    tb.srcAlpha = QRhiGraphicsPipeline::One;
    tb.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_asciiGridPipeline->setTargetBlends({ tb });
    m_asciiGridPipeline->setShaderResourceBindings(layoutSrb);
    m_asciiGridPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_asciiGridPipeline->create();
}

void GpuCanvasWidget::ensureMosPipeline()
{
    if (m_mosPipeline || !m_dotRp) return;
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_mosRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });   // quad verts; instances are procedural
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_mosPipeline.reset(m_rhi->newGraphicsPipeline());
    m_mosPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/mosaic.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/mosaic.frag.qsb")) },
    });
    m_mosPipeline->setVertexInputLayout(il);
    QRhiGraphicsPipeline::TargetBlend tb;   // premultiplied over (tiles can overlap)
    tb.enable   = true;
    tb.srcColor = QRhiGraphicsPipeline::One;
    tb.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    tb.srcAlpha = QRhiGraphicsPipeline::One;
    tb.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_mosPipeline->setTargetBlends({ tb });
    m_mosPipeline->setShaderResourceBindings(layoutSrb);
    m_mosPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_mosPipeline->create();
}

void GpuCanvasWidget::ensureDitherPipeline()
{
    if (m_ditherPipeline || !m_dotRp) return;
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_ditherRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_ditherPipeline.reset(m_rhi->newGraphicsPipeline());
    m_ditherPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/dither.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/dither.frag.qsb")) },
    });
    m_ditherPipeline->setVertexInputLayout(il);   // opaque fullscreen write, no blend
    m_ditherPipeline->setShaderResourceBindings(layoutSrb);
    m_ditherPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_ditherPipeline->create();
}

void GpuCanvasWidget::ensureDotPipeline()
{
    if (m_dotPipeline || !m_dotRp) return;
    // Layout SRB: any per-layer dot SRB has the identical UBO+texture layout;
    // the map entries outlive create().
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_dotRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });   // quad verts; instances are procedural
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_dotPipeline.reset(m_rhi->newGraphicsPipeline());
    m_dotPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/dot.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/dot.frag.qsb")) },
    });
    m_dotPipeline->setVertexInputLayout(il);
    QRhiGraphicsPipeline::TargetBlend tb;   // premultiplied over
    tb.enable   = true;
    tb.srcColor = QRhiGraphicsPipeline::One;
    tb.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    tb.srcAlpha = QRhiGraphicsPipeline::One;
    tb.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_dotPipeline->setTargetBlends({ tb });
    m_dotPipeline->setShaderResourceBindings(layoutSrb);
    m_dotPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_dotPipeline->create();
}

void GpuCanvasWidget::rebuildCompositeSrbs()
{
    // Only rebuild when the resources the SRBs point at actually changed
    // (per-frame rebuild was pure churn — UBO pointers are stable, blend and
    // adjustments travel in the UBO, not the SRB).
    const int n = m_pkg.layers.size();
    std::vector<void*> sig;
    sig.reserve(size_t(n) + 2);
    sig.push_back(m_accum[0].get());
    sig.push_back(m_accum[1].get());
    for (QRhiTexture* t : m_frameTex) sig.push_back(t);
    if (sig == m_srbSig && int(m_compSrbs.size()) == n)
        return;

    while (int(m_compUbufs.size()) < n) {
        auto* b = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CompUbo));
        b->create();
        m_compUbufs.emplace_back(b);
    }

    m_compSrbs.clear();
    for (int i = 0; i < n; ++i) {
        QRhiTexture* dst = m_accum[i & 1].get();
        QRhiTexture* src = m_frameTex[size_t(i)];
        auto* srb = m_rhi->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_compUbufs[size_t(i)].get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, dst, m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, src, m_sampler.get()),
        });
        srb->create();
        m_compSrbs.emplace_back(srb);
    }
    m_srbSig = std::move(sig);
}

QMatrix4x4 GpuCanvasWidget::presentMatrix() const
{
    // Map the unit quad onto m_viewRect (logical widget coords → NDC).
    const double w = qMax(1, width());
    const double h = qMax(1, height());
    QRectF r = m_viewRect.isEmpty() ? QRectF(0, 0, w, h) : m_viewRect;

    const double sx = r.width()  / w;
    const double sy = r.height() / h;
    const double tx = 2.0 * r.center().x() / w - 1.0;
    const double ty = 1.0 - 2.0 * r.center().y() / h;   // NDC y up

    QMatrix4x4 m;
    m.translate(float(tx), float(ty));
    m.scale(float(sx), float(sy));
    return m_rhi->clipSpaceCorrMatrix() * m;
}

void GpuCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_rhi || !m_blitPipeline)
        return;

    QRhiResourceUpdateBatch* rub = m_rhi->nextResourceUpdateBatch();
    if (m_vbufDirty) {
        rub->uploadStaticBuffer(m_vbuf.get(), kQuad);
        m_vbufDirty = false;
    }

    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf.get(), 0);
    m_presentTex = nullptr;

    if (m_packageMode) {
        ensureAccumTargets(m_pkg.frame);   // cheap no-op unless pkg.frame's size changed
        const int n = m_pkg.layers.size();

        // A pure resize/pan/zoom repaint (no new package since the last
        // render) doesn't need any of this redone — every content pass,
        // adjust chain and blend pass reruns otherwise on EVERY repaint,
        // which is what made dragging a splitter (repeatedly resizing this
        // widget) or panning/zooming feel so slow: full-cost GPU work per
        // tick for zero visual change. Reuse last frame's composite instead;
        // only the final blit below (viewRect/present matrix) still runs.
        if (!m_pkgDirty) {
            m_presentTex = m_accum[n & 1].get();
        } else {
        ensureLayerTextures(rub);
        ensureAdjPipelines();
        ensureDotPipeline();
        ensureHalfPipeline();
        ensureDitherPipeline();
        ensureMosPipeline();
        ensureAsciiPipeline();
        ensureAsciiGridPipeline();
        rebuildCompositeSrbs();   // no-op unless textures/targets changed

        // All per-pass UBOs written up front in the same batch.
        const QMatrix4x4 corr = m_rhi->clipSpaceCorrMatrix();
        for (int i = 0; i < n; ++i) {
            const GpuLayer& l = m_pkg.layers[i];
            CompUbo u;
            writeMat(u.mvp, corr);
            QMatrix4x4 inv(l.placement.inverted());   // frame px → layer px (3x3 → 4x4)
            writeMat(u.invPlacement, inv);
            u.sizes[0] = float(m_pkg.frame.width());
            u.sizes[1] = float(m_pkg.frame.height());
            u.sizes[2] = float(l.contentSize.width());
            u.sizes[3] = float(l.contentSize.height());
            u.modeFlags[0] = int(l.blend);
            u.modeFlags[1] = l.gpuAdjust ? 1 : 0;
            u.modeFlags[2] = u.modeFlags[3] = 0;
            const Adjustments& a = l.adj;   // shader expects "actual" values (see composite.frag)
            u.adjA[0] = float(a.brightness);
            u.adjA[1] = float(a.contrast);
            u.adjA[2] = float(a.gamma) / 100.0f;
            u.adjA[3] = float(qBound(0, 100 + a.saturation, 200));
            u.adjB[0] = float(a.levelsBlack);
            u.adjB[1] = float(a.levelsMid) / 100.0f;
            u.adjB[2] = float(a.levelsWhite);
            u.adjB[3] = float(a.grain) * 1.6f / 255.0f;
            u.adjC[0] = float(a.posterize);
            u.adjC[1] = float(a.threshold);
            u.adjC[2] = a.invert ? 1.0f : 0.0f;
            u.adjC[3] = 0.0f;
            rub->updateDynamicBuffer(m_compUbufs[size_t(i)].get(), 0, sizeof(CompUbo), &u);
        }

        // Adjust chains first: bake each chain layer's adjusted source before
        // the dot/halftone content passes (and the composite) sample it. The
        // generateMips of an fp16 output rides the batch consumed by the NEXT
        // beginPass, i.e. after these passes have run.
        for (const GpuLayer& l : m_pkg.layers) {
            if (!l.adjustChain) continue;
            auto arIt = m_adjRes.find(l.id);
            if (arIt == m_adjRes.end()) continue;
            AdjRes& ar = arIt->second;
            for (size_t pi = 0; pi < ar.plan.size(); ++pi) {
                const AdjPass& p = ar.plan[pi];
                QRhiTextureRenderTarget* rt =
                    (p.dst == -2) ? ar.outRt.get() : ar.pingRt[p.dst].get();
                QRhiGraphicsPipeline* pipe =
                    (p.dst == -2 && !ar.f16Out) ? m_adjPipe8.get() : m_adjPipeF16.get();
                if (!rt) continue;
                cb->beginPass(rt, Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (pipe && pi < ar.srbs.size()) {
                    cb->setGraphicsPipeline(pipe);
                    const QSize ts = rt->pixelSize();
                    cb->setViewport({ 0, 0, float(ts.width()), float(ts.height()) });
                    cb->setShaderResources(ar.srbs[pi].get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
                cb->endPass();
            }
            if (ar.f16Out && ar.outTex) {
                if (!rub) rub = m_rhi->nextResourceUpdateBatch();
                rub->generateMips(ar.outTex.get());
            }
        }

        // Content passes: rasterize instanced Dot Grid and halftone-screen
        // layers into their offscreen targets before compositing samples them.
        for (int i = 0; i < n; ++i) {
            const GpuLayer& l = m_pkg.layers[i];
            if (l.halftoneScreen) {
                auto hrIt = m_halfRes.find(l.id);
                if (hrIt == m_halfRes.end() || !hrIt->second.rt) continue;
                HalfRes& hr = hrIt->second;
                cb->beginPass(hr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (m_halfPipeline) {
                    cb->setGraphicsPipeline(m_halfPipeline.get());
                    cb->setViewport({ 0, 0, float(hr.size.width()), float(hr.size.height()) });
                    cb->setShaderResources(hr.srb.get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
                cb->endPass();
                continue;
            }
            if (l.asciiInstanced) {
                auto grIt = m_asciiGridRes.find(l.id);
                if (grIt == m_asciiGridRes.end() || !grIt->second.rt) continue;
                AsciiGridRes& gr = grIt->second;
                cb->beginPass(gr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (m_asciiGridPipeline && gr.count > 0) {
                    cb->setGraphicsPipeline(m_asciiGridPipeline.get());
                    cb->setViewport({ 0, 0, float(gr.size.width()), float(gr.size.height()) });
                    cb->setShaderResources(gr.srb.get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6, gr.count);
                }
                cb->endPass();
                continue;
            }
            if (l.asciiScreen) {
                auto arIt = m_asciiRes.find(l.id);
                if (arIt == m_asciiRes.end() || !arIt->second.rt) continue;
                AsciiRes& ar = arIt->second;
                cb->beginPass(ar.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (m_asciiPipeline) {
                    cb->setGraphicsPipeline(m_asciiPipeline.get());
                    cb->setViewport({ 0, 0, float(ar.size.width()), float(ar.size.height()) });
                    cb->setShaderResources(ar.srb.get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
                cb->endPass();
                continue;
            }
            if (l.mosaicScreen) {
                auto mrIt = m_mosRes.find(l.id);
                if (mrIt == m_mosRes.end() || !mrIt->second.rt) continue;
                DotRes& mr = mrIt->second;
                cb->beginPass(mr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (m_mosPipeline && mr.dotCount > 0) {
                    cb->setGraphicsPipeline(m_mosPipeline.get());
                    cb->setViewport({ 0, 0, float(mr.size.width()), float(mr.size.height()) });
                    cb->setShaderResources(mr.srb.get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6, mr.dotCount);
                }
                cb->endPass();
                continue;
            }
            if (l.ditherScreen) {
                auto xrIt = m_ditherRes.find(l.id);
                if (xrIt == m_ditherRes.end() || !xrIt->second.rt) continue;
                DitherRes& xr = xrIt->second;
                cb->beginPass(xr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (m_ditherPipeline) {
                    cb->setGraphicsPipeline(m_ditherPipeline.get());
                    cb->setViewport({ 0, 0, float(xr.size.width()), float(xr.size.height()) });
                    cb->setShaderResources(xr.srb.get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
                cb->endPass();
                continue;
            }
            if (!l.dotScreen) continue;
            auto drIt = m_dotRes.find(l.id);
            if (drIt == m_dotRes.end() || !drIt->second.rt) continue;
            DotRes& dr = drIt->second;
            cb->beginPass(dr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
            rub = nullptr;   // first pass consumes the shared update batch
            if (m_dotPipeline && dr.dotCount > 0) {
                cb->setGraphicsPipeline(m_dotPipeline.get());
                cb->setViewport({ 0, 0, float(dr.size.width()), float(dr.size.height()) });
                cb->setShaderResources(dr.srb.get());
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6, dr.dotCount);
            }
            cb->endPass();
        }

        // Pass 0: clear accum A to the (premultiplied) background colour.
        QColor bg = m_pkg.bg.isValid() ? m_pkg.bg : QColor(0, 0, 0, 0);
        const float ba = float(bg.alphaF());
        const QColor bgPremul = QColor::fromRgbF(float(bg.redF()) * ba,
                                                 float(bg.greenF()) * ba,
                                                 float(bg.blueF()) * ba, ba);
        cb->beginPass(m_accumRt[0].get(), bgPremul, { 1.0f, 0 }, rub);
        cb->endPass();
        rub = nullptr;

        // One fullscreen blend pass per layer, ping-ponging A↔B.
        for (int i = 0; i < n; ++i) {
            QRhiTextureRenderTarget* target = m_accumRt[(i + 1) & 1].get();
            cb->beginPass(target, Qt::transparent, { 1.0f, 0 });
            cb->setGraphicsPipeline(m_compPipeline.get());
            const QSize ts = target->pixelSize();
            cb->setViewport({ 0, 0, float(ts.width()), float(ts.height()) });
            cb->setShaderResources(m_compSrbs[size_t(i)].get());
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(6);
            cb->endPass();
        }

        m_presentTex = m_accum[n & 1].get();
        m_pkgDirty = false;
        }
    } else {
        if (m_imageDirty && !m_image.isNull()) {
            if (!m_imageTex || m_imageTex->pixelSize() != m_image.size()) {
                m_imageTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_image.size()));
                m_imageTex->create();
            }
            rub->uploadTexture(m_imageTex.get(), m_image);
            m_imageDirty = false;
        }
        m_presentTex = m_imageTex.get();
    }

    // Present pass: blit the result into the widget at viewRect.
    QMatrix4x4 mvp = presentMatrix();
    QRhiResourceUpdateBatch* rubP = rub ? rub : m_rhi->nextResourceUpdateBatch();
    rubP->updateDynamicBuffer(m_blitUbuf.get(), 0, 64, mvp.constData());

    if (m_presentTex && (m_presentTex != m_blitSrbTex || !m_blitSrb)) {
        m_blitSrb.reset(m_rhi->newShaderResourceBindings());
        m_blitSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, m_blitUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                m_presentTex, m_sampler.get()),
        });
        m_blitSrb->create();
        m_blitSrbTex = m_presentTex;
    }

    const QColor clear(0x1E, 0x1E, 0x1E);
    cb->beginPass(renderTarget(), clear, { 1.0f, 0 }, rubP);
    if (m_presentTex) {
        cb->setGraphicsPipeline(m_blitPipeline.get());
        const QSize out = renderTarget()->pixelSize();
        cb->setViewport({ 0, 0, float(out.width()), float(out.height()) });
        cb->setShaderResources(m_blitSrb.get());
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(6);
    }
    cb->endPass();
}
