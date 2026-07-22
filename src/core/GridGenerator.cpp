#include "GridGenerator.h"

#include <QTransform>
#include <QPointF>
#include <QMutex>
#include <QtMath>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Transform: grid space → image space
// ---------------------------------------------------------------------------
//
//   M = T(C) · R(rotation) · S · T(-C)
//   S = R(eff) · scale(stretchFactor, 1) · R(-eff)
//
// where eff = stretchAngle (+ rotation when followGridRotation), and C is the
// image centre. QTransform applies the *last* call first when mapping a point,
// so the call order below reproduces M.

namespace {

QTransform buildTransform(const GridSettings& g, int imgW, int imgH)
{
    const double cx  = imgW * 0.5;
    const double cy  = imgH * 0.5;
    const double eff = g.followGridRotation ? double(g.rotation) + double(g.stretchAngle)
                                            : double(g.stretchAngle);
    const double f   = std::max(0.01, double(g.stretchFactor));

    QTransform t;
    t.translate(cx, cy);
    t.rotate(g.rotation);
    t.rotate(eff);
    t.scale(f, 1.0);
    t.rotate(-eff);
    t.translate(-cx, -cy);
    return t;
}

struct BBox { double x0, y0, x1, y1; };

// Grid-space bounding box that, once transformed, covers the whole image.
BBox gridBounds(const QTransform& inv, int imgW, int imgH)
{
    const QPointF c[4] = {
        inv.map(QPointF(0,        0)),
        inv.map(QPointF(imgW,     0)),
        inv.map(QPointF(imgW, imgH)),
        inv.map(QPointF(0,    imgH)),
    };
    BBox b { c[0].x(), c[0].y(), c[0].x(), c[0].y() };
    for (int i = 1; i < 4; ++i) {
        b.x0 = std::min(b.x0, c[i].x());
        b.y0 = std::min(b.y0, c[i].y());
        b.x1 = std::max(b.x1, c[i].x());
        b.y1 = std::max(b.y1, c[i].y());
    }
    return b;
}

// ── Dot layouts (Square / Hexagonal) — culled to the image + margin ────────

void genSquare(const GridSettings& g, const QTransform& t, const BBox& b,
               int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp     = std::max(2.0f, g.spacing);
    const double margin = sp;
    // Lattice phase anchored on the image centre (a sample always lands
    // exactly there) instead of the top-left corner — leftover space at the
    // edges then splits evenly on both sides instead of piling up on one.
    const double cx = imgW * 0.5, cy = imgH * 0.5;
    const int i0 = int(std::floor((b.x0 - cx) / sp)) - 1, i1 = int(std::ceil((b.x1 - cx) / sp)) + 1;
    const int j0 = int(std::floor((b.y0 - cy) / sp)) - 1, j1 = int(std::ceil((b.y1 - cy) / sp)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gy = cy + j * sp;
        for (int i = i0; i <= i1; ++i) {
            const double gx = cx + i * sp;
            const QPointF p = t.map(QPointF(gx, gy));
            if (p.x() < -margin || p.x() > imgW + margin ||
                p.y() < -margin || p.y() > imgH + margin) continue;
            out.push_back({ float(p.x()), float(p.y()), 0.0f, j });
        }
    }
}

void genHex(const GridSettings& g, const QTransform& t, const BBox& b,
            int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp     = std::max(2.0f, g.spacing);
    const double vstep  = sp * std::sqrt(3.0) / 2.0;
    const double margin = sp;
    const double cx = imgW * 0.5, cy = imgH * 0.5;   // phase anchored on the centre
    const int j0 = int(std::floor((b.y0 - cy) / vstep)) - 1, j1 = int(std::ceil((b.y1 - cy) / vstep)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gy   = cy + j * vstep;
        const double xoff = ((j % 2 + 2) % 2) ? sp * 0.5 : 0.0;
        const int i0 = int(std::floor((b.x0 - cx - xoff) / sp)) - 1;
        const int i1 = int(std::ceil ((b.x1 - cx - xoff) / sp)) + 1;
        for (int i = i0; i <= i1; ++i) {
            const double gx = cx + i * sp + xoff;
            const QPointF p = t.map(QPointF(gx, gy));
            if (p.x() < -margin || p.x() > imgW + margin ||
                p.y() < -margin || p.y() > imgH + margin) continue;
            out.push_back({ float(p.x()), float(p.y()), 0.0f, j });
        }
    }
}

void genRadial(const GridSettings& g, const QTransform& t, const BBox& b,
               int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp     = std::max(2.0f, g.spacing);
    // ponytail: along-ring spacing tracks `spacing` so the lattice is uniform and
    // dots stay separate. pointSpacing isn't exposed in the UI; expose it and use
    // it here if independent ring sampling is ever wanted.
    const double ps     = sp;
    const double margin = sp;
    const double cx = imgW * 0.5, cy = imgH * 0.5;   // centre is invariant under t

    // Farthest grid-space corner from the centre.
    double maxR = 0.0;
    const double corners[4][2] = {{b.x0,b.y0},{b.x1,b.y0},{b.x1,b.y1},{b.x0,b.y1}};
    for (auto& c : corners)
        maxR = std::max(maxR, std::hypot(c[0] - cx, c[1] - cy));

    out.push_back({ float(cx), float(cy), 0.0f, 0 });   // centre dot
    for (int r = 1; r * sp <= maxR + sp; ++r) {
        const double ringR = r * sp;
        const int n = std::max(1, int(std::round(2.0 * M_PI * ringR / ps)));
        const double dth = 2.0 * M_PI / n;
        for (int k = 0; k < n; ++k) {
            const double th = k * dth;
            const QPointF p = t.map(QPointF(cx + ringR * std::cos(th),
                                            cy + ringR * std::sin(th)));
            if (p.x() < -margin || p.x() > imgW + margin ||
                p.y() < -margin || p.y() > imgH + margin) continue;
            out.push_back({ float(p.x()), float(p.y()), 0.0f, r });
        }
    }
}

// Brick: square rows, full vertical step, alternate rows offset by half a cell.
void genBrick(const GridSettings& g, const QTransform& t, const BBox& b,
              int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp     = std::max(2.0f, g.spacing);
    const double margin = sp;
    const double cx = imgW * 0.5, cy = imgH * 0.5;   // phase anchored on the centre
    const int j0 = int(std::floor((b.y0 - cy) / sp)) - 1, j1 = int(std::ceil((b.y1 - cy) / sp)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gy   = cy + j * sp;
        const double xoff = ((j % 2 + 2) % 2) ? sp * 0.5 : 0.0;
        const int i0 = int(std::floor((b.x0 - cx - xoff) / sp)) - 1;
        const int i1 = int(std::ceil ((b.x1 - cx - xoff) / sp)) + 1;
        for (int i = i0; i <= i1; ++i) {
            const double gx = cx + i * sp + xoff;
            const QPointF p = t.map(QPointF(gx, gy));
            if (p.x() < -margin || p.x() > imgW + margin ||
                p.y() < -margin || p.y() > imgH + margin) continue;
            out.push_back({ float(p.x()), float(p.y()), 0.0f, j });
        }
    }
}

// Wave: square lattice whose rows are displaced vertically by a sine of x —
// amplitude and wavelength derive from the spacing (no extra controls yet).
void genWave(const GridSettings& g, const QTransform& t, const BBox& b,
             int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp     = std::max(2.0f, g.spacing);
    const double amp    = sp * 0.9;                 // vertical sway
    const double k      = 2.0 * M_PI / (sp * 8.0);  // wavelength = 8 cells
    const double margin = sp + amp;
    const double cx = imgW * 0.5, cy = imgH * 0.5;   // phase anchored on the centre
    const int i0 = int(std::floor((b.x0 - cx) / sp)) - 1,         i1 = int(std::ceil((b.x1 - cx) / sp)) + 1;
    const int j0 = int(std::floor((b.y0 - amp - cy) / sp)) - 1, j1 = int(std::ceil((b.y1 + amp - cy) / sp)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gyBase = cy + j * sp;
        for (int i = i0; i <= i1; ++i) {
            const double gx = cx + i * sp;
            const double gy = gyBase + amp * std::sin(gx * k);
            const QPointF p = t.map(QPointF(gx, gy));
            if (p.x() < -margin || p.x() > imgW + margin ||
                p.y() < -margin || p.y() > imgH + margin) continue;
            out.push_back({ float(p.x()), float(p.y()), 0.0f, j });
        }
    }
}

// Phyllotaxis: Vogel's sunflower — r = c·√n, θ = n·137.5°. Isotropic, no rows.
void genPhyllotaxis(const GridSettings& g, const QTransform& t, const BBox& b,
                    int imgW, int imgH, std::vector<GridSample>& out)
{
    const double c      = std::max(2.0f, g.spacing) * 0.8;   // seed scale
    const double margin = c;
    const double cx = imgW * 0.5, cy = imgH * 0.5;
    const double golden = M_PI * (3.0 - std::sqrt(5.0));      // ≈ 137.5°

    double maxR = 0.0;
    const double corners[4][2] = {{b.x0,b.y0},{b.x1,b.y0},{b.x1,b.y1},{b.x0,b.y1}};
    for (auto& cc : corners)
        maxR = std::max(maxR, std::hypot(cc[0] - cx, cc[1] - cy));

    const long nMax = std::min(2000000L, long(std::pow((maxR + c) / c, 2.0)) + 1);
    for (long n = 0; n < nMax; ++n) {
        const double r  = c * std::sqrt(double(n));
        const double th = double(n) * golden;
        const QPointF p = t.map(QPointF(cx + r * std::cos(th),
                                        cy + r * std::sin(th)));
        if (p.x() < -margin || p.x() > imgW + margin ||
            p.y() < -margin || p.y() > imgH + margin) continue;
        out.push_back({ float(p.x()), float(p.y()), 0.0f, 0 });
    }
}

// ── small LRU cache ─────────────────────────────────────────────────────
// Multiple grid layers (Dot Grid / Mosaic / Ascii) can render concurrently
// via QtConcurrent, each with different settings — a single-entry cache
// thrashed every call. A handful of slots lets them coexist; linear scan is
// fine at this size (ponytail: bump kSlots if a document ever needs more
// than a few distinct grid configs live at once).
constexpr int kSlots = 8;

struct CacheEntry {
    bool                     valid = false;
    GridSettings             g;
    int                      w = -1, h = -1;
    std::vector<GridSample>  samples;
};

QMutex     s_mutex;
CacheEntry s_slots[kSlots];
int        s_nextEvict = 0;

} // namespace

std::vector<GridSample> GridGenerator::generate(const GridSettings& g, int imgW, int imgH)
{
    if (imgW <= 0 || imgH <= 0) return {};

    QMutexLocker lock(&s_mutex);
    for (CacheEntry& e : s_slots) {
        if (e.valid && e.w == imgW && e.h == imgH && e.g == g)
            return e.samples;
    }
    lock.unlock();

    const QTransform t   = buildTransform(g, imgW, imgH);
    const QTransform inv = t.inverted();
    const BBox       b   = gridBounds(inv, imgW, imgH);

    std::vector<GridSample> out;
    switch (g.type) {
        case GridType::Square:      genSquare     (g, t, b, imgW, imgH, out); break;
        case GridType::Hexagonal:   genHex        (g, t, b, imgW, imgH, out); break;
        case GridType::Brick:       genBrick      (g, t, b, imgW, imgH, out); break;
        case GridType::Wave:        genWave       (g, t, b, imgW, imgH, out); break;
        case GridType::Radial:      genRadial     (g, t, b, imgW, imgH, out); break;
        case GridType::Phyllotaxis: genPhyllotaxis(g, t, b, imgW, imgH, out); break;
    }

    lock.relock();
    CacheEntry& slot = s_slots[s_nextEvict];
    s_nextEvict = (s_nextEvict + 1) % kSlots;
    slot.valid = true; slot.g = g; slot.w = imgW; slot.h = imgH;
    slot.samples = out;
    return out;
}

GridGpuLayout GridGenerator::computeGpuLayout(const GridSettings& g, int imgW, int imgH)
{
    GridGpuLayout L;
    if (imgW <= 0 || imgH <= 0) return L;

    const QTransform t = buildTransform(g, imgW, imgH);
    const BBox b = gridBounds(t.inverted(), imgW, imgH);
    const double sp = std::max(2.0f, g.spacing);
    const double cx = imgW * 0.5, cy = imgH * 0.5;

    L.m11 = float(t.m11()); L.m12 = float(t.m12());
    L.m21 = float(t.m21()); L.m22 = float(t.m22());
    L.dx  = float(t.dx());  L.dy  = float(t.dy());
    L.type = int(g.type);

    double maxR = 0.0;
    const double corners[4][2] = {{b.x0,b.y0},{b.x1,b.y0},{b.x1,b.y1},{b.x0,b.y1}};
    for (auto& c : corners)
        maxR = std::max(maxR, std::hypot(c[0] - cx, c[1] - cy));

    switch (g.type) {
        case GridType::Square: {
            L.i0   = int(std::floor((b.x0 - cx) / sp)) - 1;
            L.cols = int(std::ceil ((b.x1 - cx) / sp)) + 1 - L.i0 + 1;
            L.j0   = int(std::floor((b.y0 - cy) / sp)) - 1;
            L.rows = int(std::ceil ((b.y1 - cy) / sp)) + 1 - L.j0 + 1;
            break; }
        case GridType::Hexagonal:
        case GridType::Brick: {
            const double vstep = (g.type == GridType::Hexagonal)
                               ? sp * std::sqrt(3.0) / 2.0 : sp;
            // i-range widened by the odd-row half-cell offset.
            L.i0   = int(std::floor((b.x0 - cx - sp * 0.5) / sp)) - 1;
            L.cols = int(std::ceil ((b.x1 - cx) / sp)) + 1 - L.i0 + 1;
            L.j0   = int(std::floor((b.y0 - cy) / vstep)) - 1;
            L.rows = int(std::ceil ((b.y1 - cy) / vstep)) + 1 - L.j0 + 1;
            break; }
        case GridType::Wave: {
            const double amp = sp * 0.9;
            L.i0   = int(std::floor((b.x0 - cx) / sp)) - 1;
            L.cols = int(std::ceil ((b.x1 - cx) / sp)) + 1 - L.i0 + 1;
            L.j0   = int(std::floor((b.y0 - amp - cy) / sp)) - 1;
            L.rows = int(std::ceil ((b.y1 + amp - cy) / sp)) + 1 - L.j0 + 1;
            break; }
        case GridType::Radial: {
            // genRadial: rings r = 1 .. while r·sp ≤ maxR+sp; per-ring count
            // round(2π·ringR/ps) = round(2π·r) since ps == sp.
            L.rows  = int(std::floor((maxR + sp) / sp));
            L.ringN = std::max(1, int(std::round(2.0 * M_PI * double(L.rows))));
            L.count = 1 + L.rows * L.ringN;
            return L; }
        case GridType::Phyllotaxis: {
            const double c = sp * 0.8;
            L.count = int(std::min(2000000L,
                                   long(std::pow((maxR + c) / c, 2.0)) + 1));
            return L; }
    }
    L.count = L.cols * L.rows;
    return L;
}
