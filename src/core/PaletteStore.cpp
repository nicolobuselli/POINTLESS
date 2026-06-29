#include "PaletteStore.h"
#include "ColorMath.h"

#include <QImage>
#include <QSettings>
#include <QStringList>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr const char* kOrg     = "ULTRA_Ditherer";
constexpr const char* kApp     = "ULTRA_Ditherer";
constexpr const char* kArray   = "palettes";
constexpr const char* kSeeded  = "palettes_seeded";

QString encode(const std::vector<QColor>& colors)
{
    QStringList parts;
    for (const QColor& c : colors)
        parts << c.name(QColor::HexRgb).toUpper();
    return parts.join(',');
}

std::vector<QColor> decode(const QString& s)
{
    std::vector<QColor> out;
    const QStringList parts = s.split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QColor c(p.trimmed());
        if (c.isValid()) out.push_back(c);
    }
    return out;
}

void writeAll(QSettings& st, const std::vector<PalettePreset>& list)
{
    // Clear the whole array group first so stale entries cannot survive.
    st.beginGroup(kArray);
    st.remove(QString());
    st.endGroup();

    st.beginWriteArray(kArray);
    for (int i = 0; i < int(list.size()); ++i) {
        st.setArrayIndex(i);
        st.setValue("name",   list[i].name);
        st.setValue("colors", encode(list[i].colors));
    }
    st.endArray();
}

} // namespace

std::vector<PalettePreset> PaletteStore::all()
{
    QSettings st(kOrg, kApp);

    // First run: seed the library from the built-in presets.
    if (!st.value(kSeeded, false).toBool()) {
        std::vector<PalettePreset> seed;
        for (const PalettePreset& p : palettePresets())
            seed.push_back(p);
        writeAll(st, seed);
        st.setValue(kSeeded, true);
    }

    std::vector<PalettePreset> out;
    const int n = st.beginReadArray(kArray);
    for (int i = 0; i < n; ++i) {
        st.setArrayIndex(i);
        PalettePreset p;
        p.name   = st.value("name").toString();
        p.colors = decode(st.value("colors").toString());
        if (!p.colors.empty()) out.push_back(p);
    }
    st.endArray();
    return out;
}

void PaletteStore::save(const QString& name, const std::vector<QColor>& colors)
{
    if (name.trimmed().isEmpty() || colors.empty()) return;

    std::vector<PalettePreset> list = all();   // ensures the library is seeded

    bool replaced = false;
    for (PalettePreset& p : list) {
        if (p.name == name) { p.colors = colors; replaced = true; break; }
    }
    if (!replaced) list.push_back({ name, colors });

    QSettings st(kOrg, kApp);
    writeAll(st, list);
}

void PaletteStore::remove(int index)
{
    std::vector<PalettePreset> list = all();
    if (index < 0 || index >= int(list.size())) return;
    list.erase(list.begin() + index);

    QSettings st(kOrg, kApp);
    writeAll(st, list);
}

std::vector<QColor> PaletteStore::randomColors(int n)
{
    n = std::clamp(n, 1, 8);

    // Seeded once and kept advancing across calls, so successive presses never
    // repeat. NB: std::random_device is deterministic on MinGW (same sequence
    // every run), so mix in the clock — otherwise the first press after each
    // launch always produced the identical palette.
    static std::mt19937 rng{ static_cast<std::mt19937::result_type>(
        std::random_device{}()
        ^ static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())) };
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Every swatch gets a fully independent hue and a wide saturation /
    // value spread — deliberately wild, "anything goes" palettes.
    std::vector<QColor> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const float h = unit(rng);                       // any hue
        const float s = 0.20f + unit(rng) * 0.80f;       // 0.20 .. 1.00
        const float v = 0.12f + unit(rng) * 0.88f;       // 0.12 .. 1.00
        QColor c;
        c.setHsvF(h, s, v);
        out.push_back(c);
    }

    // Order light → dark by luminance so the highlight→shadow rows read
    // top-to-bottom (only the ordering is tamed, never the colors).
    std::sort(out.begin(), out.end(), [](const QColor& a, const QColor& b) {
        auto lum = [](const QColor& c) {
            return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
        };
        return lum(a) > lum(b);
    });
    return out;
}

std::vector<QColor> PaletteStore::fromImage(const QImage& img, int n)
{
    n = std::clamp(n, 1, 8);
    if (img.isNull()) return {};

    // Downscale for speed; work in linear light so averages are correct.
    QImage small = img;
    const int maxDim = std::max(img.width(), img.height());
    if (maxDim > 128)
        small = img.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    small = small.convertToFormat(QImage::Format_ARGB32);

    struct P { float r, g, b; };
    std::vector<P> pts;
    pts.reserve(size_t(small.width()) * small.height());
    for (int y = 0; y < small.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QRgb px = line[x];
            if (qAlpha(px) < 8) continue;
            pts.push_back({ ColorMath::srgbToLinear(qRed(px)),
                            ColorMath::srgbToLinear(qGreen(px)),
                            ColorMath::srgbToLinear(qBlue(px)) });
        }
    }
    if (pts.empty()) return {};

    // Median cut: repeatedly split the box with the widest colour spread, so
    // buckets chase the distinct hues (the gamut) rather than the largest flat
    // areas — this keeps small but vivid accents in the palette. A small
    // population floor skips single-pixel noise.
    struct Box { int begin, end; };
    std::vector<Box> boxes{ { 0, int(pts.size()) } };
    const int minPop = std::max(2, int(pts.size()) / 1000);

    while (int(boxes.size()) < n) {
        int   bi = -1, bestCh = 0;
        float bestScore = -1.0f;
        for (int i = 0; i < int(boxes.size()); ++i) {
            const Box& bx  = boxes[i];
            const int  pop = bx.end - bx.begin;
            if (pop < minPop || pop < 2) continue;
            float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
            for (int k = bx.begin; k < bx.end; ++k) {
                const float c[3] = { pts[k].r, pts[k].g, pts[k].b };
                for (int ch = 0; ch < 3; ++ch) {
                    mn[ch] = std::min(mn[ch], c[ch]);
                    mx[ch] = std::max(mx[ch], c[ch]);
                }
            }
            int   ch  = 0;
            float rng = -1.0f;
            for (int c = 0; c < 3; ++c) {
                const float r = mx[c] - mn[c];
                if (r > rng) { rng = r; ch = c; }
            }
            if (rng > bestScore) { bestScore = rng; bi = i; bestCh = ch; }
        }
        if (bi < 0) break;   // nothing left to split

        const Box bx = boxes[bi];
        auto chan = [bestCh](const P& p) { return bestCh == 0 ? p.r : (bestCh == 1 ? p.g : p.b); };
        std::sort(pts.begin() + bx.begin, pts.begin() + bx.end,
                  [&](const P& a, const P& b) { return chan(a) < chan(b); });
        // Split at the value midpoint, not the population median: otherwise a
        // dominant flat region (e.g. a white background) gets halved into
        // near-identical buckets, starving the actual colours of slots.
        const float lo  = chan(pts[bx.begin]);
        const float hi  = chan(pts[bx.end - 1]);
        const float thr = 0.5f * (lo + hi);
        int mid = bx.begin;
        while (mid < bx.end && chan(pts[mid]) < thr) ++mid;
        mid = std::clamp(mid, bx.begin + 1, bx.end - 1);
        boxes[bi] = { bx.begin, mid };
        boxes.push_back({ mid, bx.end });
    }

    std::vector<QColor> out;
    out.reserve(boxes.size());
    for (const Box& bx : boxes) {
        const int cnt = bx.end - bx.begin;
        if (cnt <= 0) continue;
        double r = 0.0, g = 0.0, b = 0.0;
        for (int k = bx.begin; k < bx.end; ++k) { r += pts[k].r; g += pts[k].g; b += pts[k].b; }
        out.push_back(QColor(ColorMath::linearToSrgb8(float(r / cnt)),
                             ColorMath::linearToSrgb8(float(g / cnt)),
                             ColorMath::linearToSrgb8(float(b / cnt))));
    }

    std::sort(out.begin(), out.end(), [](const QColor& a, const QColor& b) {
        auto lum = [](const QColor& c) {
            return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
        };
        return lum(a) > lum(b);
    });
    return out;
}
