#include "replaydialog.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "src/Menu/uipalette.h"

// ================================================================
//  ReplayView
// ================================================================

ReplayView::ReplayView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(520, 420);
}

void ReplayView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(8, 12, 16));
    const QPointF c = rect().center();
    const double scale = std::min(width(), height()) / 2.0 / 48.0;     // 48 world units to the edge
    // Range rings every 10 km (1 unit = 100 m)
    p.setPen(QPen(QColor(60, 120, 90, 70), 1));
    for (int r = 12; r <= 48; r += 12)
        p.drawEllipse(c, r * scale, r * scale);
    p.drawLine(QPointF(c.x(), 0), QPointF(c.x(), height()));
    p.drawLine(QPointF(0, c.y()), QPointF(width(), c.y()));
    if (!m_frame)
        return;
    auto at = [&](const QPointF &world) { return c + world * scale; };

    // Shots: dots, beams as dashed light, missiles bigger
    for (const ReplayFrame::Shot &s : m_frame->shots) {
        const QColor color = s.friendly ? QColor(110, 200, 255) : UiPalette::hostile();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const double r = s.missile ? 3.0 : s.beam ? 1.6 : 1.8;
        p.drawEllipse(at(s.pos), r, r);
    }
    // Ships: triangles pointing where they head
    for (const ReplayFrame::Ship &s : m_frame->ships) {
        const QColor color = s.side == 0 ? QColor(240, 220, 120) : s.side == 1 ? UiPalette::hostile() : QColor(90, 170, 255);
        const double size = s.side == 0 ? 14 : s.mothership ? 10 : 5;
        QPointF dir = s.facing;
        const double len = std::hypot(dir.x(), dir.y());
        dir = len > 1e-4 ? dir / len : QPointF(1, 0);
        const QPointF side(-dir.y(), dir.x());
        const QPointF pos = at(s.pos);
        QPolygonF triangle{pos + dir * size, pos - dir * size * 0.6 + side * size * 0.6, pos - dir * size * 0.6 - side * size * 0.6};
        p.setPen(QPen(color.darker(150), 1));
        p.setBrush(color);
        p.drawPolygon(triangle);
        if (s.side != 0) {
            const QRectF bar(pos.x() - size, pos.y() - size - 6, size * 2, 3);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(60, 60, 60));
            p.drawRect(bar);
            p.setBrush(UiPalette::health(s.health));
            p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * s.health / 100.0, bar.height()));
        }
    }
    // Your hull and shield
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(12);
    font.setBold(true);
    p.setFont(font);
    auto bar = [&](int y, const QString &label, float value, const QColor &color) {
        p.setPen(QColor(220, 220, 210));
        p.drawText(QRectF(10, y, 60, 14), Qt::AlignVCenter, label);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(50, 50, 50));
        p.drawRect(QRectF(70, y + 4, 140, 7));
        p.setBrush(color);
        p.drawRect(QRectF(70, y + 4, 140 * std::clamp(value, 0.0f, 100.0f) / 100.0f, 7));
    };
    bar(10, tr("HULL"), m_frame->hull, UiPalette::health(m_frame->hull));
    bar(28, tr("SHIELD"), m_frame->shield, QColor(90, 170, 255));
}

// ================================================================
//  ReplayDialog
// ================================================================

ReplayDialog::ReplayDialog(QWidget *parent)
    : QDialog(parent)
    , m_view(new ReplayView(this))
    , m_timeline(new QSlider(Qt::Horizontal, this))
    , m_play(new QPushButton(this))
    , m_speed(new QPushButton(this))
    , m_time(new QLabel(this))
    , m_log(new QListWidget(this))
    , m_timer(new QTimer(this))
{
    setWindowTitle(tr("Battle replay"));
    setStyleSheet("QDialog { background: #0e1014; } QLabel { color: #e8e2c8; }"
                  "QListWidget { background: #14171c; color: #e8e2c8; border: 1px solid #333; }"
                  "QPushButton { background: #1c1a12; color: #e8e2c8; border: 1px solid #8a8160; padding: 4px 12px; }");
    resize(1000, 620);
    m_log->setMinimumWidth(320);
    m_speed->setText(QStringLiteral("x1"));
    auto *controls = new QHBoxLayout;
    controls->addWidget(m_play);
    controls->addWidget(m_speed);
    controls->addWidget(m_timeline, 1);
    controls->addWidget(m_time);
    auto *left = new QVBoxLayout;
    left->addWidget(m_view, 1);
    left->addLayout(controls);
    auto *layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addWidget(m_log);

    connect(m_play, &QPushButton::clicked, this, &ReplayDialog::togglePlay);
    connect(m_speed, &QPushButton::clicked, this, [this] {
        m_rate = m_rate == 1 ? 2 : m_rate == 2 ? 4 : 1;
        m_speed->setText(QStringLiteral("x%1").arg(m_rate));
    });
    connect(m_timeline, &QSlider::valueChanged, this, &ReplayDialog::showFrame);
    connect(m_timer, &QTimer::timeout, this, [this] {
        const int next = m_timeline->value() + m_rate;
        if (next >= m_frames.size()) {
            m_timeline->setValue(int(m_frames.size()) - 1);
            togglePlay();
            return;
        }
        m_timeline->setValue(next);
    });
}

void ReplayDialog::setReplay(const QList<ReplayFrame> &frames, const QString &faction)
{
    m_frames = frames;
    setWindowTitle(tr("Battle replay - %1").arg(faction));
    m_log->clear();
    m_timeline->setRange(0, std::max(0, int(m_frames.size()) - 1));
    m_timeline->setValue(0);
    m_timer->stop();
    m_play->setText(tr("Play"));
    showFrame(0);
    for (int i = 0; i < m_frames.size(); ++i)
        for (const QString &e : m_frames[i].events)
            m_log->addItem(QStringLiteral("%1 s  %2").arg(m_frames[i].time, 0, 'f', 1).arg(e));
}

void ReplayDialog::showFrame(int index)
{
    if (index < 0 || index >= m_frames.size()) {
        m_view->setFrame(nullptr);
        return;
    }
    const ReplayFrame &f = m_frames[index];
    m_view->setFrame(&f);
    m_time->setText(QStringLiteral("%1 / %2 s").arg(f.time, 0, 'f', 1).arg(m_frames.last().time, 0, 'f', 1));
    // Highlights the log line of this moment
    for (int i = 0; i < m_log->count(); ++i)
        if (m_log->item(i)->text().section(' ', 0, 0).toDouble() <= f.time + 0.05)
            m_log->setCurrentRow(i);
}

void ReplayDialog::togglePlay()
{
    if (m_timer->isActive()) {
        m_timer->stop();
        m_play->setText(tr("Play"));
    } else {
        if (m_timeline->value() >= m_frames.size() - 1)
            m_timeline->setValue(0);
        m_timer->start(100);
        m_play->setText(tr("Pause"));
    }
}
