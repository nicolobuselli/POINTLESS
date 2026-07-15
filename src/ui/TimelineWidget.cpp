#include "TimelineWidget.h"
#include "Theme.h"
#include "../core/AnimParams.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
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

// The timeline always offers at least this many frames of working space, even
// when the animation's end frame is earlier — so keyframes can be placed past
// the end of the animation.
constexpr int kMinTimelineFrames = 200;

// Last frame the timeline displays / accepts keyframes at: the animation end,
// or the minimum working span, whichever is larger.
int dispEnd(const Animation& a)
{
    return std::max(a.frameEnd, a.frameStart + kMinTimelineFrames - 1);
}

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
    const int r = Ui::px(8);
    const QColor fill = selected ? QColor("#FF6A00") : QColor("#E8E8E8");
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

        p.fillRect(rect(), QColor("#272727"));

        // ── Alternating per-track row backgrounds (full width) ────
        for (int i = 0; i < int(a.tracks.size()); ++i) {
            const int y0 = rul + i * row;
            // First animated row matches the panel colour; stripes alternate.
            p.fillRect(QRect(0, y0, width(), row),
                       (i % 2) ? QColor("#2D2D2D") : QColor("#272727"));
        }

        // ── Vertical grid: major every `majorStep`, minor at half ──
        const double ppf = pxPerFrame();
        int majorStep = 20;
        while (majorStep * ppf < Ui::px(48)) majorStep *= 2;   // keep labels readable
        const int minorStep = std::max(1, majorStep / 2);
        const int gridBottom = height();   // run lines all the way to the bottom
        const int dEnd       = dispEnd(a);

        for (int f = a.frameStart; f <= dEnd; ++f) {
            const int rel = f - a.frameStart;
            const int x = frameToX(f);
            if (x < gut) continue;
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

        // ── Ruler header band (panel colour, set off by a divider) ─
        p.fillRect(QRect(0, 0, width(), rul), QColor("#272727"));
        p.setPen(QColor("#3B3B3B"));
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

        // ── Track labels + keyframes ─────────────────────────────
        QFont lf = p.font(); lf.setPixelSize(Ui::px(17)); p.setFont(lf);
        for (int i = 0; i < int(a.tracks.size()); ++i) {
            const int y0 = rul + i * row;
            const int cy = y0 + row / 2;
            p.setPen(QColor("#C8C8C8"));
            p.drawText(QRect(Ui::px(20), y0, gut - Ui::px(26), row),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromUtf8(paramDesc(a.tracks[size_t(i)].param).label));
            for (int ki = 0; ki < int(a.tracks[size_t(i)].keys.size()); ++ki)
                drawDiamond(p, frameToX(a.tracks[size_t(i)].keys[size_t(ki)].frame), cy,
                            a.tracks[size_t(i)].keys[size_t(ki)].easing, isSelected(i, ki));
        }

        // ── Playhead: orange flag (with frame number) + bar ─────
        const int px = frameToX(a.playhead);
        if (px >= gut) {
            p.setRenderHint(QPainter::Antialiasing, true);
            QFont hf = p.font(); hf.setPixelSize(Ui::px(12)); hf.setBold(true); p.setFont(hf);
            const QString fnum = QString::number(a.playhead);
            const int tw = p.fontMetrics().horizontalAdvance(fnum);
            const int fw = std::max(Ui::px(22), tw + Ui::px(12));
            const int fh = Ui::px(20);
            const QRectF flag(px - fw / 2.0, 0, fw, fh);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#FF6A00"));
            p.drawRoundedRect(flag, Ui::px(3), Ui::px(3));
            p.setPen(QColor("#FFFFFF"));
            p.drawText(flag, Qt::AlignCenter, fnum);
            p.fillRect(QRectF(px - Ui::px(1), fh - Ui::px(2), Ui::px(2), height() - fh + Ui::px(2)),
                       QColor("#FF6A00"));
        }

        // ── Rubber-band selection rectangle ──────────────────────
        if (m_banding) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(QColor("#FF6A00"), 1, Qt::DashLine));
            p.setBrush(QColor(255, 106, 0, 40));
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

        const int rowi = rowAtY(pt.y());
        if (rowi >= 0) {
            auto& keys = m_owner->m_anim.tracks[size_t(rowi)].keys;
            const int cy = RUL() + rowi * ROW() + ROW() / 2;
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
        if (m_draggingKeys) {
            const Animation& a = m_owner->m_anim;
            const int delta = xToFrame(e->pos().x()) - m_dragRefFrame;
            for (int i = 0; i < m_sel.size(); ++i) {
                const int ti = m_sel[i].first, ki = m_sel[i].second;
                m_owner->m_anim.tracks[size_t(ti)].keys[size_t(ki)].frame =
                    qBound(a.frameStart, m_dragOrigFrames[i] + delta, dispEnd(a));
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
                const int cy = RUL() + ti * ROW() + ROW() / 2;
                for (int ki = 0; ki < int(a.tracks[size_t(ti)].keys.size()); ++ki)
                    if (m_bandRect.contains(frameToX(a.tracks[size_t(ti)].keys[size_t(ki)].frame), cy))
                        m_sel.append({ ti, ki });
            }
            m_banding = false;
            update();
        }
        m_scrubbing = false;
    }

    void contextMenuEvent(QContextMenuEvent* e) override
    {
        const QPoint pt = e->pos();
        const int rowi = rowAtY(pt.y());
        if (rowi < 0) return;
        auto& keys = m_owner->m_anim.tracks[size_t(rowi)].keys;
        const int cy = RUL() + rowi * ROW() + ROW() / 2;
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

    double pxPerFrame() const
    {
        const Animation& a = m_owner->m_anim;
        const int span = std::max(1, dispEnd(a) - a.frameStart);
        return double(std::max(1, width() - GUT() - PADR())) / span;
    }
    int frameToX(int f) const { return GUT() + int((f - m_owner->m_anim.frameStart) * pxPerFrame()); }
    int xToFrame(int x) const { return m_owner->m_anim.frameStart + qRound((x - GUT()) / pxPerFrame()); }
    int rowAtY(int y) const
    {
        if (y < RUL()) return -1;
        const int r = (y - RUL()) / ROW();
        return (r >= 0 && r < int(m_owner->m_anim.tracks.size())) ? r : -1;
    }

    TimelineWidget* m_owner;
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
    vl->setContentsMargins(0, Ui::px(12), 0, Ui::px(10));
    vl->setSpacing(Ui::px(10));

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(Ui::px(20), 0, Ui::px(20), 0);
    bar->setSpacing(Ui::px(12));

    // ── Auto key (far left) ──────────────────────────────────
    m_autoKeyBtn = new QPushButton("Auto key");
    m_autoKeyBtn->setObjectName("autoKeyBtn");
    m_autoKeyBtn->setCheckable(true);
    m_autoKeyBtn->setCursor(Qt::PointingHandCursor);
    m_autoKeyBtn->setFixedHeight(Ui::px(Ui::kBoxH));
    connect(m_autoKeyBtn, &QPushButton::toggled, this,
            [this](bool on) { if (onAutoKeyToggled) onAutoKeyToggled(on); });
    bar->addWidget(m_autoKeyBtn);

    bar->addStretch(1);

    // ── Transport box (rounded, 6 buttons split by 1px lines) ─
    auto* box = new QFrame;
    box->setObjectName("transportBox");
    box->setFixedHeight(Ui::px(Ui::kBoxH));
    auto* bl = new QHBoxLayout(box);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    const QSize icoBox(Ui::px(18), Ui::px(14));
    auto mkBtn = [&](const QString& res, bool flip) {
        auto* b = new QPushButton;
        b->setObjectName("tlBtn");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(Ui::px(34), Ui::px(Ui::kBoxH - 2));   // fits inside the 48 box border
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
    m_frameSpin->setFixedHeight(Ui::px(Ui::kBoxH));
    m_frameSpin->setMinimumWidth(Ui::px(66));
    connect(m_frameSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_updating) return;
        scrubTo(v);
    });
    bar->addWidget(m_frameSpin);

    // ── Combined start / end box (single rounded container) ──
    auto* rangeBox = new QFrame;
    rangeBox->setObjectName("tlRangeBox");
    rangeBox->setFixedHeight(Ui::px(Ui::kBoxH));
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
        sp->setFixedWidth(Ui::px(46));
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

    // FPS kept alive (hidden) so export keeps a frame rate.
    m_fpsSpin = new QSpinBox;
    m_fpsSpin->setRange(1, 240);
    m_fpsSpin->setValue(m_anim.fps);
    m_fpsSpin->setVisible(false);

    vl->addLayout(bar);

    m_canvas = new TimelineCanvas(this);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("timelineScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_canvas);
    vl->addWidget(scroll, 1);

    syncControls();
}

void TimelineWidget::setAnimation(const Animation& a)
{
    m_updating = true;
    m_anim = a;
    syncControls();
    m_updating = false;
    if (m_canvas) {
        const int rows = std::max(3, int(m_anim.tracks.size()));
        m_canvas->setMinimumHeight(RUL() + rows * ROW() + Ui::px(4));
        m_canvas->update();
    }
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
    m_frameSpin->setRange(m_anim.frameStart, dispEnd(m_anim));
    m_frameSpin->setValue(m_anim.playhead);
    m_startSpin->setValue(m_anim.frameStart);
    m_endSpin->setValue(m_anim.frameEnd);
    m_fpsSpin->setValue(m_anim.fps);
    m_updating = prev;
}

void TimelineWidget::emitEdited()
{
    if (!m_updating && onAnimEdited) onAnimEdited();
}

void TimelineWidget::scrubTo(int frame)
{
    frame = qBound(m_anim.frameStart, frame, dispEnd(m_anim));
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
