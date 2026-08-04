#include "TimelineWidget.h"
#include "Theme.h"
#include "Widgets.h"
#include "../core/AnimParams.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace {

// Working space past the end of the animation: the timeline always scrolls
// this many frames beyond the active range, so keyframes/clips can be placed
// (and dragged) past the end.
constexpr int kTailFrames = 200;

// Scaled metrics (design px → on-screen px).
int GUT()  { return Ui::px(150); }   // label column width
int RUL()  { return Ui::px(30); }    // ruler strip height
int ROW()  { return Ui::px(34); }    // per-track row height
int PADR() { return Ui::px(24); }    // right padding
int HIT()  { return Ui::px(11); }    // diamond hit radius

// Clipboard for keyframe copy/paste (shared across the timeline).
struct CopiedKey { int layerId; ParamId param; int frameOff; double value; Easing easing; };
QVector<CopiedKey> g_keyClip;

// Render an SVG centred and fitted inside a bw×bh box (optically balances
// glyphs that have different intrinsic aspect ratios), optionally mirrored.
QIcon svgIcon(const QString& res, int bw, int bh, bool flipH = false)
{
    QSvgRenderer r(res);
    const QSizeF def = r.defaultSize();
    const double sc = def.isEmpty() ? 1.0
                    : std::min(double(bw) / def.width(), double(bh) / def.height());
    const int iw = bw * 2, ih = bh * 2;               // supersample for crispness
    const double dw = def.width()  * sc * 2.0;
    const double dh = def.height() * sc * 2.0;
    QImage img(iw, ih, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    r.render(&p, QRectF((iw - dw) / 2.0, (ih - dh) / 2.0, dw, dh));
    p.end();
    if (flipH) img = img.mirrored(true, false);
    return QIcon(QPixmap::fromImage(img));
}

QIcon playIcon()  { return svgIcon(":/icons/tl_play.svg", Ui::px(13), Ui::px(15)); }

// Zoom slider's magnifier (zoom.svg, already #B2B2B2), rendered at 2× so it
// stays crisp on hi-dpi.
QPixmap magnifierPixmap(int s)
{
    QSvgRenderer r(QStringLiteral(":/icons/zoom.svg"));
    QPixmap pm(s * 2, s * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // The pixmap carries dpr 2, so the painter works in logical px (s), not the
    // 2s device px it actually holds — rendering into 2s draws 4× too big.
    r.render(&p, QRectF(0, 0, s, s));
    return pm;
}

QIcon pauseIcon(int heightPx)
{
    const int h = heightPx * 2;
    const int w = h;
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor("#D9D9D9"));
    p.setPen(Qt::NoPen);
    const int bw = w * 0.28, gap = w * 0.18;
    const int x0 = (w - (2 * bw + gap)) / 2;
    p.drawRoundedRect(QRectF(x0, h * 0.08, bw, h * 0.84), bw * 0.3, bw * 0.3);
    p.drawRoundedRect(QRectF(x0 + bw + gap, h * 0.08, bw, h * 0.84), bw * 0.3, bw * 0.3);
    p.end();
    return QIcon(QPixmap::fromImage(img));
}

void drawDiamond(QPainter& p, int x, int y, Easing e, bool selected)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const int r = Ui::px(10);
    const QColor fill = selected ? Ui::kColLocLime : QColor("#E8E8E8");
    p.setBrush(fill);
    p.setPen(QPen(QColor("#101010"), 1));
    if (e == Easing::Hold) {
        p.drawRect(QRectF(x - r + 1, y - r + 1, 2 * r - 2, 2 * r - 2));
    } else {
        QPolygonF d;
        d << QPointF(x, y - r) << QPointF(x + r, y) << QPointF(x, y + r) << QPointF(x - r, y);
        p.drawPolygon(d);
    }
    p.restore();
}
} // namespace

// ============================================================
//  TimelineCanvas — painted dopesheet + mouse interaction
// ============================================================

class TimelineCanvas : public QWidget
{
public:
    explicit TimelineCanvas(TimelineWidget* owner)
        : QWidget(owner), m_owner(owner)
    {
        setMouseTracking(true);
        setMinimumHeight(RUL() + ROW() * 3);
        setFocusPolicy(Qt::ClickFocus);   // so Space (play) reaches the timeline
    }

    void copySelection()
    {
        if (m_sel.isEmpty()) return;
        const Animation& a = m_owner->m_anim;
        int minFrame = a.tracks[size_t(m_sel[0].first)].keys[size_t(m_sel[0].second)].frame;
        for (const auto& s : m_sel)
            minFrame = std::min(minFrame, a.tracks[size_t(s.first)].keys[size_t(s.second)].frame);
        g_keyClip.clear();
        for (const auto& s : m_sel) {
            const Track&    t = a.tracks[size_t(s.first)];
            const Keyframe& k = t.keys[size_t(s.second)];
            g_keyClip.append({ t.layerId, t.param, k.frame - minFrame, k.value, k.easing });
        }
    }

    // Backspace/Delete from MainWindow: remove the selected keyframes.
    // Returns true if there was a selection to delete.
    bool deleteSelected()
    {
        if (m_sel.isEmpty()) return false;
        deleteSelection();
        update();
        m_owner->emitEdited();
        return true;
    }

    void pasteAtPlayhead()
    {
        if (g_keyClip.isEmpty()) return;
        const int base = m_owner->m_anim.playhead;
        for (const auto& c : g_keyClip)
            upsertKey(m_owner->m_anim, c.layerId, c.param, base + c.frameOff, c.value, c.easing);

        m_sel.clear();
        const Animation& a = m_owner->m_anim;
        for (const auto& c : g_keyClip)
            for (int ti = 0; ti < int(a.tracks.size()); ++ti) {
                if (a.tracks[size_t(ti)].layerId != c.layerId || a.tracks[size_t(ti)].param != c.param) continue;
                for (int ki = 0; ki < int(a.tracks[size_t(ti)].keys.size()); ++ki)
                    if (a.tracks[size_t(ti)].keys[size_t(ki)].frame == base + c.frameOff)
                        m_sel.append({ ti, ki });
            }
        update();
        m_owner->emitEdited();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Animation& a = m_owner->m_anim;
        const int gut = GUT(), rul = RUL(), row = ROW();

        p.fillRect(rect(), Ui::kColBgPanel);

        // ── Alternating row backgrounds ──────────────────────────
        for (int i = 0; i < m_owner->m_rows.size(); ++i) {
            const int y0 = rul + i * row;
            // First animated row matches the panel colour; stripes alternate.
            p.fillRect(QRect(0, y0, width(), row),
                       (i % 2) ? QColor("#2D2D2D") : Ui::kColBgPanel);
        }

        // Everything past the label gutter scrolls, so it must stay clipped out
        // of the (fixed) name column.
        p.save();
        p.setClipRect(QRect(gut, 0, width() - gut, height()));

        // ── Vertical grid: major every `majorStep`, minor at half ──
        // Every step is a multiple of 5: zooming in subdivides 100 → 50 → 20 →
        // 10 → 5 and stops there, instead of drawing a line per frame.
        const double ppf = pxPerFrame();
        static const int kSteps[] = { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
        int majorStep = kSteps[sizeof(kSteps) / sizeof(int) - 1];
        for (int s : kSteps) if (s * ppf >= Ui::px(48)) { majorStep = s; break; }
        // Half-steps only while they stay multiples of 5.
        const int minorStep = (majorStep / 2 >= 5 && majorStep % 10 == 0)
                            ? majorStep / 2 : majorStep;
        const int gridBottom = height();   // run lines all the way to the bottom
        const int dEnd       = m_owner->dispEndFrame();

        for (int f = a.frameStart; f <= dEnd; ++f) {
            const int rel = f - a.frameStart;
            const int x = frameToX(f);
            if (x < gut) continue;
            if (x > width()) break;
            if (rel % majorStep == 0) {
                p.setPen(QColor("#686868"));
                p.drawLine(x, rul, x, gridBottom);
            } else if (rel % minorStep == 0) {
                p.setPen(QColor("#414141"));
                p.drawLine(x, rul, x, gridBottom);
            }
        }

        // ── Dim the working space outside the active [start,end] range ─
        {
            const QColor dim(0, 0, 0, 90);
            const int xStart = frameToX(a.frameStart);
            const int xEnd   = frameToX(a.frameEnd);
            if (xStart > gut)
                p.fillRect(QRect(gut, rul, xStart - gut, height() - rul), dim);
            if (xEnd < width())
                p.fillRect(QRect(xEnd, rul, width() - xEnd, height() - rul), dim);
        }

        // ── Clip rows: duration bar with trim handles ────────────
        for (int i = 0; i < clipCount(); ++i) {
            const TimelineWidget::ClipRow& c = m_owner->m_clips[i];
            const int top = clipTop(i);
            if (top < 0) continue;
            const int barH = Ui::px(20);
            const int by   = top + (row - barH) / 2;
            const int xAll = frameToX(a.frameStart + c.offset);
            const int xEnd = frameToX(a.frameStart + c.offset + c.length);
            const int xIn  = frameToX(a.frameStart + c.offset + c.trimIn);
            const int xOut = frameToX(a.frameStart + c.offset + c.trimOut + 1);
            const double r = Ui::px(6);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(Ui::kColLocOliveDark);      // full source extent (trimmed-away)
            p.drawRoundedRect(QRectF(xAll, by, xEnd - xAll, barH), r, r);
            p.setBrush(Ui::kColLocOliveStroke);    // active part
            p.drawRoundedRect(QRectF(xIn, by, xOut - xIn, barH), r, r);
            // Grip marks on the draggable edges.
            p.setPen(Ui::kColLocOliveStroke2);
            for (int d : { 4, 7 }) {
                p.drawLine(xIn + Ui::px(d), by + Ui::px(4), xIn + Ui::px(d), by + barH - Ui::px(4));
                p.drawLine(xOut - Ui::px(d), by + Ui::px(4), xOut - Ui::px(d), by + barH - Ui::px(4));
            }
        }

        // ── Keyframe diamonds ────────────────────────────────────
        for (int i = 0; i < int(a.tracks.size()); ++i) {
            const int top = trackTop(i);
            if (top < 0) continue;
            const int cy = top + row / 2;
            for (int ki = 0; ki < int(a.tracks[size_t(i)].keys.size()); ++ki)
                drawDiamond(p, frameToX(a.tracks[size_t(i)].keys[size_t(ki)].frame), cy,
                            a.tracks[size_t(i)].keys[size_t(ki)].easing, isSelected(i, ki));
        }
        p.restore();   // end scrolling-content clip

        // ── Ruler header band (panel colour, set off by a divider) ─
        p.fillRect(QRect(0, 0, width(), rul), Ui::kColBgPanel);
        p.setPen(Ui::kColSurface2);
        p.drawLine(0, rul, width(), rul);

        QFont rf = p.font(); rf.setPixelSize(Ui::px(16)); p.setFont(rf);
        for (int f = a.frameStart; f <= dEnd; ++f) {
            if ((f - a.frameStart) % majorStep != 0) continue;
            const int x = frameToX(f);
            if (x < gut) continue;
            p.setPen(QColor("#9A9A9A"));
            p.drawText(QRect(x - Ui::px(24), 0, Ui::px(48), rul - Ui::px(1)),
                       Qt::AlignCenter, QString::number(f));
        }

        // ── Row labels in the fixed name gutter ──────────────────
        // A track that animates a clip's layer is indented under it and tied to
        // it by an L, the same parent/child cue the layers panel uses.
        QFont lf = p.font(); lf.setPixelSize(Ui::px(17)); p.setFont(lf);
        for (int r = 0; r < m_owner->m_rows.size(); ++r) {
            const TimelineWidget::Row& rw = m_owner->m_rows[r];
            const int y0     = rul + r * row;
            const int indent = rw.child ? Ui::px(48) : Ui::px(20);
            if (rw.child) {
                const int lx = Ui::px(30);
                p.setPen(QColor("#5D5D5D"));
                p.drawLine(lx, y0 - row / 2, lx, y0 + row / 2);
                p.drawLine(lx, y0 + row / 2, indent - Ui::px(8), y0 + row / 2);
            }
            p.setPen(QColor("#C8C8C8"));
            const QString text = (rw.clip >= 0)
                ? m_owner->m_clips[rw.clip].name
                : QString::fromUtf8(paramDesc(a.tracks[size_t(rw.track)].param).label);
            p.drawText(QRect(indent, y0, gut - indent - Ui::px(6), row),
                       Qt::AlignVCenter | Qt::AlignLeft, text);
        }

        // ── Playhead: lime counter (tl_counter.svg cap) + bar ───
        const int px = frameToX(a.playhead);
        // Not clipped to the frame area: at the first frames the cap straddles
        // the gutter edge, and clipping it there also ate the playhead bar.
        // The `px >= gut` guard alone keeps a scrolled-away playhead hidden.
        if (px >= gut && px <= width()) {
            static QSvgRenderer counterSvg(QStringLiteral(":/icons/tl_counter.svg"));
            p.setRenderHint(QPainter::Antialiasing, true);
            QFont hf = p.font(); hf.setPixelSize(Ui::px(12)); hf.setBold(true); p.setFont(hf);
            const QString fnum = QString::number(a.playhead);
            // Static width — sized for 3 digits (frame 100+) so it doesn't
            // resize as the frame number grows/shrinks digits.
            const int tw = p.fontMetrics().horizontalAdvance(QStringLiteral("888"));
            const int fw = std::max(Ui::px(22), tw + Ui::px(12));
            const int fh = fw;   // cap is square (tl_counter.svg viewBox is 324 wide)
            const QRectF flag(px - fw / 2.0, 0, fw, fh);
            p.save();
            p.setClipRect(flag);
            counterSvg.render(&p, QRectF(flag.topLeft(), QSizeF(fw, fw * 2224.0 / 324.0)));
            p.restore();
            p.setPen(Ui::kColLocOliveStroke);
            p.drawText(flag, Qt::AlignCenter, fnum);
            p.fillRect(QRectF(px - Ui::px(1), fh, Ui::px(2), height() - fh),
                       Ui::kColLocLime);
        }

        // ── Rubber-band selection rectangle ──────────────────────
        if (m_banding) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(Ui::kColTextDim, 1, Qt::DashLine));
            p.setBrush(QColor(142, 142, 142, 40));
            p.drawRect(m_bandRect);
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        const QPoint pt = e->pos();
        if (pt.x() < GUT()) return;

        if (pt.y() < RUL()) {
            m_scrubbing = true;
            m_owner->scrubTo(xToFrame(pt.x()));
            return;
        }

        const int ci = clipAtY(pt.y());
        if (ci >= 0) {
            m_trimEdge = trimEdgeAt(ci, pt.x());
            if (m_trimEdge != 0) {
                m_trimClip = ci;
            } else if (onClipBody(ci, pt.x())) {
                m_moveClip   = ci;
                m_moveRefFrm = xToFrame(pt.x());
                m_moveOrigOff = m_owner->m_clips[ci].offset;
                setCursor(Qt::ClosedHandCursor);
            }
            return;   // clip rows have no keyframes to hit
        }

        const int rowi = rowAtY(pt.y());
        if (rowi >= 0) {
            auto& keys = m_owner->m_anim.tracks[size_t(rowi)].keys;
            const int cy = trackTop(rowi) + ROW() / 2;
            for (int ki = 0; ki < int(keys.size()); ++ki) {
                if (qAbs(pt.x() - frameToX(keys[size_t(ki)].frame)) <= HIT()
                    && qAbs(pt.y() - cy) <= HIT()) {
                    if (!isSelected(rowi, ki))
                        m_sel = { { rowi, ki } };
                    beginKeyDrag(xToFrame(pt.x()));
                    update();
                    return;
                }
            }
        }
        m_banding = true;
        m_bandOrigin = pt;
        m_bandRect = QRect(pt, pt);
        m_sel.clear();
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_trimClip >= 0) {
            TimelineWidget::ClipRow& c = m_owner->m_clips[m_trimClip];
            const int f = xToFrame(e->pos().x()) - m_owner->m_anim.frameStart - c.offset;
            if (m_trimEdge < 0) c.trimIn  = qBound(0, qMin(f, c.trimOut), c.length - 1);
            else                c.trimOut = qBound(c.trimIn, f - 1, c.length - 1);
            emitClip(c);
            return;
        }
        if (m_moveClip >= 0) {
            TimelineWidget::ClipRow& c = m_owner->m_clips[m_moveClip];
            const int span = m_owner->dispEndFrame() - m_owner->m_anim.frameStart;
            c.offset = qBound(0, m_moveOrigOff + xToFrame(e->pos().x()) - m_moveRefFrm, span);
            emitClip(c);
            return;
        }
        if (!m_draggingKeys && !m_banding && !m_scrubbing) {
            const int ci = clipAtY(e->pos().y());
            const bool edge = ci >= 0 && trimEdgeAt(ci, e->pos().x()) != 0;
            setCursor(edge ? Qt::SizeHorCursor
                    : (ci >= 0 && onClipBody(ci, e->pos().x())) ? Qt::OpenHandCursor
                                                                : Qt::ArrowCursor);
        }
        if (m_draggingKeys) {
            const Animation& a = m_owner->m_anim;
            const int delta = xToFrame(e->pos().x()) - m_dragRefFrame;
            for (int i = 0; i < m_sel.size(); ++i) {
                const int ti = m_sel[i].first, ki = m_sel[i].second;
                m_owner->m_anim.tracks[size_t(ti)].keys[size_t(ki)].frame =
                    qBound(a.frameStart, m_dragOrigFrames[i] + delta, m_owner->dispEndFrame());
            }
            update();
            m_owner->emitEdited();
        } else if (m_banding) {
            m_bandRect = QRect(m_bandOrigin, e->pos()).normalized();
            update();
        } else if (m_scrubbing) {
            m_owner->scrubTo(xToFrame(e->pos().x()));
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        m_trimClip = -1;
        if (m_moveClip >= 0) { m_moveClip = -1; setCursor(Qt::OpenHandCursor); }
        if (m_draggingKeys) {
            QVector<QPair<int, int>> targets;
            for (const auto& s : m_sel)
                targets.append({ s.first,
                                 m_owner->m_anim.tracks[size_t(s.first)].keys[size_t(s.second)].frame });
            for (int ti : affectedTracks())
                sortTrack(ti);
            m_sel.clear();
            for (const auto& t : targets) {
                const auto& keys = m_owner->m_anim.tracks[size_t(t.first)].keys;
                for (int ki = 0; ki < int(keys.size()); ++ki)
                    if (keys[size_t(ki)].frame == t.second) { m_sel.append({ t.first, ki }); break; }
            }
            m_draggingKeys = false;
            update();
            m_owner->emitEdited();
        } else if (m_banding) {
            m_sel.clear();
            const Animation& a = m_owner->m_anim;
            for (int ti = 0; ti < int(a.tracks.size()); ++ti) {
                const int cy = trackTop(ti) + ROW() / 2;
                for (int ki = 0; ki < int(a.tracks[size_t(ti)].keys.size()); ++ki)
                    if (m_bandRect.contains(frameToX(a.tracks[size_t(ti)].keys[size_t(ki)].frame), cy))
                        m_sel.append({ ti, ki });
            }
            m_banding = false;
            update();
        }
        m_scrubbing = false;
    }

    // Ctrl+wheel zooms around the frame under the cursor; a horizontal gesture
    // (trackpad two-finger swipe / tilt wheel) pans the frame span; a plain
    // vertical wheel is left to the scroll area for row scrolling.
    void wheelEvent(QWheelEvent* e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            const int x = int(e->position().x());
            const int anchor = xToFrame(x);
            const double step = (e->angleDelta().y() > 0) ? 1.2 : 1.0 / 1.2;
            m_owner->setZoom(m_owner->m_zoom * step, anchor, std::max(x, GUT()));
            e->accept();
            return;
        }
        // pixelDelta is the trackpad's real pixel travel; angleDelta (eighths of
        // a degree) is the fallback for a notched wheel.
        const QPoint px = e->pixelDelta().isNull() ? e->angleDelta() / 2 : e->pixelDelta();
        if (px.x() != 0 && qAbs(px.x()) >= qAbs(px.y())) {
            m_owner->setScroll(m_owner->m_scroll - px.x() / pxPerFrame());
            e->accept();
            return;
        }
        e->ignore();
    }

    void resizeEvent(QResizeEvent*) override { m_owner->updateScrollRange(); }

    void contextMenuEvent(QContextMenuEvent* e) override
    {
        const QPoint pt = e->pos();
        const int rowi = rowAtY(pt.y());
        if (rowi < 0) return;
        auto& keys = m_owner->m_anim.tracks[size_t(rowi)].keys;
        const int cy = trackTop(rowi) + ROW() / 2;
        int hitKi = -1;
        for (int ki = 0; ki < int(keys.size()); ++ki)
            if (qAbs(pt.x() - frameToX(keys[size_t(ki)].frame)) <= HIT() && qAbs(pt.y() - cy) <= HIT()) {
                hitKi = ki; break;
            }
        if (hitKi < 0) return;
        if (!isSelected(rowi, hitKi)) m_sel = { { rowi, hitKi } };
        update();

        QMenu menu(this);
        auto addEase = [&](const QString& n, Easing es) {
            QAction* act = menu.addAction(n);
            act->setData(int(es));
        };
        addEase("Linear", Easing::Linear);
        addEase("Hold", Easing::Hold);
        addEase("Ease In", Easing::EaseIn);
        addEase("Ease Out", Easing::EaseOut);
        addEase("Ease In-Out", Easing::EaseInOut);
        menu.addSeparator();
        QAction* del = menu.addAction(QString("Delete keyframe%1").arg(m_sel.size() > 1 ? "s" : ""));

        QAction* chosen = menu.exec(e->globalPos());
        if (!chosen) return;
        if (chosen == del) {
            deleteSelection();
        } else {
            const Easing es = Easing(chosen->data().toInt());
            for (const auto& s : m_sel)
                m_owner->m_anim.tracks[size_t(s.first)].keys[size_t(s.second)].easing = es;
        }
        update();
        m_owner->emitEdited();
    }

private:
    bool isSelected(int ti, int ki) const
    {
        for (const auto& s : m_sel) if (s.first == ti && s.second == ki) return true;
        return false;
    }
    void beginKeyDrag(int refFrame)
    {
        m_draggingKeys = true;
        m_dragRefFrame = refFrame;
        m_dragOrigFrames.clear();
        for (const auto& s : m_sel)
            m_dragOrigFrames.append(m_owner->m_anim.tracks[size_t(s.first)].keys[size_t(s.second)].frame);
    }
    QVector<int> affectedTracks() const
    {
        QVector<int> out;
        for (const auto& s : m_sel) if (!out.contains(s.first)) out.append(s.first);
        return out;
    }
    void sortTrack(int ti)
    {
        auto& keys = m_owner->m_anim.tracks[size_t(ti)].keys;
        std::sort(keys.begin(), keys.end(),
                  [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
    }
    void deleteSelection()
    {
        QVector<QPair<int, int>> sel = m_sel;
        std::sort(sel.begin(), sel.end(), [](auto& a, auto& b) {
            return a.first != b.first ? a.first > b.first : a.second > b.second;
        });
        for (const auto& s : sel) {
            auto& keys = m_owner->m_anim.tracks[size_t(s.first)].keys;
            if (s.second >= 0 && s.second < int(keys.size()))
                keys.erase(keys.begin() + s.second);
        }
        auto& tracks = m_owner->m_anim.tracks;
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                     [](const Track& t) { return t.keys.empty(); }), tracks.end());
        m_sel.clear();
    }

public:
    // Whole working span fitted to the view (zoom 1×) — the zoom multiplies it.
    double fitPxPerFrame() const
    {
        const Animation& a = m_owner->m_anim;
        const int span = std::max(1, m_owner->dispEndFrame() - a.frameStart);
        return double(std::max(1, width() - GUT() - PADR())) / span;
    }
    double pxPerFrame() const { return fitPxPerFrame() * m_owner->m_zoom; }
    // Frames that fit in the frame area at the current zoom.
    double visibleFrames() const
    {
        return std::max(1, width() - GUT() - PADR()) / std::max(1e-9, pxPerFrame());
    }
    int frameToX(int f) const
    {
        return GUT() + int((f - m_owner->m_anim.frameStart - m_owner->m_scroll) * pxPerFrame());
    }
    int xToFrame(int x) const
    {
        return m_owner->m_anim.frameStart
             + qRound((x - GUT()) / pxPerFrame() + m_owner->m_scroll);
    }

private:

    // Row order is TimelineWidget::m_rows (clips with their tracks nested
    // under them), so clip/track index → y goes through a lookup.
    int clipCount() const { return m_owner->m_clips.size(); }
    int rowIndexOf(int clip, int track) const
    {
        for (int r = 0; r < m_owner->m_rows.size(); ++r)
            if (m_owner->m_rows[r].clip == clip && m_owner->m_rows[r].track == track) return r;
        return -1;
    }
    int clipTop(int i) const  { const int r = rowIndexOf(i, -1);  return r < 0 ? -1 : RUL() + r * ROW(); }
    int trackTop(int i) const { const int r = rowIndexOf(-1, i);  return r < 0 ? -1 : RUL() + r * ROW(); }
    int rowAtYRaw(int y) const
    {
        if (y < RUL()) return -1;
        const int r = (y - RUL()) / ROW();
        return (r >= 0 && r < m_owner->m_rows.size()) ? r : -1;
    }
    int clipAtY(int y) const
    {
        const int r = rowAtYRaw(y);
        return (r < 0) ? -1 : m_owner->m_rows[r].clip;
    }
    int rowAtY(int y) const      // → track index
    {
        const int r = rowAtYRaw(y);
        return (r < 0) ? -1 : m_owner->m_rows[r].track;
    }
    // -1 = in-point handle, +1 = out-point handle, 0 = not on a handle.
    int trimEdgeAt(int ci, int x) const
    {
        const TimelineWidget::ClipRow& c = m_owner->m_clips[ci];
        const int f0 = m_owner->m_anim.frameStart + c.offset;
        if (qAbs(x - frameToX(f0 + c.trimIn))      <= HIT()) return -1;
        if (qAbs(x - frameToX(f0 + c.trimOut + 1)) <= HIT()) return +1;
        return 0;
    }
    bool onClipBody(int ci, int x) const
    {
        const TimelineWidget::ClipRow& c = m_owner->m_clips[ci];
        const int f0 = m_owner->m_anim.frameStart + c.offset;
        return x >= frameToX(f0 + c.trimIn) && x <= frameToX(f0 + c.trimOut + 1);
    }
    void emitClip(const TimelineWidget::ClipRow& c)
    {
        update();
        if (m_owner->onClipEdited) m_owner->onClipEdited(c.mediaId, c.offset, c.trimIn, c.trimOut);
    }

    TimelineWidget* m_owner;
    int  m_trimClip    = -1;   // clip row whose edge is being dragged
    int  m_trimEdge    = 0;
    int  m_moveClip    = -1;   // clip row being dragged sideways
    int  m_moveRefFrm  = 0;
    int  m_moveOrigOff = 0;
    bool m_scrubbing   = false;
    bool m_draggingKeys = false;
    bool m_banding     = false;
    int  m_dragRefFrame = 0;
    QVector<QPair<int, int>> m_sel;
    QVector<int>             m_dragOrigFrames;
    QPoint m_bandOrigin;
    QRect  m_bandRect;
};

// ============================================================
//  TimelineWidget
// ============================================================

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("timelinePanel");
    setAttribute(Qt::WA_StyledBackground, true);   // paint #timelinePanel bg (QWidget subclass)

    auto* vl = new QVBoxLayout(this);
    // No side margins: the dopesheet rows bleed edge-to-edge. The control bar
    // keeps its own horizontal padding.
    vl->setContentsMargins(0, Ui::px(6), 0, Ui::px(6));
    vl->setSpacing(Ui::px(10));

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(Ui::px(16), 0, Ui::px(16), 0);
    bar->setSpacing(Ui::px(12));

    // ── Auto key (far left) ──────────────────────────────────
    m_autoKeyBtn = new QPushButton("Auto key");
    m_autoKeyBtn->setObjectName("autoKeyBtn");
    m_autoKeyBtn->setCheckable(true);
    m_autoKeyBtn->setCursor(Qt::PointingHandCursor);
    m_autoKeyBtn->setFixedHeight(Ui::px(Ui::kBoxHFull));
    connect(m_autoKeyBtn, &QPushButton::toggled, this,
            [this](bool on) { if (onAutoKeyToggled) onAutoKeyToggled(on); });
    bar->addWidget(m_autoKeyBtn);

    bar->addStretch(1);

    // ── Transport box (rounded, 6 buttons split by 1px lines) ─
    auto* box = new QFrame;
    box->setObjectName("transportBox");
    box->setFixedHeight(Ui::px(Ui::kBoxHFull));
    auto* bl = new QHBoxLayout(box);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    const QSize icoBox(Ui::px(18), Ui::px(14));
    auto mkBtn = [&](const QString& res, bool flip) {
        auto* b = new QPushButton;
        b->setObjectName("tlBtn");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(Ui::px(34), Ui::px(Ui::kBoxHFull - 2));   // fits inside the 48 box border
        b->setIcon(svgIcon(res, icoBox.width(), icoBox.height(), flip));
        b->setIconSize(icoBox);
        return b;
    };
    auto addSep = [&]() {
        auto* s = new QFrame;
        s->setObjectName("tlSep");
        s->setFixedWidth(1);
        bl->addWidget(s);
    };

    auto* toStart = mkBtn(":/icons/tl_end.svg",  false);   // |◄
    auto* prevK   = mkBtn(":/icons/tl_key.svg",  false);   // ◄◇
    auto* rewind  = mkBtn(":/icons/tl_play.svg", true);    // ◄ (play flipped)
    m_playBtn     = mkBtn(":/icons/tl_play.svg", false);   // ►
    m_playBtn->setCheckable(true);
    auto* nextK   = mkBtn(":/icons/tl_key.svg",  true);    // ◇►
    auto* toEnd   = mkBtn(":/icons/tl_end.svg",  true);    // ►|

    bl->addWidget(toStart); addSep();
    bl->addWidget(prevK);   addSep();
    bl->addWidget(rewind);  addSep();
    bl->addWidget(m_playBtn); addSep();
    bl->addWidget(nextK);   addSep();
    bl->addWidget(toEnd);

    connect(toStart, &QPushButton::clicked, this, [this] { scrubTo(m_anim.frameStart); });
    connect(toEnd,   &QPushButton::clicked, this, [this] { scrubTo(m_anim.frameEnd); });
    connect(prevK,   &QPushButton::clicked, this, [this] { jumpKey(-1); });
    connect(nextK,   &QPushButton::clicked, this, [this] { jumpKey(+1); });
    connect(rewind,  &QPushButton::clicked, this, [this] { scrubTo(m_anim.playhead - 1); });
    connect(m_playBtn, &QPushButton::toggled, this, [this](bool on) {
        m_playBtn->setIcon(on ? pauseIcon(Ui::px(14)) : playIcon());
        if (onPlayToggled) onPlayToggled(on);
    });
    bar->addWidget(box);

    bar->addStretch(1);

    // ── Current-frame box ────────────────────────────────────
    m_frameSpin = new QSpinBox;
    m_frameSpin->setObjectName("tlSpin");
    m_frameSpin->setRange(0, 1000000);
    m_frameSpin->setValue(m_anim.playhead);
    m_frameSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_frameSpin->setAlignment(Qt::AlignCenter);
    m_frameSpin->setFixedHeight(Ui::px(Ui::kBoxHFull));
    m_frameSpin->setMinimumWidth(Ui::px(66));
    connect(m_frameSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_updating) return;
        scrubTo(v);
    });
    bar->addWidget(m_frameSpin);

    // ── Combined start / end box (single rounded container) ──
    auto* rangeBox = new QFrame;
    rangeBox->setObjectName("tlRangeBox");
    rangeBox->setFixedHeight(Ui::px(Ui::kBoxHFull));
    auto* rl = new QHBoxLayout(rangeBox);
    rl->setContentsMargins(Ui::px(14), 0, Ui::px(14), 0);
    rl->setSpacing(Ui::px(8));

    auto rangeChanged = [this](int) {
        if (m_updating) return;
        m_anim.frameStart = m_startSpin->value();
        m_anim.frameEnd   = qMax(m_startSpin->value() + 1, m_endSpin->value());
        if (m_canvas) m_canvas->update();
        emitEdited();
    };
    auto mkInner = [&](QSpinBox*& sp, const QString& lbl, int lo, int hi, int def) {
        auto* l = new QLabel(lbl);
        l->setObjectName("tlRangeLbl");
        rl->addWidget(l);
        sp = new QSpinBox;
        sp->setObjectName("tlInnerSpin");
        sp->setRange(lo, hi);
        sp->setValue(def);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        sp->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        sp->setFixedWidth(Ui::px(56));   // fits 4 digits
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, rangeChanged);
        rl->addWidget(sp);
    };
    mkInner(m_startSpin, "start", 0, 1000000, m_anim.frameStart);
    auto* rsep = new QFrame;
    rsep->setObjectName("tlSep");
    rsep->setFixedWidth(1);
    rl->addWidget(rsep);
    mkInner(m_endSpin, "end", 1, 1000000, m_anim.frameEnd);
    bar->addWidget(rangeBox);

    // ── FPS dropdown — frame-hold effect: quantizes which native frame gets
    // rendered (stutter/stop-motion look), duration and export length are
    // untouched (see Animation::steppedFrame).
    auto* fpsLbl = new QLabel("fps");
    fpsLbl->setObjectName("tlRangeLbl");
    bar->addWidget(fpsLbl);

    m_fpsPicker = new PopupPicker(1);
    m_fpsPicker->setMinimumWidth(Ui::px(60));
    m_fpsPicker->setEntries({
        { 24, "24" },
        { 15, "15" },
        { 12, "12" },
        { 8,  "8"  },
    });
    m_fpsPicker->onSelected = [this](QVariant v) {
        if (m_updating) return;
        m_anim.stepFps = v.toInt();
        emitEdited();
    };
    bar->addWidget(m_fpsPicker);

    vl->addLayout(bar);

    m_canvas = new TimelineCanvas(this);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("timelineScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_canvas);
    vl->addWidget(scroll, 1);

    // ── Bottom strip: horizontal pan + zoom ──────────────────
    // The canvas paints its own (fixed) label gutter, so panning is a frame
    // offset it applies while painting, not a real QScrollArea scroll.
    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(Ui::px(16), 0, Ui::px(16), 0);
    bottom->setSpacing(Ui::px(12));

    m_hbar = new QScrollBar(Qt::Horizontal);
    m_hbar->setObjectName("timelineHBar");
    connect(m_hbar, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_scroll = v;
        if (m_canvas) m_canvas->update();
    });
    bottom->addWidget(m_hbar, 1);

    auto* zoomIcon = new QLabel;
    zoomIcon->setPixmap(magnifierPixmap(Ui::px(16)));
    bottom->addWidget(zoomIcon);

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setObjectName("timelineZoom");
    m_zoomSlider->setRange(100, 4000);          // 1× … 40×
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(Ui::px(147));
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        // Zoom around the middle of the visible span, so the slider doesn't
        // yank the view back to frame 0.
        const int mid = m_canvas ? int(m_scroll + m_canvas->visibleFrames() / 2.0)
                                 : 0;
        setZoom(v / 100.0, m_anim.frameStart + mid,
                GUT() + (width() - GUT()) / 2);
    });
    bottom->addWidget(m_zoomSlider);

    vl->addLayout(bottom);

    syncControls();
    updateScrollRange();
}

void TimelineWidget::updateScrollRange()
{
    if (!m_canvas || !m_hbar) return;
    const int span = dispEndFrame() - m_anim.frameStart;
    const int vis  = int(m_canvas->visibleFrames());
    const int maxScroll = std::max(0, span - vis);
    m_scroll = qBound(0.0, m_scroll, double(maxScroll));

    const bool prev = m_updating;
    m_updating = true;
    m_hbar->setRange(0, maxScroll);
    m_hbar->setPageStep(std::max(1, vis));
    m_hbar->setSingleStep(std::max(1, vis / 10));
    m_hbar->setValue(int(m_scroll));
    m_updating = prev;

    m_hbar->setEnabled(maxScroll > 0);
    m_canvas->update();
}

void TimelineWidget::setZoom(double z, int anchorFrame, int anchorX)
{
    m_zoom = qBound(1.0, z, 40.0);
    if (m_canvas)   // keep anchorFrame pinned under anchorX
        m_scroll = (anchorFrame - m_anim.frameStart)
                 - (anchorX - GUT()) / m_canvas->pxPerFrame();
    if (m_zoomSlider) {
        const bool prev = m_updating;
        m_updating = true;
        m_zoomSlider->setValue(int(m_zoom * 100));
        m_updating = prev;
    }
    updateScrollRange();
}

void TimelineWidget::setScroll(double firstFrame)
{
    m_scroll = firstFrame;
    updateScrollRange();
}

void TimelineWidget::setAnimation(const Animation& a)
{
    m_updating = true;
    m_anim = a;
    syncControls();
    m_updating = false;
    rebuildRows();         // tracks may have appeared/disappeared
    updateCanvasHeight();
    updateScrollRange();   // the frame range (and so the scrollable span) moved
}

void TimelineWidget::setClips(const QVector<ClipRow>& clips, const QHash<int, int>& layerMedia)
{
    m_clips      = clips;
    m_layerMedia = layerMedia;
    rebuildRows();
    updateCanvasHeight();
    updateScrollRange();
}

int TimelineWidget::dispEndFrame() const
{
    int end = std::max(m_anim.frameEnd, m_anim.frameStart);
    for (const ClipRow& c : m_clips)
        end = std::max(end, m_anim.frameStart + c.offset + c.length - 1);
    return end + kTailFrames;
}

// Row order: every clip followed by the keyframe tracks that animate one of
// its layers (drawn indented), then any track with no clip of its own.
void TimelineWidget::rebuildRows()
{
    m_rows.clear();
    QVector<bool> placed(int(m_anim.tracks.size()), false);

    for (int ci = 0; ci < m_clips.size(); ++ci) {
        m_rows.append({ ci, -1, false });
        for (int ti = 0; ti < int(m_anim.tracks.size()); ++ti) {
            if (placed[ti]) continue;
            if (m_layerMedia.value(m_anim.tracks[size_t(ti)].layerId, -1) != m_clips[ci].mediaId)
                continue;
            m_rows.append({ -1, ti, true });
            placed[ti] = true;
        }
    }
    for (int ti = 0; ti < int(m_anim.tracks.size()); ++ti)
        if (!placed[ti]) m_rows.append({ -1, ti, false });
}

void TimelineWidget::updateCanvasHeight()
{
    if (!m_canvas) return;
    const int rows = std::max(3, int(m_rows.size()));
    m_canvas->setMinimumHeight(RUL() + rows * ROW() + Ui::px(4));
    m_canvas->update();
}

void TimelineWidget::setPlayheadSilent(int frame)
{
    m_anim.playhead = frame;
    if (m_frameSpin) {
        m_frameSpin->blockSignals(true);
        m_frameSpin->setValue(frame);
        m_frameSpin->blockSignals(false);
    }
    if (m_canvas) m_canvas->update();
}

bool TimelineWidget::autoKey() const
{
    return m_autoKeyBtn && m_autoKeyBtn->isChecked();
}

void TimelineWidget::togglePlay()
{
    if (m_playBtn) m_playBtn->toggle();
}

void TimelineWidget::setPlayingSilent(bool on)
{
    if (!m_playBtn) return;
    m_playBtn->blockSignals(true);
    m_playBtn->setChecked(on);
    m_playBtn->setIcon(on ? pauseIcon(Ui::px(14)) : playIcon());
    m_playBtn->blockSignals(false);
}

void TimelineWidget::copyKeys()  { if (m_canvas) m_canvas->copySelection(); }
void TimelineWidget::pasteKeys() { if (m_canvas) m_canvas->pasteAtPlayhead(); }
bool TimelineWidget::deleteSelectedKeys() { return m_canvas && m_canvas->deleteSelected(); }

void TimelineWidget::syncControls()
{
    const bool prev = m_updating;
    m_updating = true;
    m_frameSpin->setRange(m_anim.frameStart, dispEndFrame());
    m_frameSpin->setValue(m_anim.playhead);
    m_startSpin->setValue(m_anim.frameStart);
    m_endSpin->setValue(m_anim.frameEnd);
    m_fpsPicker->setValue(m_anim.stepFps);
    m_updating = prev;
}

void TimelineWidget::emitEdited()
{
    if (!m_updating && onAnimEdited) onAnimEdited();
}

void TimelineWidget::scrubTo(int frame)
{
    frame = qBound(m_anim.frameStart, frame, dispEndFrame());
    m_anim.playhead = frame;
    if (m_frameSpin) {
        m_frameSpin->blockSignals(true);
        m_frameSpin->setValue(frame);
        m_frameSpin->blockSignals(false);
    }
    if (m_canvas) m_canvas->update();
    if (onPlayheadChanged) onPlayheadChanged(frame);
}

void TimelineWidget::jumpKey(int dir)
{
    int best = -1;
    for (const Track& t : m_anim.tracks)
        for (const Keyframe& k : t.keys) {
            if (dir < 0 && k.frame < m_anim.playhead)
                best = (best < 0) ? k.frame : std::max(best, k.frame);
            if (dir > 0 && k.frame > m_anim.playhead)
                best = (best < 0) ? k.frame : std::min(best, k.frame);
        }
    if (best >= 0) scrubTo(best);
}
