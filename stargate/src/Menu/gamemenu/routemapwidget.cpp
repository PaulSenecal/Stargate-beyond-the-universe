#include "routemapwidget.h"

#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

namespace {
const QColor kFrame(205, 205, 205, 200);
const QColor kText(220, 220, 215);
const QColor kDim(130, 130, 128);
const QColor kGrid(255, 255, 255, 14);
const QColor kAvailable(60, 220, 90);
const QColor kUnavailable(200, 40, 60);
const QColor kEnemy(255, 74, 61);
const QColor kTeam(240, 200, 60);

void drawTriangle(QPainter &p, const QPointF &center, qreal size)
{
    const QPolygonF tri{center + QPointF(-size, 0), center + QPointF(size * 0.7, -size * 0.75),
                        center + QPointF(size * 0.7, size * 0.75)};
    p.drawPolygon(tri);
}
} // namespace

RouteMapWidget::RouteMapWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RouteMapWidget::setData(const RouteMapData &data)
{
    m_data = data;
    update();
}

void RouteMapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(std::clamp(height() / 22, 9, 13));
    font.setBold(true);
    p.setFont(font);
    const qreal line = QFontMetricsF(font).height();

    // --- Frame with a cut corner and a faint grid, like the HUD screens
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

    // --- Header
    p.setPen(kText);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignLeft | Qt::AlignVCenter,
               tr("ROUTE  %1 %2").arg(m_data.galaxyCode, m_data.galaxyName.toUpper()));
    p.setPen(m_data.gateOpen ? kAvailable : kUnavailable);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignRight | Qt::AlignVCenter,
               m_data.gateOpen ? tr("GATE %1 h").arg(m_data.gateRemaining, 0, 'f', 1) : tr("GATE CLOSED"));
    y += line + 2;

    // --- Legend (same as the original maps.png)
    QFont small = font;
    small.setBold(false);
    small.setPixelSize(std::max(8, font.pixelSize() - 2));
    p.setFont(small);
    const qreal smallLine = QFontMetricsF(small).height();
    qreal x = left;
    auto legend = [&](const QColor &color, bool triangle, const QString &text) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const QPointF c(x + 5, y + smallLine / 2);
        if (triangle)
            drawTriangle(p, c, 4.5);
        else
            p.drawEllipse(c, 3.5, 3.5);
        p.setPen(kDim);
        const qreal w = QFontMetricsF(small).horizontalAdvance(text);
        p.drawText(QRectF(x + 13, y, w + 2, smallLine), Qt::AlignVCenter, text);
        x += 13 + w + 14;
    };
    legend(kEnemy, true, tr("Enemy"));
    legend(kAvailable, false, tr("Gate available"));
    legend(kUnavailable, false, tr("Gate unavailable"));
    y += smallLine + 8;

    // --- Route strip: a window of stops around the current one
    const QList<int> &stops = m_data.stops;
    const int current = int(stops.indexOf(m_data.currentSequence));
    const int window = std::min(int(stops.size()), 13);
    const int first = std::clamp(current - window / 2, 0, std::max(0, int(stops.size()) - window));
    const qreal stripY = y + 8;
    if (window > 0) {
        const qreal step = window > 1 ? (right - left - 20) / (window - 1) : 0;
        p.setPen(QPen(kDim, 1));
        p.drawLine(QPointF(left + 10, stripY), QPointF(left + 10 + step * (window - 1), stripY));
        for (int i = 0; i < window; ++i) {
            const int index = first + i;
            const QPointF c(left + 10 + step * i, stripY);
            if (index == current) {
                p.setPen(QPen(kText, 2));
                p.setBrush(QColor(28, 28, 28));
                p.drawEllipse(c, 6, 6);
                p.setBrush(kText);
                p.drawEllipse(c, 2.5, 2.5);
            } else {
                p.setPen(QPen(kDim, 1.2));
                p.setBrush(index < current ? kDim : QColor(28, 28, 28));
                p.drawEllipse(c, 3.5, 3.5);
            }
        }
        if (current >= 0) {
            p.setPen(kText);
            p.drawText(QRectF(left, stripY + 8, right - left, smallLine), Qt::AlignLeft,
                       tr("STOP %1/%2 - SYSTEM %3").arg(current + 1).arg(stops.size())
                           .arg(m_data.currentSequence, 4, 10, QChar('0')));
        }
    }
    y = stripY + 8 + smallLine + 6;

    // --- Current system
    const QRectF system(left, y, right - left, r.bottom() - 8 - y);
    if (system.height() < 30)
        return;
    const QPointF star(system.left() + std::min<qreal>(40, system.width() * 0.12), system.center().y());
    QRadialGradient glow(star, 22);
    glow.setColorAt(0, QColor(255, 240, 200, 230));
    glow.setColorAt(0.35, QColor(255, 200, 120, 120));
    glow.setColorAt(1, QColor(255, 160, 60, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(star, 22, 22);

    const int count = int(m_data.planets.size());
    const qreal maxOrbit = std::min(system.width() * 0.38, system.height() * 1.1);
    for (int i = 0; i < count; ++i) {
        const RouteMapData::PlanetMark &pl = m_data.planets.at(i);
        const qreal orbit = maxOrbit * (0.45 + 0.55 * (count > 1 ? qreal(i) / (count - 1) : 0.6));
        const qreal angle = qDegreesToRadians(count > 1 ? -28.0 + 56.0 * i / (count - 1) : 0.0);

        p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(star, orbit, orbit);

        const QPointF c(star.x() + orbit * std::cos(angle), star.y() + orbit * std::sin(angle));
        const bool available = m_data.gateOpen && !pl.explored;
        p.setPen(Qt::NoPen);
        p.setBrush(pl.tint);
        p.drawEllipse(c, 7, 7);
        p.setPen(QPen(available ? kAvailable : kUnavailable, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 10, 10);
        if (pl.team) {
            p.setPen(QPen(kTeam, 1.5));
            p.drawRect(QRectF(c.x() - 14, c.y() - 14, 28, 28));
        }

        // Label
        QString label = QStringLiteral("%1 %2 \"%3\"").arg(pl.letter, pl.address, pl.nickname);
        if (pl.team)
            label += tr(" TEAM");
        if (pl.explored)
            label += tr(" (explored)");
        const qreal labelLeft = c.x() + 16;
        p.setFont(small);
        p.setPen(pl.team ? kTeam : kText);
        p.drawText(QRectF(labelLeft, c.y() - smallLine, right - labelLeft, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(small).elidedText(label, Qt::ElideRight, right - labelLeft));
        p.setPen(kDim);
        p.drawText(QRectF(labelLeft, c.y(), right - labelLeft, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   pl.classification);
    }

    // --- Hostiles in the sector
    if (m_data.hostiles > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(kEnemy);
        const int shown = std::min(m_data.hostiles, 10);
        for (int i = 0; i < shown; ++i)
            drawTriangle(p, QPointF(right - 8 - i * 13, system.top() + 6), 5);
        p.setPen(kEnemy);
        p.drawText(QRectF(left, system.top() + 12, right - left, smallLine), Qt::AlignRight,
                   tr("%1 HOSTILE(S)").arg(m_data.hostiles));
    }
}
