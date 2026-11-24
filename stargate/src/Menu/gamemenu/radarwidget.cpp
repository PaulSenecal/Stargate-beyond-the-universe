#include "radarwidget.h"

#include <QConicalGradient>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>
#include <algorithm>

namespace {
const QColor kFrame(205, 205, 205, 200);
const QColor kText(220, 220, 215);
const QColor kDim(130, 130, 128);
const QColor kGrid(255, 255, 255, 14);
const QColor kScope(70, 255, 110);
const QColor kEnemy(255, 74, 61);
const QColor kIncoming(255, 170, 40);
const QColor kOutgoing(130, 200, 255);
const QColor kMissile(255, 60, 160);

constexpr qreal kRange = 42.0;          // world units shown to the edge
constexpr qreal kUnitKm = 10.0;         // same scale as the sector report

void drawTriangle(QPainter &p, const QPointF &tip, const QPointF &direction, qreal size)
{
    const QPointF d = direction / std::max(1e-6, std::hypot(direction.x(), direction.y()));
    const QPointF n(-d.y(), d.x());
    const QPolygonF tri{tip, tip - d * size * 1.6 + n * size, tip - d * size * 1.6 - n * size};
    p.drawPolygon(tri);
}
} // namespace

RadarWidget::RadarWidget(QWidget *parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !m_provider)
            return;
        m_picture = m_provider();
        ++m_frame;
        update();
    });
    m_timer->start(50);
}

void RadarWidget::setProvider(std::function<RadarPicture()> provider)
{
    m_provider = std::move(provider);
    if (m_provider)
        m_picture = m_provider();
    update();
}

void RadarWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(std::clamp(height() / 22, 9, 13));
    font.setBold(true);
    QFont small = font;
    small.setBold(false);
    small.setPixelSize(std::max(8, font.pixelSize() - 2));
    const qreal line = QFontMetricsF(font).height();
    const qreal smallLine = QFontMetricsF(small).height();

    // --- Frame (cut corner at the top left)
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    QPainterPath frame;
    frame.moveTo(r.left() + 18, r.top());
    frame.lineTo(r.right(), r.top());
    frame.lineTo(r.right(), r.bottom() - 18);
    frame.lineTo(r.right() - 18, r.bottom());
    frame.lineTo(r.left(), r.bottom());
    frame.lineTo(r.left(), r.top() + 18);
    frame.closeSubpath();
    p.fillPath(frame, QColor(28, 28, 28));
    p.save();
    p.setClipPath(frame);
    p.setPen(QPen(kGrid, 1));
    for (qreal x = r.left(); x < r.right(); x += 22)
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    for (qreal y = r.top(); y < r.bottom(); y += 22)
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    p.restore();
    p.setPen(QPen(kFrame, 1.5));
    p.drawPath(frame);

    const qreal left = r.left() + 12;
    const qreal right = r.right() - 12;
    qreal y = r.top() + 6;

    // --- Header + legend (left column)
    p.setFont(font);
    p.setPen(kText);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignLeft | Qt::AlignVCenter, tr("RADAR"));
    p.setPen(m_picture.contacts.isEmpty() ? kScope : kEnemy);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignRight | Qt::AlignVCenter,
               m_picture.contacts.isEmpty() ? tr("NO CONTACT") : tr("%1 CONTACT(S)").arg(m_picture.contacts.size()));
    y += line + 2;

    p.setFont(small);
    auto legend = [&](const QColor &color, int shape, const QString &text) {
        const QPointF c(left + 5, y + smallLine / 2);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        if (shape == 0)
            drawTriangle(p, c + QPointF(0, -3), QPointF(0, 1), 3.5);
        else if (shape == 2)
            p.drawPolygon(QPolygonF{c + QPointF(0, -4), c + QPointF(4, 0), c + QPointF(0, 4), c + QPointF(-4, 0)});
        else
            p.drawEllipse(c, 2.5, 2.5);
        p.setPen(kDim);
        p.drawText(QRectF(left + 13, y, 140, smallLine), Qt::AlignVCenter, text);
        y += smallLine;
    };
    legend(kEnemy, 0, tr("Hostile"));
    legend(kIncoming, 1, tr("Incoming fire"));
    legend(kOutgoing, 1, tr("Destiny fire"));
    legend(kMissile, 2, tr("Missile"));
    p.setPen(kDim);
    p.drawText(QRectF(left, y + 2, 140, smallLine), Qt::AlignVCenter,
               tr("Ring: %1 km").arg(qRound(kRange / 4 * kUnitKm)));

    // --- Scope
    const qreal top = r.top() + line + 10;
    const qreal radius = std::max<qreal>(20, std::min((r.bottom() - 10 - top) / 2, (right - left) / 2 - 10));
    const QPointF center(std::max(left + 150 + radius, (left + right) / 2), top + (r.bottom() - 10 - top) / 2);
    const qreal scale = radius / kRange;
    auto toScreen = [&](const QPointF &local) {     // bow up, starboard right
        return QPointF(center.x() + local.y() * scale, center.y() - local.x() * scale);
    };

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 30, 16, 200));
    p.drawEllipse(center, radius, radius);

    // Sweep
    const qreal sweepAngle = std::fmod(m_frame * 6.0, 360.0);
    QConicalGradient sweep(center, -sweepAngle);
    sweep.setColorAt(0.0, QColor(kScope.red(), kScope.green(), kScope.blue(), 110));
    sweep.setColorAt(0.12, QColor(kScope.red(), kScope.green(), kScope.blue(), 0));
    sweep.setColorAt(1.0, QColor(kScope.red(), kScope.green(), kScope.blue(), 0));
    p.setBrush(sweep);
    p.drawEllipse(center, radius, radius);

    // Rings and axes
    p.setBrush(Qt::NoBrush);
    for (int i = 1; i <= 4; ++i) {
        p.setPen(QPen(QColor(kScope.red(), kScope.green(), kScope.blue(), i == 4 ? 150 : 60), 1));
        p.drawEllipse(center, radius * i / 4, radius * i / 4);
    }
    p.setPen(QPen(QColor(kScope.red(), kScope.green(), kScope.blue(), 45), 1));
    p.drawLine(center - QPointF(radius, 0), center + QPointF(radius, 0));
    p.drawLine(center - QPointF(0, radius), center + QPointF(0, radius));
    p.setPen(kDim);
    p.drawText(QRectF(center.x() - 20, center.y() - radius - smallLine - 1, 40, smallLine), Qt::AlignCenter, tr("BOW"));

    // Destiny (and its shield)
    if (m_picture.shieldUp) {
        p.setPen(QPen(QColor(90, 170, 255, 120), 1, Qt::DashLine));
        p.drawEllipse(toScreen({1.95, 0}), 5.8 * scale + 2, 6.9 * scale + 2);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(kText);
    drawTriangle(p, toScreen({8.0, 0}), QPointF(0, -1), std::max<qreal>(3.0, 3.2 * scale));

    // Fire
    for (const RadarPicture::Bolt &b : m_picture.bolts) {
        const QPointF at = toScreen(b.local);
        if (QLineF(at, center).length() > radius)
            continue;
        if (b.missile) {
            p.setBrush(kMissile);
            p.drawPolygon(QPolygonF{at + QPointF(0, -4), at + QPointF(4, 0), at + QPointF(0, 4), at + QPointF(-4, 0)});
            continue;
        }
        p.setBrush(b.fromDestiny ? kOutgoing : kIncoming);
        p.drawEllipse(at, 1.8, 1.8);
    }

    // Contacts (clamped to the edge when out of range)
    p.setFont(small);
    for (const RadarPicture::Contact &c : m_picture.contacts) {
        QPointF at = toScreen(c.local);
        QPointF offset = at - center;
        const qreal distance = std::hypot(offset.x(), offset.y());
        const bool outOfRange = distance > radius - 4;
        if (outOfRange) {
            offset *= (radius - 4) / std::max(1e-6, distance);
            at = center + offset;
        }
        const qreal size = c.mothership ? 5.5 : 3.5;
        p.setPen(Qt::NoPen);
        p.setBrush(outOfRange ? QColor(kEnemy.red(), kEnemy.green(), kEnemy.blue(), 140) : kEnemy);
        drawTriangle(p, at, -offset, size);   // points toward the Destiny
        if (c.locked) {
            const qreal pulse = 9 + 2 * std::sin(m_frame * 0.4);
            p.setPen(QPen(kEnemy, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, pulse, pulse);
        }
        p.setPen(kEnemy);
        p.drawText(QRectF(at.x() + 7, at.y() - smallLine, 60, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   c.locked ? tr("T%1 LOCK").arg(c.index + 1) : tr("T%1").arg(c.index + 1));
    }
}
