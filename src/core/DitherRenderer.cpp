#include "DitherRenderer.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline float lumOf(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Tones sorted dark → light by level (ascending).
std::vector<ToneEntry> sortedTones(const std::vector<ToneEntry>& tones)
{
    std::vector<ToneEntry> t = tones;
    std::sort(t.begin(), t.end(),
              [](const ToneEntry& a, const ToneEntry& b) { return a.level < b.level; });
    return t;
}

// ── Ordered threshold patterns ───────────────────────────────

std::vector<float> bayerMatrix(int n)
{
    // Recursive construction; n must be a power of two.
    std::vector<int> m = { 0 };
    int size = 1;
    while (size < n) {
        std::vector<int> nm(size_t(size) * size * 4);
        int s2 = size * 2;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int v = m[size_t(y) * size + x] * 4;
                nm[size_t(y) * s2 + x]                  = v;
                nm[size_t(y) * s2 + x + size]           = v + 2;
                nm[size_t(y + size) * s2 + x]           = v + 3;
                nm[size_t(y + size) * s2 + x + size]    = v + 1;
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

// Classic clustered-dot 8×8 screen ("heavy" blobs).
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

// ── Error-diffusion kernels ──────────────────────────────────

struct KTap { int da, db; float w; };   // da: along scan axis, db: rows below

const KTap kFloydSteinberg[] = {
    { 1, 0, 7.f/16 }, { -1, 1, 3.f/16 }, { 0, 1, 5.f/16 }, { 1, 1, 1.f/16 }
};
const KTap kJJN[] = {
    { 1, 0, 7.f/48 }, { 2, 0, 5.f/48 },
    { -2, 1, 3.f/48 }, { -1, 1, 5.f/48 }, { 0, 1, 7.f/48 }, { 1, 1, 5.f/48 }, { 2, 1, 3.f/48 },
    { -2, 2, 1.f/48 }, { -1, 2, 3.f/48 }, { 0, 2, 5.f/48 }, { 1, 2, 3.f/48 }, { 2, 2, 1.f/48 }
};
const KTap kBurkes[] = {
    { 1, 0, 8.f/32 }, { 2, 0, 4.f/32 },
    { -2, 1, 2.f/32 }, { -1, 1, 4.f/32 }, { 0, 1, 8.f/32 }, { 1, 1, 4.f/32 }, { 2, 1, 2.f/32 }
};
const KTap kAtkinson[] = {
    { 1, 0, 1.f/8 }, { 2, 0, 1.f/8 },
    { -1, 1, 1.f/8 }, { 0, 1, 1.f/8 }, { 1, 1, 1.f/8 },
    { 0, 2, 1.f/8 }
};
const KTap kLine[] = {
    { 1, 0, 0.95f }
};

} // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

bool DitherRenderer::isOrdered(DitherAlgorithm a)
{
    switch (a) {
        case DitherAlgorithm::Bayer:
        case DitherAlgorithm::DispersedModulation:
        case DitherAlgorithm::HeavyModulation:
        case DitherAlgorithm::CircuitModulation:
            return true;
        default:
            return false;
    }
}

QImage DitherRenderer::render(const QImage& input, const DitherSettings& s)
{
    if (input.isNull()) return {};

    const int ps = qBound(1, s.pixelSize, 32);
    QImage work = input;
    if (ps > 1) {
        work = input.scaled(qMax(1, input.width() / ps),
                            qMax(1, input.height() / ps),
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    work = work.convertToFormat(QImage::Format_ARGB32);

    QImage out(work.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    if (isOrdered(s.algorithm)) renderOrdered(work, out, s);
    else                        renderDiffusion(work, out, s);

    if (ps > 1)
        out = out.scaled(input.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

    return out;
}

// ---------------------------------------------------------------------------
// Error diffusion
// ---------------------------------------------------------------------------

void DitherRenderer::renderDiffusion(const QImage& work, QImage& out,
                                     const DitherSettings& s)
{
    const int w = work.width();
    const int h = work.height();
    if (w == 0 || h == 0) return;

    const KTap* taps   = kFloydSteinberg;
    int         nTaps  = 4;
    bool        vertical = false;

    switch (s.algorithm) {
        case DitherAlgorithm::FloydSteinberg:    taps = kFloydSteinberg; nTaps = 4;  break;
        case DitherAlgorithm::JarvisJudiceNinke: taps = kJJN;            nTaps = 12; break;
        case DitherAlgorithm::Burkes:            taps = kBurkes;         nTaps = 7;  break;
        case DitherAlgorithm::Atkinson:          taps = kAtkinson;       nTaps = 6;  break;
        case DitherAlgorithm::RowModulation:     taps = kLine;           nTaps = 1;  break;
        case DitherAlgorithm::ColumnModulation:  taps = kLine;           nTaps = 1; vertical = true; break;
        default: break;
    }

    const float strength    = qBound(0, s.strength, 100) / 100.0f;
    const bool  imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const auto  tones       = sortedTones(s.tonal.tones);
    const int   nTones      = int(tones.size());
    const int   L           = qBound(2, s.levels, 8);
    const float step        = 255.0f / float(L - 1);

    // Float channel buffers (source composited over white).
    std::vector<float> fr(size_t(w) * h), fg(size_t(w) * h), fb(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            float a = qAlpha(p) / 255.0f;
            size_t i = size_t(y) * w + x;
            fr[i] = qRed(p)   * a + 255.0f * (1.0f - a);
            fg[i] = qGreen(p) * a + 255.0f * (1.0f - a);
            fb[i] = qBlue(p)  * a + 255.0f * (1.0f - a);
        }
    }

    // Single-tone luminosity threshold: midpoint between ink and paper.
    float inkLum = 0.0f;
    if (!imageColors && nTones == 1)
        inkLum = lumOf(tones[0].color.red(), tones[0].color.green(), tones[0].color.blue());

    const int outer = vertical ? w : h;
    const int inner = vertical ? h : w;

    for (int b = 0; b < outer; ++b) {
        const bool reverse = (b & 1) != 0;   // serpentine
        for (int ai = 0; ai < inner; ++ai) {
            const int a = reverse ? inner - 1 - ai : ai;
            const int x = vertical ? b : a;
            const int y = vertical ? a : b;
            const size_t i = size_t(y) * w + x;

            const float r = fr[i], g = fg[i], bl = fb[i];
            float cr, cg, cb;          // chosen color value (for error)
            QRgb  outPx;

            if (imageColors) {
                cr = qBound(0.0f, std::round(r / step) * step, 255.0f);
                cg = qBound(0.0f, std::round(g / step) * step, 255.0f);
                cb = qBound(0.0f, std::round(bl / step) * step, 255.0f);
                outPx = qRgba(int(cr), int(cg), int(cb), 255);
            } else if (nTones <= 1) {
                const float lum = lumOf(r, g, bl);
                const bool ink = std::fabs(lum - inkLum) <= std::fabs(lum - 255.0f);
                if (ink) {
                    const QColor& c = tones.empty() ? QColor(Qt::black) : tones[0].color;
                    cr = c.red(); cg = c.green(); cb = c.blue();
                    outPx = qRgba(c.red(), c.green(), c.blue(), 255);
                } else {
                    cr = 255; cg = 255; cb = 255;
                    outPx = qRgba(0, 0, 0, 0);   // paper → background shows
                }
            } else {
                int best = 0;
                float bestD = 1e12f;
                for (int t = 0; t < nTones; ++t) {
                    const QColor& c = tones[t].color;
                    float dr = r - c.red(), dg = g - c.green(), db = bl - c.blue();
                    float d = dr*dr + dg*dg + db*db;
                    if (d < bestD) { bestD = d; best = t; }
                }
                const QColor& c = tones[best].color;
                cr = c.red(); cg = c.green(); cb = c.blue();
                outPx = qRgba(c.red(), c.green(), c.blue(), 255);
            }

            reinterpret_cast<QRgb*>(out.scanLine(y))[x] = outPx;

            const float er = (r  - cr) * strength;
            const float eg = (g  - cg) * strength;
            const float eb = (bl - cb) * strength;
            if (er == 0.0f && eg == 0.0f && eb == 0.0f) continue;

            for (int t = 0; t < nTaps; ++t) {
                int da = reverse ? -taps[t].da : taps[t].da;
                int na = a + da;
                int nb = b + taps[t].db;
                if (na < 0 || na >= inner || nb < 0 || nb >= outer) continue;
                int nx = vertical ? nb : na;
                int ny = vertical ? na : nb;
                size_t ni = size_t(ny) * w + nx;
                fr[ni] += er * taps[t].w;
                fg[ni] += eg * taps[t].w;
                fb[ni] += eb * taps[t].w;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Ordered dithering
// ---------------------------------------------------------------------------

void DitherRenderer::renderOrdered(const QImage& work, QImage& out,
                                   const DitherSettings& s)
{
    const int w = work.width();
    const int h = work.height();
    if (w == 0 || h == 0) return;

    const float strength    = qBound(0, s.strength, 100) / 100.0f;
    const bool  imageColors = (s.tonal.mode == ToneMode::ImageColors);
    const auto  tones       = sortedTones(s.tonal.tones);
    const int   nTones      = int(tones.size());
    const int   L           = qBound(2, s.levels, 8);

    int bayerN = 8;
    std::vector<float> bayer;
    if (s.algorithm == DitherAlgorithm::Bayer) {
        bayerN = (s.bayerSize == 2 || s.bayerSize == 4 ||
                  s.bayerSize == 8 || s.bayerSize == 16) ? s.bayerSize : 8;
        bayer = bayerMatrix(bayerN);
    }

    auto threshold = [&](int x, int y) -> float {
        switch (s.algorithm) {
            case DitherAlgorithm::Bayer:
                return bayer[size_t(y % bayerN) * bayerN + (x % bayerN)];
            case DitherAlgorithm::DispersedModulation: {
                // Interleaved gradient noise
                float v = std::fmod(0.06711056f * x + 0.00583715f * y, 1.0f);
                return std::fmod(52.9829189f * v, 1.0f);
            }
            case DitherAlgorithm::HeavyModulation:
                return (kClustered8[((y & 7) << 3) | (x & 7)] + 0.5f) / 64.0f;
            case DitherAlgorithm::CircuitModulation:
                return (((x ^ y) & 15) + 0.5f) / 16.0f;
            default:
                return 0.5f;
        }
    };

    for (int y = 0; y < h; ++y) {
        const QRgb* in  = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        QRgb*       dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = in[x];
            const float a = qAlpha(p) / 255.0f;
            const float r  = qRed(p)   * a + 255.0f * (1.0f - a);
            const float g  = qGreen(p) * a + 255.0f * (1.0f - a);
            const float bl = qBlue(p)  * a + 255.0f * (1.0f - a);

            // Strength compresses the threshold toward 0.5 (no dither).
            const float t = 0.5f + (threshold(x, y) - 0.5f) * strength;

            if (imageColors) {
                auto quant = [&](float v) -> int {
                    float val  = v / 255.0f * (L - 1);
                    int   base = int(std::floor(val));
                    float frac = val - base;
                    int   q    = base + (frac > t ? 1 : 0);
                    return clamp255(qRound(qBound(0, q, L - 1) * 255.0f / (L - 1)));
                };
                dst[x] = qRgba(quant(r), quant(g), quant(bl), 255);
            } else if (nTones <= 1) {
                const float lum01 = lumOf(r, g, bl) / 255.0f;
                if (lum01 < t) {
                    const QColor& c = tones.empty() ? QColor(Qt::black) : tones[0].color;
                    dst[x] = qRgba(c.red(), c.green(), c.blue(), 255);
                } else {
                    dst[x] = qRgba(0, 0, 0, 0);
                }
            } else {
                const float lum = lumOf(r, g, bl);
                const float lo  = float(tones.front().level);
                const float hi  = float(tones.back().level);
                float v = qBound(lo, lum, hi);

                int seg = 0;
                while (seg < nTones - 2 && v > float(tones[seg + 1].level)) ++seg;
                const float l0 = float(tones[seg].level);
                const float l1 = float(tones[seg + 1].level);
                const float span = l1 - l0;
                const float frac = (span > 0.5f) ? (v - l0) / span : 0.5f;

                const QColor& c = (frac > t) ? tones[seg + 1].color : tones[seg].color;
                dst[x] = qRgba(c.red(), c.green(), c.blue(), 255);
            }
        }
    }
}
