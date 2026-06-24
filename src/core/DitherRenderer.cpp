#include "DitherRenderer.h"
#include "ColorMath.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QHash>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

// ============================================================
//  Scalar helpers
// ============================================================

// Per-tone opacity → output alpha channel (0..255).
inline int toneAlpha(const ToneEntry& t)
{
    return qRound(qBound(0.0f, t.opacity, 1.0f) * 255.0f);
}

std::vector<ToneEntry> sortedTones(const std::vector<ToneEntry>& tones)
{
    std::vector<ToneEntry> t = tones;
    std::sort(t.begin(), t.end(),
              [](const ToneEntry& a, const ToneEntry& b) { return a.level < b.level; });
    return t;
}

// ============================================================
//  Single-pixel quantisation in LINEAR light (shared by error
//  diffusion and dot diffusion).
//
//  r,g,b are linear-light channels (0..1), already composited over
//  white. The chosen colour is written to outPx in sRGB; its *linear*
//  channels are returned in cr,cg,cb so the caller can diffuse the
//  residual error in linear light. Tone selection uses perceptual
//  luma so the 0..255 level sliders keep their meaning.
// ============================================================

inline void quantizePixel(
    float r, float g, float b,
    bool imageColors, const std::vector<ToneEntry>& tones, int nTones,
    int L, float inkLumLin,
    const std::vector<ColorMath::PaletteEntry>* palette,
    float& cr, float& cg, float& cb, QRgb& outPx)
{
    using namespace ColorMath;

    if (palette && !palette->empty()) {
        // True palette dithering: pick the nearest colour (OkLab) and diffuse
        // the residual against that colour's linear value.
        const PaletteEntry& e = (*palette)[nearestPaletteIndex(*palette, linearToOklab(r, g, b))];
        cr = e.rLin; cg = e.gLin; cb = e.bLin;
        outPx = e.out;
    } else if (imageColors) {
        const float steps = float(L - 1);
        auto q = [&](float v) {
            return std::round(qBound(0.0f, v, 1.0f) * steps) / steps;
        };
        cr = q(r); cg = q(g); cb = q(b);
        outPx = qRgba(linearToSrgb8(cr), linearToSrgb8(cg), linearToSrgb8(cb), 255);
    } else if (nTones <= 1) {
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const bool  ink = std::fabs(lum - inkLumLin) <= std::fabs(lum - 1.0f);
        if (ink) {
            const QColor& c = tones.empty() ? QColor(Qt::black) : tones[0].color;
            cr = srgbToLinear(c.red());
            cg = srgbToLinear(c.green());
            cb = srgbToLinear(c.blue());
            const int a = tones.empty() ? 255 : toneAlpha(tones[0]);
            outPx = qRgba(c.red(), c.green(), c.blue(), a);
        } else {
            cr = cg = cb = 1.0f;                  // paper = white
            outPx = qRgba(0, 0, 0, 0);
        }
    } else {
        const float pl  = perceptualLumaFromLinear(r, g, b);
        const int   idx = pickToneIndex(tones, pl);
        const QColor& c = tones[idx].color;
        cr = srgbToLinear(c.red());
        cg = srgbToLinear(c.green());
        cb = srgbToLinear(c.blue());
        outPx = qRgba(c.red(), c.green(), c.blue(), toneAlpha(tones[idx]));
    }
}

// ============================================================
//  Ordered threshold matrices
// ============================================================

// Bayer: recursive construction; n must be a power of two.
std::vector<float> bayerMatrix(int n)
{
    std::vector<int> m = { 0 };
    int size = 1;
    while (size < n) {
        std::vector<int> nm(size_t(size) * size * 4);
        int s2 = size * 2;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int v = m[size_t(y) * size + x] * 4;
                nm[size_t(y) * s2 + x]                = v;
                nm[size_t(y) * s2 + x + size]         = v + 2;
                nm[size_t(y + size) * s2 + x]         = v + 3;
                nm[size_t(y + size) * s2 + x + size]  = v + 1;
            }
        }
        m = std::move(nm);
        size = s2;
    }
    std::vector<float> out(m.size());
    const float denom = float(n) * float(n);
    for (size_t i = 0; i < m.size(); ++i) out[i] = (m[i] + 0.5f) / denom;
    return out;
}

// Clustered dot: 4×4 (grows from centre outward — mimics a printing screen).
const int kClustered4[16] = {
    12, 5,  6, 13,
     4, 0,  1,  7,
    11, 3,  2,  8,
    15, 10, 9, 14
};

// Clustered dot: 8×8 (denser, closer to newspaper half-tone).
const int kClustered8[64] = {
    24, 10, 12, 26, 35, 47, 49, 37,
     8,  0,  2, 14, 45, 59, 61, 51,
    22,  6,  4, 16, 43, 57, 63, 53,
    30, 20, 18, 28, 33, 41, 55, 39,
    34, 46, 48, 36, 25, 11, 13, 27,
    44, 58, 60, 50,  9,  1,  3, 15,
    42, 56, 62, 52, 23,  7,  5, 17,
    32, 40, 54, 38, 31, 21, 19, 29
};

// ============================================================
//  Void-and-cluster mask generation
//  Implements Ulichney's algorithm (1993).
//  Produces a blue-noise-distributed threshold map on an n×n toroidal grid.
//  Called once per unique (n, seed) pair and then cached.
// ============================================================

std::vector<float> generateVoidCluster(int n, uint32_t seed)
{
    const int N = n * n;
    const float sigma  = std::max(1.5f, float(n) / 8.0f);
    const float inv2s2 = 1.0f / (2.0f * sigma * sigma);
    const int   R      = std::max(2, int(std::ceil(sigma * 2.5f)));

    // Precompute the Gaussian kernel offsets and weights.
    struct KTap { int dx, dy; float w; };
    std::vector<KTap> kernel;
    kernel.reserve(size_t((2*R+1) * (2*R+1)));
    for (int dy = -R; dy <= R; ++dy)
        for (int dx = -R; dx <= R; ++dx)
            kernel.push_back({ dx, dy, std::exp(-float(dx*dx + dy*dy) * inv2s2) });

    std::vector<float> energy(N, 0.0f);
    std::vector<bool>  occupied(N, false);

    // Toroidal energy update.
    auto addEnergy = [&](int cx, int cy, float sign) {
        for (const KTap& k : kernel) {
            int x = ((cx + k.dx) % n + n) % n;
            int y = ((cy + k.dy) % n + n) % n;
            energy[y * n + x] += sign * k.w;
        }
    };

    // Xorshift32 deterministic PRNG.
    uint32_t rng = seed ? seed : 0xDEADBEEFu;
    auto xr32 = [&]() -> uint32_t {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
    };

    // Random initial occupancy (N/2 occupied).
    std::vector<int> perm(N);
    std::iota(perm.begin(), perm.end(), 0);
    for (int i = N - 1; i > 0; --i) {
        int j = int(xr32() % unsigned(i + 1));
        std::swap(perm[i], perm[j]);
    }
    for (int k = 0; k < N / 2; ++k) {
        occupied[perm[k]] = true;
        addEnergy(perm[k] % n, perm[k] / n, 1.0f);
    }

    std::vector<int> rank(N, -1);

    // Phase 1: assign ranks N/2 … N-1 by removing the most-clustered point first.
    for (int k = N - 1; k >= N / 2; --k) {
        int maxI = -1; float maxE = -1e30f;
        for (int i = 0; i < N; ++i)
            if (occupied[i] && energy[i] > maxE) { maxE = energy[i]; maxI = i; }
        rank[maxI] = k;
        occupied[maxI] = false;
        addEnergy(maxI % n, maxI / n, -1.0f);
    }

    // Phase 2: assign ranks 0 … N/2-1 by filling the most-void cell first.
    for (int k = N / 2 - 1; k >= 0; --k) {
        int minI = -1; float minE = 1e30f;
        for (int i = 0; i < N; ++i)
            if (!occupied[i] && energy[i] < minE) { minE = energy[i]; minI = i; }
        rank[minI] = k;
        occupied[minI] = true;
        addEnergy(minI % n, minI / n, 1.0f);
    }

    std::vector<float> mask(N);
    for (int i = 0; i < N; ++i) mask[i] = (rank[i] + 0.5f) / float(N);
    return mask;
}

// Lazily initialised masks — thread-safe (C++11 static-local guarantee).
const std::vector<float>& blueNoiseMask()
{
    // 64×64 void-and-cluster mask, seed A.
    static const std::vector<float> m = generateVoidCluster(64, 0xF3A2C1D4u);
    return m;
}
const std::vector<float>& voidClusterMask()
{
    // 32×32 void-and-cluster mask, seed B (different spatial frequency).
    static const std::vector<float> m = generateVoidCluster(32, 0x1B7E4F93u);
    return m;
}

// ============================================================
//  Error-diffusion kernels
//  KTap: { column_offset, row_offset, weight }
//  column_offset is relative to the current pixel along the scan axis
//  (negated automatically in serpentine passes).
// ============================================================

struct KTap { int da, db; float w; };

// Floyd–Steinberg (1976)
const KTap kFS[] = {
    { 1,0, 7.f/16 }, { -1,1, 3.f/16 }, { 0,1, 5.f/16 }, { 1,1, 1.f/16 }
};
// False Floyd–Steinberg (lightweight approximation)
const KTap kFFS[] = {
    { 1,0, 3.f/8 }, { 0,1, 3.f/8 }, { 1,1, 2.f/8 }
};
// Jarvis–Judice–Ninke (1976)
const KTap kJJN[] = {
    { 1,0, 7.f/48 }, { 2,0, 5.f/48 },
    { -2,1, 3.f/48 }, { -1,1, 5.f/48 }, { 0,1, 7.f/48 }, { 1,1, 5.f/48 }, { 2,1, 3.f/48 },
    { -2,2, 1.f/48 }, { -1,2, 3.f/48 }, { 0,2, 5.f/48 }, { 1,2, 3.f/48 }, { 2,2, 1.f/48 }
};
// Burkes (1988)
const KTap kBurkes[] = {
    { 1,0, 8.f/32 }, { 2,0, 4.f/32 },
    { -2,1, 2.f/32 }, { -1,1, 4.f/32 }, { 0,1, 8.f/32 }, { 1,1, 4.f/32 }, { 2,1, 2.f/32 }
};
// Atkinson (1987)
const KTap kAtkinson[] = {
    { 1,0, 1.f/8 }, { 2,0, 1.f/8 },
    { -1,1, 1.f/8 }, { 0,1, 1.f/8 }, { 1,1, 1.f/8 },
    { 0,2, 1.f/8 }
};
// Sierra (1989)
const KTap kSierra[] = {
    { 1,0, 5.f/32 }, { 2,0, 3.f/32 },
    { -2,1, 2.f/32 }, { -1,1, 4.f/32 }, { 0,1, 5.f/32 }, { 1,1, 4.f/32 }, { 2,1, 2.f/32 },
    { -1,2, 2.f/32 }, { 0,2, 3.f/32 }, { 1,2, 2.f/32 }
};
// Sierra Lite / 2-4A (1989)
const KTap kSierraLite[] = {
    { 1,0, 2.f/4 }, { -1,1, 1.f/4 }, { 0,1, 1.f/4 }
};
// Stucki (1981)
const KTap kStucki[] = {
    { 1,0, 8.f/42 }, { 2,0, 4.f/42 },
    { -2,1, 2.f/42 }, { -1,1, 4.f/42 }, { 0,1, 8.f/42 }, { 1,1, 4.f/42 }, { 2,1, 2.f/42 },
    { -2,2, 1.f/42 }, { -1,2, 2.f/42 }, { 0,2, 4.f/42 }, { 1,2, 2.f/42 }, { 2,2, 1.f/42 }
};

} // anonymous namespace

// ============================================================
//  DitherRenderer — public entry point
// ============================================================

bool DitherRenderer::isOrdered(DitherAlgorithm a)
{
    return a == DitherAlgorithm::Bayer
        || a == DitherAlgorithm::ClusteredDot
        || a == DitherAlgorithm::BlueNoise
        || a == DitherAlgorithm::VoidAndCluster;
}

bool DitherRenderer::isHybrid(DitherAlgorithm a)
{
    return a == DitherAlgorithm::DotDiffusion;
}

bool DitherRenderer::isThreshold(DitherAlgorithm a)
{
    return a == DitherAlgorithm::Threshold;
}

QImage DitherRenderer::render(const QImage& input, const DitherSettings& s)
{
    if (input.isNull()) return {};

    const int ps = qBound(1, s.pixelSize, 32);
    QImage work = input;
    if (ps > 1) {
        work = input.scaled(qMax(1, input.width()  / ps),
                            qMax(1, input.height() / ps),
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    work = work.convertToFormat(QImage::Format_ARGB32);

    QImage out(work.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    if      (isThreshold(s.algorithm)) renderThreshold   (work, out, s);
    else if (isOrdered  (s.algorithm)) renderOrdered     (work, out, s);
    else if (isHybrid   (s.algorithm)) renderDotDiffusion(work, out, s);
    else                               renderDiffusion    (work, out, s);

    if (ps > 1) {
        if (s.cornerRadius > 0.0f) {
            const int ow = out.width();
            const int oh = out.height();

            // Identify highlight color: the fixed tone with the lowest level (darkest).
            // Image-colors and palette modes have no single ink color — skip
            // connected rounding (cells stay plain squares).
            const bool skipInk = (s.tonal.mode == ToneMode::ImageColors)
                              || (s.tonal.mode == ToneMode::Palette);
            quint32 inkRgb = 0;
            bool hasInk = false;
            if (!skipInk && !s.tonal.tones.empty()) {
                const ToneEntry* darkest = &s.tonal.tones[0];
                for (const auto& t : s.tonal.tones)
                    if (t.level < darkest->level) darkest = &t;
                inkRgb = quint32(darkest->color.rgb()) & 0x00FFFFFFu;
                hasInk = true;
            }

            auto cellRgb = [&](int col, int row) -> quint32 {
                if (col < 0 || col >= ow || row < 0 || row >= oh) return 0u;
                const QRgb px = reinterpret_cast<const QRgb*>(out.constScanLine(row))[col];
                return (qAlpha(px) > 0) ? (quint32(px) & 0x00FFFFFFu) : 0u;
            };

            auto isInkCell = [&](int col, int row) -> bool {
                return hasInk && cellRgb(col, row) == inkRgb;
            };

            QImage finalOut(input.size(), QImage::Format_ARGB32);
            finalOut.fill(Qt::transparent);
            QPainter painter(&finalOut);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            const float cr = qMin(s.cornerRadius, float(ps) * 0.5f);

            for (int row = 0; row < oh; ++row) {
                const QRgb* line = reinterpret_cast<const QRgb*>(out.constScanLine(row));
                for (int col = 0; col < ow; ++col) {
                    const QRgb px = line[col];
                    if (qAlpha(px) == 0) continue;

                    painter.setBrush(QColor(px));
                    const float x0 = float(col * ps);
                    const float y0 = float(row * ps);
                    const float x1 = x0 + ps;
                    const float y1 = y0 + ps;

                    if (isInkCell(col, row)) {
                        // Round only convex (outer) corners of the merged ink shape.
                        const bool left  = isInkCell(col - 1, row);
                        const bool right = isInkCell(col + 1, row);
                        const bool up    = isInkCell(col,     row - 1);
                        const bool down  = isInkCell(col,     row + 1);

                        const bool roundTL = !left && !up;
                        const bool roundTR = !right && !up;
                        const bool roundBR = !right && !down;
                        const bool roundBL = !left  && !down;

                        QPainterPath path;
                        path.moveTo(roundTL ? x0 + cr : x0, y0);

                        if (roundTR) {
                            path.lineTo(x1 - cr, y0);
                            path.arcTo(x1 - 2*cr, y0, 2*cr, 2*cr, 90, -90);
                        } else {
                            path.lineTo(x1, y0);
                        }
                        if (roundBR) {
                            path.lineTo(x1, y1 - cr);
                            path.arcTo(x1 - 2*cr, y1 - 2*cr, 2*cr, 2*cr, 0, -90);
                        } else {
                            path.lineTo(x1, y1);
                        }
                        if (roundBL) {
                            path.lineTo(x0 + cr, y1);
                            path.arcTo(x0, y1 - 2*cr, 2*cr, 2*cr, 270, -90);
                        } else {
                            path.lineTo(x0, y1);
                        }
                        if (roundTL) {
                            path.lineTo(x0, y0 + cr);
                            path.arcTo(x0, y0, 2*cr, 2*cr, 180, -90);
                        } else {
                            path.lineTo(x0, y0);
                        }
                        path.closeSubpath();
                        painter.drawPath(path);
                    } else {
                        // Non-ink colors: plain square, no rounding.
                        painter.drawRect(QRectF(x0, y0, float(ps), float(ps)));
                    }
                }
            }
            out = finalOut;
        } else {
            out = out.scaled(input.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }
    }

    // Apply opacity after compositing to avoid seams on shared edges.
    if (s.opacity < 1.0f) {
        const float op = qBound(0.0f, s.opacity, 1.0f);
        for (int y = 0; y < out.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                const QRgb px = line[x];
                if (qAlpha(px) == 0) continue;
                line[x] = qRgba(qRed(px), qGreen(px), qBlue(px),
                                qRound(qAlpha(px) * op));
            }
        }
    }

    return out;
}

// Greedy maximal-rectangle merge of the dithered cell grid → fillRect per
// merged block. Adjacent cells sharing a full edge and the exact same RGBA are
// fused, so a flat region becomes one big rect instead of hundreds.
int DitherRenderer::paintMergedRects(const QImage& input, const DitherSettings& s,
                                     QPainter& out, double targetW, double targetH)
{
    if (input.isNull()) return 0;

    // Build the quantised grid at cell resolution (the small image, before the
    // block upscale render() does), so each pixel here is exactly one cell.
    const int ps = qBound(1, s.pixelSize, 32);
    QImage work = input;
    if (ps > 1)
        work = input.scaled(qMax(1, input.width() / ps), qMax(1, input.height() / ps),
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    work = work.convertToFormat(QImage::Format_ARGB32);

    QImage grid(work.size(), QImage::Format_ARGB32);
    grid.fill(Qt::transparent);
    if      (isThreshold(s.algorithm)) renderThreshold   (work, grid, s);
    else if (isOrdered  (s.algorithm)) renderOrdered     (work, grid, s);
    else if (isHybrid   (s.algorithm)) renderDotDiffusion(work, grid, s);
    else                               renderDiffusion    (work, grid, s);

    const int W = grid.width(), H = grid.height();
    if (W == 0 || H == 0) return 0;
    const double cw = targetW / W, ch = targetH / H;
    const float  op = qBound(0.0f, s.opacity, 1.0f);

    std::vector<char> used(size_t(W) * H, 0);
    auto px = [&](int x, int y) { return reinterpret_cast<const QRgb*>(grid.constScanLine(y))[x]; };

    // Merge into maximal rects (in cell coords), collected per colour, then
    // simplify each colour's path to the outline of the union region — internal
    // rectangle edges are dropped, so a colour becomes one clean silhouette
    // (with holes) instead of a grid of abutting rectangles.
    QHash<QRgb, QPainterPath> byColor;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (used[size_t(y) * W + x]) continue;
            const QRgb c = px(x, y);
            if (qAlpha(c) == 0) { used[size_t(y) * W + x] = 1; continue; }   // empty cell

            int x2 = x;                                   // extend right
            while (x2 + 1 < W && !used[size_t(y) * W + x2 + 1] && px(x2 + 1, y) == c) ++x2;

            int y2 = y;                                   // extend down (full-width match)
            bool ok = true;
            while (ok && y2 + 1 < H) {
                for (int xx = x; xx <= x2; ++xx)
                    if (used[size_t(y2 + 1) * W + xx] || px(xx, y2 + 1) != c) { ok = false; break; }
                if (ok) ++y2;
            }

            for (int yy = y; yy <= y2; ++yy)
                for (int xx = x; xx <= x2; ++xx) used[size_t(yy) * W + xx] = 1;

            byColor[c].addRect(QRectF(x, y, x2 - x + 1, y2 - y + 1));   // cell units
        }
    }

    QTransform toTarget;
    toTarget.scale(cw, ch);   // cell units → target pixels (applied after simplify)

    out.save();
    out.setRenderHint(QPainter::Antialiasing, false);
    out.setPen(Qt::NoPen);
    for (auto it = byColor.begin(); it != byColor.end(); ++it) {
        QColor col = QColor::fromRgba(it.key());
        if (op < 1.0f) col.setAlphaF(col.alphaF() * op);
        // simplified(): unite abutting/overlapping subpaths into the region's
        // outline, removing the internal seams the editor was drawing.
        const QPainterPath outline = it.value().simplified();
        out.setBrush(col);
        out.drawPath(toTarget.map(outline));
    }
    out.restore();
    return byColor.size();
}

// ============================================================
//  Error diffusion
// ============================================================

void DitherRenderer::renderDiffusion(const QImage& work, QImage& out,
                                     const DitherSettings& s)
{
    const int w = work.width();
    const int h = work.height();
    if (w == 0 || h == 0) return;

    const KTap* taps  = kFS;
    int         nTaps = 4;

    switch (s.algorithm) {
        case DitherAlgorithm::FloydSteinberg:      taps = kFS;         nTaps = 4;  break;
        case DitherAlgorithm::FalseFloydSteinberg: taps = kFFS;        nTaps = 3;  break;
        case DitherAlgorithm::JarvisJudiceNinke:   taps = kJJN;        nTaps = 12; break;
        case DitherAlgorithm::Burkes:              taps = kBurkes;     nTaps = 7;  break;
        case DitherAlgorithm::Atkinson:            taps = kAtkinson;   nTaps = 6;  break;
        case DitherAlgorithm::Sierra:              taps = kSierra;     nTaps = 10; break;
        case DitherAlgorithm::SierraLite:          taps = kSierraLite; nTaps = 3;  break;
        case DitherAlgorithm::Stucki:              taps = kStucki;     nTaps = 12; break;
        default: break;
    }

    const float strength    = qBound(0, s.strength, 100) / 100.0f;
    const bool  imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const bool  paletteMode = (s.tonal.mode == ToneMode::Palette);
    const auto  tones       = sortedTones(s.tonal.tones);
    const int   nTones      = int(tones.size());
    const int   L           = 2;

    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tones) : std::vector<ColorMath::PaletteEntry>{};
    const std::vector<ColorMath::PaletteEntry>* pal =
        (paletteMode && !palette.empty()) ? &palette : nullptr;

    float inkLumLin = 0.0f;
    if (!imageColors && !paletteMode && nTones == 1)
        inkLumLin = ColorMath::linearLuminance(tones[0].color.rgb());

    // Float channel buffers in LINEAR light (source composited over white).
    std::vector<float> fr(size_t(w) * h), fg(size_t(w) * h), fb(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            float a = qAlpha(p) / 255.0f;
            size_t i = size_t(y) * w + x;
            fr[i] = ColorMath::srgbToLinear(qRed(p))   * a + (1.0f - a);
            fg[i] = ColorMath::srgbToLinear(qGreen(p)) * a + (1.0f - a);
            fb[i] = ColorMath::srgbToLinear(qBlue(p))  * a + (1.0f - a);
        }
    }

    // Serpentine raster scan (horizontal).
    for (int row = 0; row < h; ++row) {
        const bool rev = (row & 1) != 0;
        for (int ci = 0; ci < w; ++ci) {
            const int col = rev ? w - 1 - ci : ci;
            const size_t i = size_t(row) * w + col;

            float cr, cg, cb; QRgb outPx;
            quantizePixel(fr[i], fg[i], fb[i],
                          imageColors, tones, nTones, L, inkLumLin, pal,
                          cr, cg, cb, outPx);

            reinterpret_cast<QRgb*>(out.scanLine(row))[col] = outPx;

            const float er = (fr[i] - cr) * strength;
            const float eg = (fg[i] - cg) * strength;
            const float eb = (fb[i] - cb) * strength;
            if (er == 0.0f && eg == 0.0f && eb == 0.0f) continue;

            for (int t = 0; t < nTaps; ++t) {
                const int da = rev ? -taps[t].da : taps[t].da;
                const int nc = col + da;
                const int nr = row + taps[t].db;
                if (nc < 0 || nc >= w || nr < 0 || nr >= h) continue;
                const size_t ni = size_t(nr) * w + nc;
                fr[ni] += er * taps[t].w;
                fg[ni] += eg * taps[t].w;
                fb[ni] += eb * taps[t].w;
            }
        }
    }
}

// ============================================================
//  Ordered dithering
// ============================================================

void DitherRenderer::renderOrdered(const QImage& work, QImage& out,
                                   const DitherSettings& s)
{
    const int w = work.width();
    const int h = work.height();
    if (w == 0 || h == 0) return;

    const float strength    = qBound(0, s.strength, 100) / 100.0f;
    const bool  imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const bool  paletteMode = (s.tonal.mode == ToneMode::Palette);
    const auto  tones       = sortedTones(s.tonal.tones);
    const int   nTones      = int(tones.size());
    const int   L           = 2;

    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tones) : std::vector<ColorMath::PaletteEntry>{};
    const std::vector<ColorMath::PaletteEntry>* pal =
        (paletteMode && !palette.empty()) ? &palette : nullptr;

    // --- Prepare the threshold lookup for this algorithm --------
    // Returns a threshold in [0,1] for pixel (x,y).
    std::function<float(int,int)> threshold;

    if (s.algorithm == DitherAlgorithm::Bayer) {
        const int bn = (s.bayerSize == 2 || s.bayerSize == 4 ||
                        s.bayerSize == 8 || s.bayerSize == 16) ? s.bayerSize : 8;
        const std::vector<float> bayer = bayerMatrix(bn);
        threshold = [bayer, bn](int x, int y) -> float {
            return bayer[size_t(y % bn) * bn + (x % bn)];
        };

    } else if (s.algorithm == DitherAlgorithm::ClusteredDot) {
        // bayerSize ≤ 4 selects the 4×4 pattern; otherwise 8×8.
        if (s.bayerSize <= 4) {
            threshold = [](int x, int y) -> float {
                return (kClustered4[((y & 3) << 2) | (x & 3)] + 0.5f) / 16.0f;
            };
        } else {
            threshold = [](int x, int y) -> float {
                return (kClustered8[((y & 7) << 3) | (x & 7)] + 0.5f) / 64.0f;
            };
        }

    } else if (s.algorithm == DitherAlgorithm::BlueNoise) {
        const std::vector<float>& mask = blueNoiseMask();
        threshold = [&mask](int x, int y) -> float {
            return mask[size_t((y & 63) * 64 + (x & 63))];
        };

    } else { // VoidAndCluster
        const std::vector<float>& mask = voidClusterMask();
        threshold = [&mask](int x, int y) -> float {
            return mask[size_t((y & 31) * 32 + (x & 31))];
        };
    }

    for (int y = 0; y < h; ++y) {
        const QRgb* in  = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        QRgb*       dst = reinterpret_cast<QRgb*>(out.scanLine(y));

        for (int x = 0; x < w; ++x) {
            const QRgb p = in[x];
            const float alpha = qAlpha(p) / 255.0f;
            // Linear-light channels, composited over white.
            const float r  = ColorMath::srgbToLinear(qRed(p))   * alpha + (1.0f - alpha);
            const float g  = ColorMath::srgbToLinear(qGreen(p)) * alpha + (1.0f - alpha);
            const float bl = ColorMath::srgbToLinear(qBlue(p))  * alpha + (1.0f - alpha);

            // Compress threshold toward 0.5 as strength decreases.
            const float t = 0.5f + (threshold(x, y) - 0.5f) * strength;

            if (pal) {
                // Ordered dithering to a palette: bias the linear value by the
                // matrix, then snap to the nearest colour.
                const float bias = (threshold(x, y) - 0.5f) * strength * 0.5f;
                const float rr = qBound(0.0f, r  + bias, 1.0f);
                const float gg = qBound(0.0f, g  + bias, 1.0f);
                const float bb = qBound(0.0f, bl + bias, 1.0f);
                dst[x] = (*pal)[ColorMath::nearestPaletteIndex(
                             *pal, ColorMath::linearToOklab(rr, gg, bb))].out;

            } else if (imageColors) {
                auto quant = [&](float v) -> int {
                    float val  = qBound(0.0f, v, 1.0f) * (L - 1);
                    int   base = int(std::floor(val));
                    float frac = val - base;
                    int   q    = qBound(0, base + (frac > t ? 1 : 0), L - 1);
                    return ColorMath::linearToSrgb8(float(q) / float(L - 1));
                };
                dst[x] = qRgba(quant(r), quant(g), quant(bl), 255);

            } else if (nTones <= 1) {
                const float lumLin = 0.2126f * r + 0.7152f * g + 0.0722f * bl;
                if (lumLin < t) {
                    const QColor& c = tones.empty() ? QColor(Qt::black) : tones[0].color;
                    const int a = tones.empty() ? 255 : toneAlpha(tones[0]);
                    dst[x] = qRgba(c.red(), c.green(), c.blue(), a);
                } else {
                    dst[x] = qRgba(0, 0, 0, 0);
                }
            } else {
                // Tone selection stays perceptual so the level sliders hold.
                const float lum = ColorMath::perceptualLumaFromLinear(r, g, bl) * 255.0f;
                int seg = 0;
                while (seg < nTones - 2 && lum > float(tones[seg + 1].level)) ++seg;
                const float l0   = float(tones[seg].level);
                const float l1   = float(tones[seg + 1].level);
                const float span = l1 - l0;
                const float frac = (span > 0.5f) ? (lum - l0) / span : 0.5f;
                const ToneEntry& te = (frac > t) ? tones[seg + 1] : tones[seg];
                dst[x] = qRgba(te.color.red(), te.color.green(), te.color.blue(), toneAlpha(te));
            }
        }
    }
}

// ============================================================
//  Dot Diffusion (Knuth, 1987)
//  Pixels are processed according to a class matrix instead of raster
//  order. Error is diffused only to pixels not yet processed (higher
//  class value), producing unique clustered textures that are
//  substantially different from both Floyd and Bayer.
// ============================================================

void DitherRenderer::renderDotDiffusion(const QImage& work, QImage& out,
                                        const DitherSettings& s)
{
    const int w = work.width();
    const int h = work.height();
    if (w == 0 || h == 0) return;

    // 3×3 class matrix (Knuth's original).
    // Class 0 marks the dot centre; higher classes are processed later.
    //   5  2  6
    //   1  0  3
    //   7  4  8
    static const int kClass[9] = { 5, 2, 6,  1, 0, 3,  7, 4, 8 };

    // Group pixel indices by class (0..8) — O(N) bucket sort.
    std::vector<std::vector<int>> groups(9);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            groups[kClass[(y % 3) * 3 + (x % 3)]].push_back(y * w + x);

    const float strength    = qBound(0, s.strength, 100) / 100.0f;
    const bool  imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const bool  paletteMode = (s.tonal.mode == ToneMode::Palette);
    const auto  tones       = sortedTones(s.tonal.tones);
    const int   nTones      = int(tones.size());
    const int   L           = 2;

    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tones) : std::vector<ColorMath::PaletteEntry>{};
    const std::vector<ColorMath::PaletteEntry>* pal =
        (paletteMode && !palette.empty()) ? &palette : nullptr;

    float inkLumLin = 0.0f;
    if (!imageColors && !paletteMode && nTones == 1)
        inkLumLin = ColorMath::linearLuminance(tones[0].color.rgb());

    // Float channel buffers in LINEAR light (source composited over white).
    std::vector<float> fr(size_t(w) * h), fg(size_t(w) * h), fb(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            float a = qAlpha(p) / 255.0f;
            size_t i = size_t(y) * w + x;
            fr[i] = ColorMath::srgbToLinear(qRed(p))   * a + (1.0f - a);
            fg[i] = ColorMath::srgbToLinear(qGreen(p)) * a + (1.0f - a);
            fb[i] = ColorMath::srgbToLinear(qBlue(p))  * a + (1.0f - a);
        }
    }

    static const int dx8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int dy8[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

    for (int cls = 0; cls < 9; ++cls) {
        for (int idx : groups[cls]) {
            const int x = idx % w;
            const int y = idx / w;

            float cr, cg, cb; QRgb outPx;
            quantizePixel(fr[idx], fg[idx], fb[idx],
                          imageColors, tones, nTones, L, inkLumLin, pal,
                          cr, cg, cb, outPx);

            reinterpret_cast<QRgb*>(out.scanLine(y))[x] = outPx;

            const float er = (fr[idx] - cr) * strength;
            const float eg = (fg[idx] - cg) * strength;
            const float eb = (fb[idx] - cb) * strength;
            if (er == 0.0f && eg == 0.0f && eb == 0.0f) continue;

            // Count future neighbours (class > cls) before distributing.
            int nFuture = 0;
            for (int k = 0; k < 8; ++k) {
                const int nx = x + dx8[k], ny = y + dy8[k];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                if (kClass[(ny % 3) * 3 + (nx % 3)] > cls) ++nFuture;
            }
            if (nFuture == 0) continue;

            const float we = 1.0f / float(nFuture);
            for (int k = 0; k < 8; ++k) {
                const int nx = x + dx8[k], ny = y + dy8[k];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                if (kClass[(ny % 3) * 3 + (nx % 3)] > cls) {
                    const size_t ni = size_t(ny) * w + nx;
                    fr[ni] += er * we;
                    fg[ni] += eg * we;
                    fb[ni] += eb * we;
                }
            }
        }
    }
}

// ============================================================
//  Threshold — hard 1-bit cut by a level (no dithering).
//  With pixelSize (chunky cells) and cornerRadius (connected
//  rounding, applied by render()) it yields smooth black/white
//  poster shapes with rounded edges.
// ============================================================

void DitherRenderer::renderThreshold(const QImage& work, QImage& out,
                                     const DitherSettings& s)
{
    const int w = work.width();
    const int h = work.height();
    if (w == 0 || h == 0) return;

    const float thr         = qBound(0, s.threshold, 100) / 100.0f;
    const bool  imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const auto  tones       = sortedTones(s.tonal.tones);

    // Ink = darkest tone (lowest level); paper = transparent (background shows).
    const QColor inkColor = tones.empty() ? QColor(Qt::black) : tones.front().color;
    const int    inkA     = tones.empty() ? 255 : toneAlpha(tones.front());
    const QRgb   inkPx    = qRgba(inkColor.red(), inkColor.green(), inkColor.blue(), inkA);

    for (int y = 0; y < h; ++y) {
        const QRgb* in  = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        QRgb*       dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb  p = in[x];
            const float a = qAlpha(p) / 255.0f;
            const float r = ColorMath::srgbToLinear(qRed(p))   * a + (1.0f - a);
            const float g = ColorMath::srgbToLinear(qGreen(p)) * a + (1.0f - a);
            const float b = ColorMath::srgbToLinear(qBlue(p))  * a + (1.0f - a);
            const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;

            if (lum < thr)
                dst[x] = imageColors ? qRgba(0, 0, 0, 255) : inkPx;
            else
                dst[x] = imageColors ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 0);
        }
    }
}
