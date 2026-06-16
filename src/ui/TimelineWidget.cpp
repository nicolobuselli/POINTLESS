#include "TimelineWidget.h"
#include "../core/AnimParams.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kGutter = 132;   // label column width
constexpr int kRuler  = 22;    // ruler strip height
constexpr int kRow    = 22;    // per-track row height
constexpr int kPadR   = 12;    // right padding
constexpr int kHit    = 7;     // diamond hit radius

// Clipboard for keyframe copy/paste (shared across the timeline).
struct CopiedKey { int layerId; ParamId param; int frameOff; double value; Easing easing; };
QVector<CopiedKey> g_keyClip;

const char* kTransportQss =
    "QPushButton{background:#3B3B3B;border:1px solid #5D5D5D;border-radius:4px;"
    "color:#C8C8C8;font-size:11pt;}"
    "QPushButton:hover{border-color:#828282;}"
    "QPushButton:checked{background:#484848;border-color:#9A9A9A;color:#F0F0F0;}";

void drawDiamond(QPainter& p, int x, int y, Easing e, bool selected)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const int r = 5;
    const QColor fill = selected ? QColor("#FD5A1F") : QColor("#E8E8E8");
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
        setMinimumHeight(kRuler + kRow * 3);
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

    void pasteAtPlayhead()
    {
        if (g_keyClip.isEmpty()) return;
        const int base = m_owner->m_anim.playhead;
        for (const auto& c : g_keyClip)
            upsertKey(m_owner->m_anim, c.layerId, c.param, base + c.frameOff, c.value, c.easing);

        // Select the freshly pasted keys for immediate feedback.
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

        p.fillRect(rect(), QColor("#1E1E1E"));
        p.fillRect(QRect(0, 0, kGutter, height()), QColor("#242424"));

        // Ruler.
        p.setPen(QColor("#3A3A3A"));
        p.drawLine(0, kRuler, width(), kRuler);
        const double ppf = pxPerFrame();
        int labelStep = std::max(1, int(std::ceil(42.0 / ppf)));
        for (int nice : { 1, 2, 5, 10, 25, 50, 100, 250, 500 })
            if (labelStep <= nice) { labelStep = nice; break; }
        for (int f = a.frameStart; f <= a.frameEnd; ++f) {
            if ((f - a.frameStart) % labelStep != 0) continue;
            const int x = frameToX(f);
            p.setPen(QColor("#3A3A3A"));
            p.drawLine(x, kRuler - 5, x, kRuler);
            p.setPen(QColor("#9A9A9A"));
            p.drawText(QRect(x - 22, 2, 44, kRuler - 6), Qt::AlignCenter, QString::number(f));
        }

        QFont fnt = p.font(); fnt.setPointSizeF(8.5); p.setFont(fnt);
        for (int i = 0; i < int(a.tracks.size()); ++i) {
            const int y0 = kRuler + i * kRow;
            const int cy = y0 + kRow / 2;
            if (i % 2) p.fillRect(QRect(kGutter, y0, width() - kGutter, kRow), QColor("#212121"));
            p.setPen(QColor("#C8C8C8"));
            p.drawText(QRect(10, y0, kGutter - 14, kRow), Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromUtf8(paramDesc(a.tracks[size_t(i)].param).label));
            p.setPen(QColor("#2C2C2C"));
            p.drawLine(0, y0 + kRow, width(), y0 + kRow);
            for (int ki = 0; ki < int(a.tracks[size_t(i)].keys.size()); ++ki)
                drawDiamond(p, frameToX(a.tracks[size_t(i)].keys[size_t(ki)].frame), cy,
                            a.tracks[size_t(i)].keys[size_t(ki)].easing, isSelected(i, ki));
        }

        // Playhead.
        const int px = frameToX(a.playhead);
        p.setPen(QPen(QColor("#FD5A1F"), 1));
        p.drawLine(px, 0, px, height());
        p.setBrush(QColor("#FD5A1F")); p.setPen(Qt::NoPen);
        QPolygon tri; tri << QPoint(px - 4, 0) << QPoint(px + 4, 0) << QPoint(px, 8);
        p.drawPolygon(tri);

        // Rubber-band selection rectangle.
        if (m_banding) {
            p.setPen(QPen(QColor("#FD5A1F"), 1, Qt::DashLine));
            p.setBrush(QColor(253, 90, 31, 40));
            p.drawRect(m_bandRect);
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        const QPoint pt = e->pos();
        if (pt.x() < kGutter) return;

        if (pt.y() < kRuler) {
            m_scrubbing = true;
            m_owner->scrubTo(xToFrame(pt.x()));
            return;
        }

        const int row = rowAtY(pt.y());
        if (row >= 0) {
            auto& keys = m_owner->m_anim.tracks[size_t(row)].keys;
            const int cy = kRuler + row * kRow + kRow / 2;
            for (int ki = 0; ki < int(keys.size()); ++ki) {
                if (qAbs(pt.x() - frameToX(keys[size_t(ki)].frame)) <= kHit
                    && qAbs(pt.y() - cy) <= kHit) {
                    if (!isSelected(row, ki))
                        m_sel = { { row, ki } };       // start a fresh selection
                    beginKeyDrag(xToFrame(pt.x()));
                    update();
                    return;
                }
            }
        }
        // Empty area → rubber-band select.
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
                    qBound(a.frameStart, m_dragOrigFrames[i] + delta, a.frameEnd);
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
            // Remember target frames, sort, then rebuild selection by frame.
            QVector<QPair<int, int>> targets;   // (trackIdx, frame)
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
                const int cy = kRuler + ti * kRow + kRow / 2;
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
        const int row = rowAtY(pt.y());
        if (row < 0) return;
        auto& keys = m_owner->m_anim.tracks[size_t(row)].keys;
        const int cy = kRuler + row * kRow + kRow / 2;
        int hitKi = -1;
        for (int ki = 0; ki < int(keys.size()); ++ki)
            if (qAbs(pt.x() - frameToX(keys[size_t(ki)].frame)) <= kHit && qAbs(pt.y() - cy) <= kHit) {
                hitKi = ki; break;
            }
        if (hitKi < 0) return;
        if (!isSelected(row, hitKi)) m_sel = { { row, hitKi } };
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
        // Remove highest key indices first per track, then empty tracks.
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
        const int span = std::max(1, a.frameEnd - a.frameStart);
        return double(std::max(1, width() - kGutter - kPadR)) / span;
    }
    int frameToX(int f) const { return kGutter + int((f - m_owner->m_anim.frameStart) * pxPerFrame()); }
    int xToFrame(int x) const { return m_owner->m_anim.frameStart + qRound((x - kGutter) / pxPerFrame()); }
    int rowAtY(int y) const
    {
        if (y < kRuler) return -1;
        const int r = (y - kRuler) / kRow;
        return (r >= 0 && r < int(m_owner->m_anim.tracks.size())) ? r : -1;
    }

    TimelineWidget* m_owner;
    bool m_scrubbing   = false;
    bool m_draggingKeys = false;
    bool m_banding     = false;
    int  m_dragRefFrame = 0;
    QVector<QPair<int, int>> m_sel;          // (trackIdx, keyIdx)
    QVector<int>             m_dragOrigFrames;
    QPoint m_bandOrigin;
    QRect  m_bandRect;
};

// ============================================================
//  TimelineWidget
// ============================================================

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(8, 6, 8, 6);
    vl->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);

    m_autoKeyBtn = new QPushButton("Auto-key");
    m_autoKeyBtn->setCheckable(true);
    m_autoKeyBtn->setCursor(Qt::PointingHandCursor);
    m_autoKeyBtn->setStyleSheet(
        "QPushButton{background:#3B3B3B;border:1px solid #5D5D5D;border-radius:4px;"
        "color:#B2B2B2;font-size:9pt;padding:4px 10px;}"
        "QPushButton:checked{background:#7A1F1F;border-color:#C0392B;color:#FFE3E0;}"
        "QPushButton:hover{border-color:#828282;}");
    connect(m_autoKeyBtn, &QPushButton::toggled, this,
            [this](bool on) { if (onAutoKeyToggled) onAutoKeyToggled(on); });
    bar->addWidget(m_autoKeyBtn);

    auto mkBtn = [&](const QString& t) {
        auto* b = new QPushButton(t);
        b->setFixedHeight(26);
        b->setFixedWidth(30);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(kTransportQss);
        return b;
    };
    auto* toStart = mkBtn(QString::fromUtf8("|◄"));
    auto* prevK   = mkBtn(QString::fromUtf8("◄"));
    m_playBtn     = mkBtn(QString::fromUtf8("►"));
    m_playBtn->setCheckable(true);
    auto* nextK   = mkBtn(QString::fromUtf8("►"));
    auto* toEnd   = mkBtn(QString::fromUtf8("►|"));
    connect(toStart, &QPushButton::clicked, this, [this] { scrubTo(m_anim.frameStart); });
    connect(toEnd,   &QPushButton::clicked, this, [this] { scrubTo(m_anim.frameEnd); });
    connect(prevK,   &QPushButton::clicked, this, [this] { jumpKey(-1); });
    connect(nextK,   &QPushButton::clicked, this, [this] { jumpKey(+1); });
    connect(m_playBtn, &QPushButton::toggled, this, [this](bool on) {
        m_playBtn->setText(QString::fromUtf8(on ? "❚❚" : "►"));
        if (onPlayToggled) onPlayToggled(on);
    });
    bar->addWidget(toStart); bar->addWidget(prevK);
    bar->addWidget(m_playBtn);
    bar->addWidget(nextK); bar->addWidget(toEnd);

    bar->addStretch(1);

    auto addRange = [&](const QString& lbl, QSpinBox*& sp, int lo, int hi, int def) {
        auto* l = new QLabel(lbl);
        l->setStyleSheet("color:#9A9A9A; font-size:9pt;");
        bar->addWidget(l);
        sp = new QSpinBox;
        sp->setRange(lo, hi);
        sp->setValue(def);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);   // type directly, no arrows
        sp->setFixedWidth(54);
        sp->setAlignment(Qt::AlignCenter);
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (m_updating) return;
            m_anim.frameStart = m_startSpin->value();
            m_anim.frameEnd   = qMax(m_startSpin->value(), m_endSpin->value());
            m_anim.fps        = m_fpsSpin->value();
            if (m_canvas) m_canvas->update();
            emitEdited();
        });
        bar->addWidget(sp);
    };
    addRange("Start", m_startSpin, 0, 1000000, m_anim.frameStart);
    addRange("End",   m_endSpin,   1, 1000000, m_anim.frameEnd);
    addRange("FPS",   m_fpsSpin,   1, 240,     m_anim.fps);

    vl->addLayout(bar);

    m_canvas = new TimelineCanvas(this);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_canvas);   // scrolls vertically when tracks overflow
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
        m_canvas->setMinimumHeight(kRuler + rows * kRow + 4);   // grows → scroll area scrolls
        m_canvas->update();
    }
}

void TimelineWidget::setPlayheadSilent(int frame)
{
    m_anim.playhead = frame;
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

void TimelineWidget::copyKeys()  { if (m_canvas) m_canvas->copySelection(); }
void TimelineWidget::pasteKeys() { if (m_canvas) m_canvas->pasteAtPlayhead(); }

void TimelineWidget::syncControls()
{
    const bool prev = m_updating;
    m_updating = true;
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
    frame = qBound(m_anim.frameStart, frame, m_anim.frameEnd);
    m_anim.playhead = frame;
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
