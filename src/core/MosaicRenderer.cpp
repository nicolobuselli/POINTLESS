#include "MosaicRenderer.h"
#include "ColorMath.h"
#include "GridGenerator.h"

#include <QFont>
#include <QFontMetricsF>
#include <QStaticText>
#include <cmath>
#include <vector>

namespace {

// Average of the cell in linear light + its perceptual luma (for tone pick).
struct CellAvg {
    float rLin = 1, gLin = 1, bLin = 1;
    float lumPerc = 1;
};

CellAvg cellAverage(const QImage& rgb, int cx, int cy, int cw, int ch)
{
    double r = 0, g = 0, b = 0;
    int count = 0;
    for (int y = cy; y < cy + ch; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cx; x < cx + cw; ++x) {
            const QRgb p = line[x];
            r += ColorMath::srgbToLinear(qRed(p));
            g += ColorMath::srgbToLinear(qGreen(p));
            b += ColorMath::srgbToLinear(qBlue(p));
            ++count;
        }
    }
    CellAvg a;
    if (count == 0) return a;
    a.rLin = float(r / count);
    a.gLin = float(g / count);
    a.bLin = float(b / count);
    a.lumPerc = ColorMath::perceptualLumaFromLinear(a.rLin, a.gLin, a.bLin);
    return a;
}

// Font sized so `text` fits a tileW×tileH tile minus padFrac (fraction of the
// tile's shorter side) padding on every side.
QFont fitFont(const MosaicSettings& s, const QString& text, float tileW, float tileH, float padFrac)
{
    const float pad = padFrac * qMin(tileW, tileH);
    const float maxW = qMax(1.0f, tileW - 2.0f * pad);
    const float maxH = qMax(1.0f, tileH - 2.0f * pad);

    QFont font(s.fontFamily.isEmpty() ? QStringLiteral("Funnel Display") : s.fontFamily);
    font.setWeight(QFont::Weight(qBound(100, s.fontWeight, 900)));
    font.setPixelSize(qMax(1, qRound(maxH)));

    const QFontMetricsF fm(font);
    const qreal w = fm.horizontalAdvance(text);
    if (w > maxW && w > 0.0)
        font.setPixelSize(qMax(1, int(std::floor(maxH * maxW / w))));
    return font;
}

} // namespace

void MosaicRenderer::render(const QImage& input, QPainter& output, const MosaicSettings& params)
{
    if (input.isNull()) return;
    const QImage rgb = input.convertToFormat(QImage::Format_RGB32);
    const int w = rgb.width(), h = rgb.height();

    const float cellW = qMax(2.0f, params.cellW());
    const float cellH = qMax(2.0f, params.cellH());

    // Tile centres come from the same GridGenerator Halftone uses: `gridShape`
    // picks the lattice (Square/Hex/Brick/Wave/Radial/Phyllotaxis) and
    // `gridRotation` rotates it as a whole, exactly like Halftone's grid.
    GridSettings gs;
    gs.type     = params.gridShape;
    gs.spacing  = qMax(2.0f, params.spacing);
    gs.rotation = params.gridRotation;
    const std::vector<GridSample> samples = GridGenerator::generate(gs, w, h);

    const int cellPxW = qMax(1, qRound(cellW));
    const int cellPxH = qMax(1, qRound(cellH));

    const float radPct  = qBound(0.0f, params.cornerRadius, 100.0f) / 100.0f;
    const float opacity = qBound(0.0f, params.opacity, 1.0f);

    const LocField locSpacing = locField(params.loc, LocParam::MsSpacing,     float(w), float(h));
    const LocField locWidth   = locField(params.loc, LocParam::MsWidthPct,    float(w), float(h));
    const LocField locHeight  = locField(params.loc, LocParam::MsHeightPct,   float(w), float(h));
    const LocField locPad     = locField(params.loc, LocParam::MsTextPadding, float(w), float(h));
    const LocMask  lmask      = locMask(params.loc, float(w), float(h));
    const bool tileLocalized = locSpacing.on || locWidth.on || locHeight.on || locPad.on;

    const bool  shrunk  = (radPct > 0.001f || tileLocalized);

    const TonalSettings& tonal = params.tonal;
    const bool imageColors = (tonal.mode == ToneMode::ImageColors);
    const bool paletteMode = (tonal.mode == ToneMode::Palette);
    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tonal.tones)
                    : std::vector<ColorMath::PaletteEntry>{};

    // Single fixed tone = binary ink-or-paper, and paper is transparent —
    // same rule as DitherRenderer's nTones<=1 path: tiles nearer white than
    // the ink's level anchor draw nothing.
    const bool inkPaper = (!imageColors && !paletteMode && tonal.tones.size() == 1);
    const float paperCut = inkPaper
        ? (qBound(0, tonal.tones[0].level, 255) + 255.0f) * 0.5f / 255.0f
        : 2.0f;

    // Per-tone text + fitted font, computed once (all tiles share the size).
    // staticText caches the shaped glyph run: every non-localized tile of a
    // tone reuses it instead of re-shaping the same string from scratch —
    // with dense grids (thousands of tiles) drawText()'s per-call layout cost
    // otherwise dominates the whole render.
    struct ToneText { QString text; QFont font; QStaticText staticText; };
    const float tileWRef = cellW;
    const float tileHRef = cellH;
    std::vector<ToneText> toneTexts(tonal.tones.size());
    for (size_t i = 0; i < tonal.tones.size(); ++i) {
        if (i < params.texts.size() && !params.texts[i].isEmpty()) {
            toneTexts[i].text = params.texts[i];
            if (!tileLocalized) {
                const float padFrac = qBound(0, params.textPadding, 45) / 100.0f;
                toneTexts[i].font = fitFont(params, params.texts[i], tileWRef, tileHRef, padFrac);
                toneTexts[i].staticText.setText(params.texts[i]);
                toneTexts[i].staticText.setTextFormat(Qt::PlainText);
                toneTexts[i].staticText.setPerformanceHint(QStaticText::AggressiveCaching);
                toneTexts[i].staticText.setTextWidth(tileWRef);
                QTextOption opt(Qt::AlignCenter);
                toneTexts[i].staticText.setTextOption(opt);
                toneTexts[i].staticText.prepare(QTransform(), toneTexts[i].font);
            }
        }
    }

    output.save();
    output.setPen(Qt::NoPen);
    output.setRenderHint(QPainter::Antialiasing, shrunk);

    for (const GridSample& s : samples) {
        // Sampling/base-tile window centred on the grid sample point. A
        // window that would spill off the frame (right/bottom edge samples,
        // whenever cellPxW/H doesn't evenly divide w/h) is skipped outright
        // instead of being clipped in place — a clipped tile kept the full
        // reference-size text (sized off the uncropped cell, not the shrunk
        // one), which visually overlapped the next tile.
        const int xIdeal0 = qRound(s.x) - cellPxW / 2;
        const int yIdeal0 = qRound(s.y) - cellPxH / 2;
        if (xIdeal0 < 0 || yIdeal0 < 0 || xIdeal0 + cellPxW > w || yIdeal0 + cellPxH > h)
            continue;
        const int x0 = xIdeal0, y0 = yIdeal0;
        const int pw = cellPxW, ph = cellPxH;

        const float cxp = s.x;
        const float cyp = s.y;
        const float maskVal = lmask.on ? lmask.mask(cxp, cyp) : 1.0f;
        if (lmask.on && maskVal <= 0.0f) continue;   // spotlight: nothing outside enabled circles

        const CellAvg avg = cellAverage(rgb, x0, y0, pw, ph);
        if (avg.lumPerc > paperCut) continue;   // ink-or-paper: paper = nothing

        // Fill colour + the tone index that owns this tile's text.
        QColor fill;
        int toneIdx = -1;
        if (paletteMode && !palette.empty()) {
            toneIdx = ColorMath::nearestPaletteIndex(
                palette, ColorMath::linearToOklab(avg.rLin, avg.gLin, avg.bLin));
            fill = QColor::fromRgba(palette[size_t(toneIdx)].out);
        } else if (imageColors || tonal.tones.empty()) {
            fill = QColor(ColorMath::linearToSrgb8(avg.rLin),
                          ColorMath::linearToSrgb8(avg.gLin),
                          ColorMath::linearToSrgb8(avg.bLin));
            if (!tonal.tones.empty())   // texts still follow the tone bands
                toneIdx = pickToneIndex(tonal.tones, avg.lumPerc);
        } else {
            toneIdx = pickToneIndex(tonal.tones, avg.lumPerc);
            const ToneEntry& te = tonal.tones[size_t(toneIdx)];
            fill = te.color;
            fill.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
        }
        fill.setAlphaF(fill.alphaF() * opacity * maskVal);

        // Tile centred in its slot; mSpacing/mWidth/mHeight are 1 unless their
        // loc point is enabled, so this reduces to the original uniform
        // formula when unlocalized.
        const float mSpacing = locSpacing.mul(cxp, cyp);
        const float mWidth   = locWidth.mul(cxp, cyp);
        const float mHeight  = locHeight.mul(cxp, cyp);
        const float tw = pw * mSpacing * mWidth;
        const float th = ph * mSpacing * mHeight;
        const QRectF r(x0 + (pw - tw) * 0.5f, y0 + (ph - th) * 0.5f, tw, th);

        // Tiles turn with the lattice (same rotate() the halftone grid
        // uses), pivoted on each tile's own centre so tile edges line up
        // with the grid's oblique direction instead of staying axis-aligned.
        output.save();
        output.translate(r.center());
        output.rotate(params.gridRotation);
        output.translate(-r.center());

        if (radPct > 0.001f) {
            const float rad = radPct * qMin(tw, th) * 0.5f;
            output.setBrush(fill);
            output.drawRoundedRect(r, rad, rad);
            output.setBrush(Qt::NoBrush);
        } else {
            output.fillRect(r, fill);
        }

        if (toneIdx >= 0 && toneIdx < int(toneTexts.size())
            && !toneTexts[size_t(toneIdx)].text.isEmpty()) {
            QColor pen;
            if (toneIdx < int(params.textColors.size())
                && params.textColors[size_t(toneIdx)].isValid()) {
                pen = params.textColors[size_t(toneIdx)];
            } else {
                // Default: auto black/white by fill contrast.
                pen = ColorMath::perceptualLuma(fill.rgb()) > 0.5f
                    ? QColor(Qt::black) : QColor(Qt::white);
            }
            pen.setAlphaF(pen.alphaF() * opacity * maskVal);
            output.setPen(pen);
            if (tileLocalized) {
                const float padFrac = qBound(0, params.textPadding, 45) / 100.0f
                                    * locPad.mul(cxp, cyp);
                output.setFont(fitFont(params, toneTexts[size_t(toneIdx)].text, tw, th, padFrac));
                output.drawText(r, Qt::AlignCenter, toneTexts[size_t(toneIdx)].text);
            } else {
                const ToneText& tt = toneTexts[size_t(toneIdx)];
                output.setFont(tt.font);
                const QSizeF sz = tt.staticText.size();
                const QPointF pos(r.center().x() - tileWRef * 0.5,
                                   r.center().y() - sz.height() * 0.5);
                output.drawStaticText(pos, tt.staticText);
            }
            output.setPen(Qt::NoPen);
        }
        output.restore();
    }

    output.restore();
}
