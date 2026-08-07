#include "HalftoneRenderer.h"
#include "CellSample.h"
#include "ColorMath.h"
#include "GridGenerator.h"

#include <QPainterPath>
#include <QTransform>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Ink colors, flood (flat coverage offset), gain (proportional coverage
// gain), grid noise and the "Ink" joined style are modeled after
// paper-design/shaders' halftone-cmyk shader (Apache-2.0,
// https://github.com/paper-design/shaders).
//
// One job = one rotated screen. In CMYK mode there are four (one per process
// ink); in tonal mode (shared Fill system) there is one per tone, its
// coverage peaking where the cell's luminosity matches the tone's level
// (duotone/tritone/N-ink printing).
struct ChannelJob {
    const QImage*           rgb    = nullptr;
    const HalftoneSettings* params = nullptr;
    float                   angle  = 0.0f;
    int                     chan   = 0;        // CMYK: 0=C 1=M 2=Y 3=K; also the jitter salt
    QColor                  ink;
    float                   flood  = 0.0f;     // -1..1
    float                   gain   = 0.0f;     // -1..1
    bool                    cmyk   = true;
    bool                    single = false;    // tonal with one tone: coverage = darkness
    float                   level  = 128.0f;   // tonal: this tone's luminosity anchor
    float                   levelLo = -1.0f;   // neighbour anchors (-1 / 256 = none)
    float                   levelHi = 256.0f;
    GridSamplesPtr          samples;           // generated on the calling thread
    QImage                  canvas;            // transparent ARGB32_Premultiplied
};

float channelValue(const ColorMath::Cmyk& c, int chan)
{
    switch (chan) {
        case 0:  return c.c;
        case 1:  return c.m;
        case 2:  return c.y;
        default: return c.k;
    }
}

// Coverage of this job's ink over the sampling cell, 0..1. Linear-light
// averaging first (matches Dot Grid); CMYK converts the average once, tonal
// jobs take the perceptual luma (so Fill "level" anchors keep their meaning)
// and weigh it triangularly against the neighbouring tone levels.
// Cell geometry + averaging come from core/CellSample.h.
float cellCoverage(const ChannelJob& job, int cx, int cy, int cw, int ch)
{
    const CellSample::Mean m = CellSample::mean(*job.rgb, cx, cy, cw, ch);
    if (m.empty) return 0.0f;

    if (job.cmyk)
        return channelValue(ColorMath::rgbToCmyk(m.rLin, m.gLin, m.bLin), job.chan);

    const float L = m.tonePercPerChannel() * 255.0f;
    if (job.single) return 1.0f - L / 255.0f;   // classic single-ink screen
    if (L <= job.level) {
        if (job.levelLo < -0.5f) return 1.0f;               // darkest tone: full below its anchor
        if (L <= job.levelLo) return 0.0f;
        return (L - job.levelLo) / (job.level - job.levelLo);
    }
    if (job.levelHi > 255.5f) return 1.0f;                  // lightest tone: full above its anchor
    if (L >= job.levelHi) return 0.0f;
    return (job.levelHi - L) / (job.levelHi - job.level);
}

// Deterministic per-cell hash → two uniform floats (paper-design's
// u_gridNoise: every cell drifts off its lattice point by a stable random
// offset, decorrelated across channels via `salt`).
inline void cellJitter(float sx, float sy, int salt, float amount,
                       float& dx, float& dy)
{
    unsigned h = static_cast<unsigned>(qRound(sx) * 73856093
                                     ^ qRound(sy) * 19349663
                                     ^ (salt + 1) * 83492791);
    h ^= (h >> 16); h *= 0x45d9f3b; h ^= (h >> 16);
    dx = ((h & 0xFFFF) / 65535.0f - 0.5f) * amount;
    dy = (((h >> 16) & 0xFFFF) / 65535.0f - 0.5f) * amount;
}

// Hash-based 2D value noise (paper-design's valueNoise, single channel):
// smooth bilinear interpolation of a per-lattice-point hash. Used for the
// paper-grain overlay and the organic Ink edges.
inline float noiseHash(int x, int y)
{
    unsigned h = static_cast<unsigned>(x * 73856093 ^ y * 19349663);
    h ^= (h >> 16); h *= 0x45d9f3b; h ^= (h >> 16);
    return (h & 0xFFFFFF) / 16777215.0f;
}

float valueNoise(float x, float y)
{
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = noiseHash(xi, yi),     b = noiseHash(xi + 1, yi);
    const float c = noiseHash(xi, yi + 1), d = noiseHash(xi + 1, yi + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// ~2.2px paper-fibre feature size (paper-design samples its noise texture at
// a comparable density).
constexpr float kGrainScale = 0.45f;
constexpr float kMinEdgeSoftness = 0.02f;

// Rows of the Ink style's float accumulation field held at once (see
// renderChannelInk): keeps it a few MB per channel instead of the whole frame.
constexpr int kInkBandHeight = 256;

// "Ink" style — CPU port of paper-design/shaders' "ink" type: every cell
// splats a soft radial mask whose reach exceeds the cell (r ∝ coverage, up
// to ~1.15× spacing), the masks ACCUMULATE in a float field, and one
// threshold at 0.5 cuts the union — neighbouring dots fuse into organic
// blobs. `softness` widens the threshold ramp (fuzzy edges). Tonal accuracy
// is deliberately traded for the look; gamma and flood/gain steer it back.
void renderChannelInk(ChannelJob& job)
{
    const HalftoneSettings& p    = *job.params;
    const int imgW = job.rgb->width(), imgH = job.rgb->height();
    const float sp       = qMax(2.0f, p.spacing);
    const int   cellPx   = qMax(1, qRound(sp));
    const float invGamma = 1.0f / qBound(0.1f, p.gamma, 5.0f);
    const float gn       = qBound(0.0f, p.gridNoise, 1.0f) * sp;

    // Pass 1 — resolve every splat's centre and radius. cellCoverage() is the
    // expensive part (it averages the whole sampling cell), so it runs exactly
    // once per sample here rather than once per sample per band below.
    struct Splat { float px, py, r; };
    std::vector<Splat> splats;
    splats.reserve(job.samples->size());

    for (const GridSample& s : *job.samples) {
        float jx = 0, jy = 0;
        if (gn > 0.0f) cellJitter(s.x, s.y, job.chan, gn, jx, jy);
        const float px = s.x + jx, py = s.y + jy;

        int cx, cy, cw, ch;
        CellSample::around(px, py, cellPx, imgW, imgH, cx, cy, cw, ch);
        float cov = cellCoverage(job, cx, cy, cw, ch);
        cov = std::pow(qBound(0.0f, cov, 1.0f), invGamma);
        cov = qBound(0.0f, cov * (1.0f + job.gain) + job.flood, 1.0f);
        if (cov <= 0.005f) continue;

        const float r = 1.15f * cov * sp;   // reaches past the cell → blobs merge
        if (r < 0.5f) continue;
        splats.push_back({ px, py, r });
    }

    // Pass 2 — accumulate + threshold one horizontal band at a time. The field
    // is only ever bandH rows tall instead of the whole image: at frame
    // resolution the full-image version was tens of MB per channel, and four
    // channels render concurrently. Each output row depends only on splats
    // within rMax of it, so banding is exactly equivalent to one big field.
    const int bandH  = qMax(1, qMin(imgH, kInkBandHeight));
    const int nBands = (imgH + bandH - 1) / bandH;

    // Bucket splats by the bands they can reach, so a band doesn't rescan the
    // whole list.
    const size_t nBandSlots = size_t(nBands);
    std::vector<std::vector<int>> byBand(nBandSlots);
    for (int i = 0; i < int(splats.size()); ++i) {
        const Splat& s = splats[size_t(i)];
        const int b0 = qBound(0, int(std::floor((s.py - s.r) / bandH)), nBands - 1);
        const int b1 = qBound(0, int(std::floor((s.py + s.r) / bandH)), nBands - 1);
        for (int b = b0; b <= b1; ++b) byBand[size_t(b)].push_back(i);
    }

    // Single threshold over the accumulated field → alpha of this channel's
    // ink. Grain perturbs the field before the cut (paper-design's
    // grainMixer): the blob edges wobble organically instead of staying
    // mathematically smooth.
    const float soft = qBound(0.0f, p.softness, 1.0f);
    const float edgeSoft = qMax(soft, kMinEdgeSoftness);
    const float grain = qBound(0.0f, p.grain, 1.0f);
    const float lo = 0.5f - 0.5f * edgeSoft;
    const float hi = 0.5f + 0.5f * edgeSoft + 0.01f;
    const float invW = 1.0f / (hi - lo);
    const int ir = job.ink.red(), ig = job.ink.green(), ib = job.ink.blue();
    const float inkA = job.ink.alphaF();

    std::vector<float> field(size_t(imgW) * size_t(bandH));

    for (int b = 0; b < nBands; ++b) {
        const int bandTop = b * bandH;
        const int bandBot = qMin(imgH, bandTop + bandH);   // exclusive
        std::fill(field.begin(), field.begin() + size_t(imgW) * (bandBot - bandTop), 0.0f);

        for (int idx : byBand[size_t(b)]) {
            const Splat& s = splats[size_t(idx)];
            const int x0 = qMax(0, int(s.px - s.r));
            const int x1 = qMin(imgW - 1, int(s.px + s.r) + 1);
            const int y0 = qMax(bandTop, int(s.py - s.r));
            const int y1 = qMin(bandBot - 1, int(s.py + s.r) + 1);
            const float invR = 1.0f / s.r;
            for (int y = y0; y <= y1; ++y) {
                float* row = &field[size_t(y - bandTop) * imgW];
                const float dy = y + 0.5f - s.py;
                for (int x = x0; x <= x1; ++x) {
                    const float dx = x + 0.5f - s.px;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d >= s.r) continue;
                    const float t = 1.0f - d * invR;          // 1 centre → 0 at r
                    row[x] += t * t * (3.0f - 2.0f * t);      // smoothstep falloff
                }
            }
        }

        for (int y = bandTop; y < bandBot; ++y) {
            QRgb* out = reinterpret_cast<QRgb*>(job.canvas.scanLine(y));
            const float* row = &field[size_t(y - bandTop) * imgW];
            for (int x = 0; x < imgW; ++x) {
                float f = row[x];
                if (grain > 0.005f)
                    f += (valueNoise(x * kGrainScale, y * kGrainScale) - 0.5f) * grain * 0.7f;
                float t = (f - lo) * invW;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                const float a = t * t * (3.0f - 2.0f * t) * inkA;
                if (a <= 0.003f) continue;
                out[x] = qPremultiply(qRgba(ir, ig, ib, int(a * 255.0f + 0.5f)));
            }
        }
    }
}

// One screen, painter-drawn styles:
//   Round:  ALWAYS a circle, r = sp·√(cov/π) — never inverts into a
//           cell-with-holes (per user request: the "+"-shaped remnants of the
//           Euclidean inversion read as alien). At cov=1 r≈0.564·sp: circles
//           overlap their neighbours and only small paper specks survive at
//           the lattice corners — that's the accepted look.
//   Square: side = sp·√cov — tiles seamlessly at cov=1, area-exact
//   Line:   thickness = sp·cov, length sp×1.1 so segments fuse into lines
// Softness feathers each dot's own edge (crisp core + gradient rim scaled to
// the dot, like the source shader) — NOT a raster blur: Round gets a radial
// gradient, Square/Line approximate it with three concentric shells.
void renderChannel(ChannelJob& job)
{
    if (job.params->dotShape == ScreenDotShape::Ink) {
        renderChannelInk(job);
        return;
    }
    const HalftoneSettings& p = *job.params;
    const int imgW = job.rgb->width(), imgH = job.rgb->height();
    const float sp       = qMax(2.0f, p.spacing);
    const int   cellPx   = qMax(1, qRound(sp));
    const float invGamma = 1.0f / qBound(0.1f, p.gamma, 5.0f);
    const float gn       = qBound(0.0f, p.gridNoise, 1.0f) * sp;
    const float soft     = qBound(0.0f, p.softness, 1.0f);
    const float edgeSoft = qMax(soft, kMinEdgeSoftness);
    const auto& samples  = *job.samples;

    QPainter painter(&job.canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(job.ink);

    // Feather approximation for path shapes: faint outer shells first, solid
    // core last (alpha stacks toward 1 at the centre).
    struct Shell { float grow; float alpha; };
    const Shell shells[3] = { { 0.30f, 0.25f }, { 0.15f, 0.55f }, { 0.0f, 1.0f } };

    for (const GridSample& s : samples) {
        float jx = 0, jy = 0;
        if (gn > 0.0f) cellJitter(s.x, s.y, job.chan, gn, jx, jy);
        const float px = s.x + jx, py = s.y + jy;

        int cx, cy, cw, ch;
        CellSample::around(px, py, cellPx, imgW, imgH, cx, cy, cw, ch);
        float cov = cellCoverage(job, cx, cy, cw, ch);
        cov = std::pow(qBound(0.0f, cov, 1.0f), invGamma);
        cov = qBound(0.0f, cov * (1.0f + job.gain) + job.flood, 1.0f);
        if (p.grain > 0.005f) {
            // Same paper-fibre noise as the Ink style, applied per-dot
            // instead of to an accumulated field: nudges each dot's own
            // coverage so size varies slightly cell-to-cell.
            const float n = valueNoise(px * kGrainScale, py * kGrainScale) - 0.5f;
            cov = qBound(0.0f, cov + n * p.grain * 0.7f, 1.0f);
        }
        if (cov <= 0.001f) continue;

        QTransform t;
        t.translate(px, py);
        t.rotate(job.angle);   // shapes stay aligned with their screen lattice

        switch (p.dotShape) {
        case ScreenDotShape::Round: {
            const float r = sp * std::sqrt(cov / float(M_PI));
            if (r < 0.25f) break;
            if (edgeSoft <= 0.01f) {
                painter.drawEllipse(QPointF(px, py), r, r);
            } else {
                // Crisp core + gradient rim proportional to the dot's radius.
                const float R = r * (1.0f + 0.35f * edgeSoft);
                QRadialGradient grad(QPointF(px, py), R);
                QColor edge = job.ink; edge.setAlpha(0);
                grad.setColorAt(qBound(0.0, 1.0 - 0.75 * double(edgeSoft), 0.99), job.ink);
                grad.setColorAt(1.0, edge);
                painter.setBrush(grad);
                painter.drawEllipse(QPointF(px, py), R, R);
                painter.setBrush(job.ink);
            }
            break;
        }
        case ScreenDotShape::Square: {
            const float half = sp * std::sqrt(cov) * 0.5f;
            if (half < 0.2f) break;
            for (const Shell& sh : shells) {
                if (edgeSoft <= 0.01f && sh.alpha < 1.0f) continue;
                const float hh = half * (1.0f + sh.grow * edgeSoft);
                QColor c = job.ink;
                c.setAlphaF(c.alphaF() * sh.alpha);
                painter.setBrush(c);
                QPainterPath sq;
                sq.addRect(QRectF(-hh, -hh, hh * 2.0f, hh * 2.0f));
                painter.drawPath(t.map(sq));
            }
            painter.setBrush(job.ink);
            break;
        }
        case ScreenDotShape::Line: {
            const float th = sp * cov;
            if (th < 0.2f) break;
            for (const Shell& sh : shells) {
                if (edgeSoft <= 0.01f && sh.alpha < 1.0f) continue;
                const float t2 = th * (1.0f + sh.grow * edgeSoft);
                QColor c = job.ink;
                c.setAlphaF(c.alphaF() * sh.alpha);
                painter.setBrush(c);
                QPainterPath ln;
                ln.addRect(QRectF(-sp * 0.55f, -t2 * 0.5f, sp * 1.1f, t2));
                painter.drawPath(t.map(ln));
            }
            painter.setBrush(job.ink);
            break;
        }
        case ScreenDotShape::Ink: break;   // handled by renderChannelInk above
        }
    }
}

} // namespace

void HalftoneRenderer::render(const QImage& input, QPainter& output,
                              const HalftoneSettings& params)
{
    if (input.isNull()) return;
    const int imgW = input.width();
    const int imgH = input.height();

    const QImage rgb = (input.format() == QImage::Format_RGB32)
                       ? input
                       : input.convertToFormat(QImage::Format_RGB32);

    const bool useCmyk = (params.tonal.mode == ToneMode::ImageColors)
                      || params.tonal.tones.empty();

    QVector<ChannelJob> jobs;
    if (useCmyk) {
        const float  angles[4] = { params.angleC, params.angleM, params.angleY, params.angleK };
        const QColor inks[4]   = { params.inkC, params.inkM, params.inkY, params.inkK };
        const float  floods[4] = { params.floodC, params.floodM, params.floodY, params.floodK };
        const float  gains[4]  = { params.gainC, params.gainM, params.gainY, params.gainK };
        jobs.resize(4);
        for (int i = 0; i < 4; ++i) {
            ChannelJob& j = jobs[i];
            j.chan  = i;
            j.angle = angles[i];
            j.ink   = inks[i];
            j.flood = qBound(-1.0f, floods[i], 1.0f);
            j.gain  = qBound(-1.0f, gains[i], 1.0f);
        }
    } else {
        // Tonal Fill: one screen per tone, darkest first (so the darkest ink
        // gets the K angle, then C/M/Y, then evenly spread extras).
        std::vector<ToneEntry> tones = params.tonal.tones;
        std::sort(tones.begin(), tones.end(),
                  [](const ToneEntry& a, const ToneEntry& b) { return a.level < b.level; });
        const int n = int(tones.size());
        const float angles[4] = { params.angleK, params.angleC, params.angleM, params.angleY };
        jobs.resize(n);
        for (int i = 0; i < n; ++i) {
            ChannelJob& j = jobs[i];
            j.cmyk   = false;
            j.single = (n == 1);
            j.chan   = i;   // jitter salt
            j.level   = float(tones[size_t(i)].level);
            j.levelLo = (i > 0)     ? float(tones[size_t(i) - 1].level) : -1.0f;
            j.levelHi = (i < n - 1) ? float(tones[size_t(i) + 1].level) : 256.0f;
            QColor ink = tones[size_t(i)].color;
            ink.setAlphaF(qBound(0.0f, tones[size_t(i)].opacity, 1.0f));
            j.ink   = ink;
            j.flood = qBound(-1.0f, tones[size_t(i)].flood, 1.0f);
            j.gain  = qBound(-1.0f, tones[size_t(i)].gain, 1.0f);
            j.angle = (i < 4) ? angles[i]
                              : std::fmod(params.angleK + float(i) * 37.5f, 180.0f);
        }
    }

    for (ChannelJob& j : jobs) {
        j.rgb    = &rgb;
        j.params = &params;
        // Generate on this thread: GridGenerator's single-entry cache would be
        // thrashed (and contended) by concurrent calls.
        GridSettings gs;
        gs.type     = GridType::Square;
        gs.spacing  = qMax(2.0f, params.spacing);
        gs.rotation = j.angle;
        j.samples = GridGenerator::generate(gs, imgW, imgH);
        j.canvas  = QImage(imgW, imgH, QImage::Format_ARGB32_Premultiplied);
        j.canvas.fill(Qt::transparent);
    }

    QtConcurrent::blockingMap(jobs, renderChannel);

    // Multiply the screens over the paper colour (CMYK order: C→M→Y→K;
    // tonal order: dark→light — multiplicative, so order only matters
    // visually for semi-transparent inks).
    QImage paper(imgW, imgH, QImage::Format_ARGB32_Premultiplied);
    paper.fill(params.paper.isValid() ? params.paper : QColor(Qt::white));
    {
        QPainter pp(&paper);
        pp.setCompositionMode(QPainter::CompositionMode_Multiply);
        for (const ChannelJob& j : jobs)
            pp.drawImage(0, 0, j.canvas);
    }

    // Grain only perturbs the dot-edge field earlier (organic displacement);
    // it no longer paints a visible speckle overlay here — high grain values
    // were making the halftone too noisy without adding useful texture.

    output.save();
    output.setOpacity(output.opacity() * qBound(0.0f, params.opacity, 1.0f));
    output.drawImage(0, 0, paper);
    output.restore();
}

int HalftoneRenderer::estimateDotCount(const QImage& input, const HalftoneSettings& params)
{
    if (input.isNull()) return 0;
    const float sp = qMax(2.0f, params.spacing);
    const int screens = (params.tonal.mode == ToneMode::ImageColors || params.tonal.tones.empty())
                      ? 4 : int(params.tonal.tones.size());
    return int(float(screens) * (input.width() / sp + 1.0f) * (input.height() / sp + 1.0f));
}
