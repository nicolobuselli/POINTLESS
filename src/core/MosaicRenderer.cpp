#include "MosaicRenderer.h"
#include "ColorMath.h"

#include <QFont>
#include <QFontMetricsF>
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

// Font sized so `text` fits a tileW×tileH tile minus padding on every side.
QFont fitFont(const MosaicSettings& s, const QString& text, float tileW, float tileH)
{
    const float pad = qBound(0, s.textPadding, 45) / 100.0f * qMin(tileW, tileH);
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
    const int cols = qMax(1, int(std::ceil(w / cellW)));
    const int rows = qMax(1, int(std::ceil(h / cellH)));

    const float gapX    = qBound(0.0f, params.gapX, 90.0f) / 100.0f;
    const float gapY    = qBound(0.0f, params.gapY, 90.0f) / 100.0f;
    const float radPct  = qBound(0.0f, params.cornerRadius, 100.0f) / 100.0f;
    const float opacity = qBound(0.0f, params.opacity, 1.0f);
    const bool  shrunk  = (gapX > 0.001f || gapY > 0.001f || radPct > 0.001f);

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
    struct ToneText { QString text; QFont font; };
    const float tileWRef = cellW * (1.0f - gapX);
    const float tileHRef = cellH * (1.0f - gapY);
    std::vector<ToneText> toneTexts(tonal.tones.size());
    for (size_t i = 0; i < tonal.tones.size(); ++i) {
        if (i < params.texts.size() && !params.texts[i].isEmpty()) {
            toneTexts[i].text = params.texts[i];
            toneTexts[i].font = fitFont(params, params.texts[i], tileWRef, tileHRef);
        }
    }

    output.save();
    output.setPen(Qt::NoPen);
    output.setRenderHint(QPainter::Antialiasing, shrunk);

    for (int ry = 0; ry < rows; ++ry) {
        for (int cx = 0; cx < cols; ++cx) {
            // Integer slot edges shared by neighbours: at gap 0 the tiles butt
            // exactly, so AA can't leave grey hairline seams between them.
            const int x0 = qRound(cx * cellW),       y0 = qRound(ry * cellH);
            const int x1 = qMin(qRound((cx + 1) * cellW), w);
            const int y1 = qMin(qRound((ry + 1) * cellH), h);
            const int pw = x1 - x0, ph = y1 - y0;
            if (pw <= 0 || ph <= 0) continue;

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
            fill.setAlphaF(fill.alphaF() * opacity);

            // Tile centred in its slot, shrunk by the gaps.
            const float tw = pw * (1.0f - gapX), th = ph * (1.0f - gapY);
            const QRectF r(x0 + (pw - tw) * 0.5f, y0 + (ph - th) * 0.5f, tw, th);
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
                pen.setAlphaF(pen.alphaF() * opacity);
                output.setPen(pen);
                output.setFont(toneTexts[size_t(toneIdx)].font);
                output.drawText(r, Qt::AlignCenter, toneTexts[size_t(toneIdx)].text);
                output.setPen(Qt::NoPen);
            }
        }
    }

    output.restore();
}
