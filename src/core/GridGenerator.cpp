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

inline float dirAngle(const QTransform& t, double gx, double gy, double dx, double dy)
{
    const QPointF a = t.map(QPointF(gx, gy));
    const QPointF b = t.map(QPointF(gx + dx, gy + dy));
    return float(std::atan2(b.y() - a.y(), b.x() - a.x()));
}

// ── Dot layouts (Square / Hexagonal) — culled to the image + margin ────────

void genSquare(const GridSettings& g, const QTransform& t, const BBox& b,
               int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp     = std::max(2.0f, g.spacing);
    const double margin = sp;
    const int i0 = int(std::floor(b.x0 / sp)) - 1, i1 = int(std::ceil(b.x1 / sp)) + 1;
    const int j0 = int(std::floor(b.y0 / sp)) - 1, j1 = int(std::ceil(b.y1 / sp)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gy = (j + 0.5) * sp;          // cell centre
        for (int i = i0; i <= i1; ++i) {
            const double gx = (i + 0.5) * sp;
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
    const int j0 = int(std::floor(b.y0 / vstep)) - 1, j1 = int(std::ceil(b.y1 / vstep)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gy   = (j + 0.5) * vstep;
        const double xoff = ((j % 2 + 2) % 2) ? sp * 0.5 : 0.0;
        const int i0 = int(std::floor((b.x0 - xoff) / sp)) - 1;
        const int i1 = int(std::ceil ((b.x1 - xoff) / sp)) + 1;
        for (int i = i0; i <= i1; ++i) {
            const double gx = i * sp + xoff + sp * 0.5;
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
    const double ps     = std::max(2.0f, g.pointSpacing);
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

// ── Stroke layouts (Line / Circles) — kept whole; renderer clips ───────────

void genLine(const GridSettings& g, const QTransform& t, const BBox& b,
             std::vector<GridSample>& out)
{
    const double sp = std::max(2.0f, g.spacing);
    const double ps = std::max(2.0f, g.pointSpacing);
    const float  ang = dirAngle(t, 0.0, 0.0, 1.0, 0.0);   // constant line direction

    const int j0 = int(std::floor(b.y0 / sp)) - 1, j1 = int(std::ceil(b.y1 / sp)) + 1;
    const int k0 = int(std::floor(b.x0 / ps)) - 1, k1 = int(std::ceil(b.x1 / ps)) + 1;

    for (int j = j0; j <= j1; ++j) {
        const double gy = j * sp;
        for (int k = k0; k <= k1; ++k) {
            const double gx = k * ps;
            const QPointF p = t.map(QPointF(gx, gy));
            out.push_back({ float(p.x()), float(p.y()), ang, j });
        }
    }
}

void genCircles(const GridSettings& g, const QTransform& t, const BBox& b,
                int imgW, int imgH, std::vector<GridSample>& out)
{
    const double sp = std::max(2.0f, g.spacing);
    const double ps = std::max(2.0f, g.pointSpacing);
    const double cx = imgW * 0.5, cy = imgH * 0.5;

    double maxR = 0.0;
    const double corners[4][2] = {{b.x0,b.y0},{b.x1,b.y0},{b.x1,b.y1},{b.x0,b.y1}};
    for (auto& c : corners)
        maxR = std::max(maxR, std::hypot(c[0] - cx, c[1] - cy));

    for (int r = 1; r * sp <= maxR + sp; ++r) {
        const double ringR = r * sp;
        const int n = std::max(8, int(std::round(2.0 * M_PI * ringR / ps)));
        const double dth = 2.0 * M_PI / n;
        for (int k = 0; k < n; ++k) {
            const double th = k * dth;
            const double gx = cx + ringR * std::cos(th);
            const double gy = cy + ringR * std::sin(th);
            const QPointF p = t.map(QPointF(gx, gy));
            const float ang = dirAngle(t, gx, gy, -std::sin(th), std::cos(th));
            out.push_back({ float(p.x()), float(p.y()), ang, r });
        }
    }
}

// ── single-entry cache ─────────────────────────────────────────────────────

QMutex                  s_mutex;
GridSettings            s_lastG;
int                     s_lastW = -1, s_lastH = -1;
bool                    s_valid = false;
std::vector<GridSample> s_cache;

} // namespace

std::vector<GridSample> GridGenerator::generate(const GridSettings& g, int imgW, int imgH)
{
    if (imgW <= 0 || imgH <= 0) return {};

    QMutexLocker lock(&s_mutex);
    if (s_valid && s_lastW == imgW && s_lastH == imgH && s_lastG == g)
        return s_cache;

    const QTransform t   = buildTransform(g, imgW, imgH);
    const QTransform inv = t.inverted();
    const BBox       b   = gridBounds(inv, imgW, imgH);

    std::vector<GridSample> out;
    switch (g.type) {
        case GridType::Square:    genSquare (g, t, b, imgW, imgH, out); break;
        case GridType::Hexagonal: genHex    (g, t, b, imgW, imgH, out); break;
        case GridType::Radial:    genRadial (g, t, b, imgW, imgH, out); break;
        case GridType::Line:      genLine   (g, t, b,             out); break;
        case GridType::Circles:   genCircles(g, t, b, imgW, imgH, out); break;
    }

    s_lastG = g; s_lastW = imgW; s_lastH = imgH; s_valid = true;
    s_cache = std::move(out);
    return s_cache;
}
