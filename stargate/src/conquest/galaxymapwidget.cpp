#include "galaxymapwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QHash>
#include <QPainterPath>
#include <QPolygonF>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

#include "conquestgame.h"

using namespace Conquest;

namespace {

const QColor kBackground(8, 9, 14);
const QColor kFrame(92, 86, 63);
const QColor kGold(226, 191, 62);
const QColor kText(232, 226, 200);
const QColor kNeutral(130, 130, 125);
const QColor kLane(70, 76, 96);
constexpr double kMargin = 14;
constexpr double kLabelRoom = 90;     // names are written right of the planets

QColor ownerColor(const ConquestGame *game, int owner)
{
    return owner < 0 ? kNeutral : QColor(game->factions().at(owner).color);
}

} // namespace

GalaxyMapWidget::GalaxyMapWidget(const ConquestGame *game, QWidget *parent)
    : QWidget(parent)
    , m_game(game)
{
    setMinimumSize(360, 240);
    setMouseTracking(false);
}

void GalaxyMapWidget::select(int planetId)
{
    m_selected = planetId;
    update();
}

// The map fills the widget (schematic: x and y scales may differ)
QPointF GalaxyMapWidget::toScreen(const QPointF &map) const
{
    const double sx = std::max(0.1, (width() - 2 * kMargin - kLabelRoom) / kMapWidth);
    const double sy = std::max(0.1, (height() - 2 * kMargin) / kMapHeight);
    return QPointF(kMargin + map.x() * sx, kMargin + map.y() * sy);
}

int GalaxyMapWidget::planetAt(const QPointF &pos) const
{
    int best = -1;
    double bestDistance = 18;
    for (const Planet &p : m_game->planets()) {
        const double d = QLineF(toScreen(p.pos), pos).length();
        if (d < bestDistance) {
            bestDistance = d;
            best = p.id;
        }
    }
    return best;
}

void GalaxyMapWidget::mousePressEvent(QMouseEvent *event)
{
    const int id = planetAt(event->position());
    if (id >= 0) {
        select(id);
        emit planetSelected(id);
    }
}

void GalaxyMapWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int id = planetAt(event->position());
    if (id >= 0)
        emit planetActivated(id);
}

void GalaxyMapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), kBackground);

    // Faint stars (same every frame)
    QRandomGenerator stars(17);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 160; ++i) {
        p.setBrush(QColor(255, 255, 255, 25 + int(stars.bounded(60))));
        p.drawEllipse(QPointF(stars.bounded(width() + 1), stars.bounded(height() + 1)), 0.8, 0.8);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kFrame, 1.5));
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);

    if (!m_game || m_game->planets().isEmpty())
        return;
    const QList<Planet> &planets = m_game->planets();

    // Lanes (brighter between two worlds of the same owner)
    for (const Planet &a : planets)
        for (int n : a.lanes) {
            if (n < a.id)
                continue;
            const Planet &b = planets[n];
            const bool same = a.owner >= 0 && a.owner == b.owner;
            QColor c = same ? ownerColor(m_game, a.owner) : kLane;
            c.setAlpha(same ? 120 : 150);
            p.setPen(QPen(c, same ? 2.0 : 1.2));
            p.drawLine(toScreen(a.pos), toScreen(b.pos));
        }

    // Route of the flagship
    if (m_game->travelling()) {
        QPainterPath route(toScreen(m_game->flagshipPos()));
        for (int id : m_game->route())
            route.lineTo(toScreen(planets[id].pos));
        p.setPen(QPen(kGold, 2.0, Qt::DashLine));
        p.drawPath(route);
    }

    // Planets
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);
    for (const Planet &planet : planets) {
        const QPointF c = toScreen(planet.pos);
        const QColor color = ownerColor(m_game, planet.owner);
        const double r = planet.capital ? 11 : 7 + std::min(3.0, planet.population / 4.0);

        if (planet.id == m_selected) {
            p.setPen(QPen(kText, 1.5, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, r + 7, r + 7);
        }
        QRadialGradient glow(c, r * 2.2);
        QColor halo = color;
        halo.setAlpha(70);
        glow.setColorAt(0, halo);
        glow.setColorAt(1, Qt::transparent);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, r * 2.2, r * 2.2);
        p.setBrush(color.darker(planet.owner < 0 ? 150 : 115));
        p.setPen(QPen(color.lighter(130), planet.capital ? 2.5 : 1.5));
        p.drawEllipse(c, r, r);

        // Number inside, defense pips under, name on the right
        p.setPen(planet.owner < 0 ? kText : QColor(Qt::black));
        p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter, QString::number(planet.id + 1));
        for (int d = 0; d < planet.defense; ++d) {
            p.setPen(Qt::NoPen);
            p.setBrush(color.lighter(140));
            p.drawRect(QRectF(c.x() - planet.defense * 3.0 + d * 6.0, c.y() + r + 3, 4, 3));
        }
        p.setPen(planet.capital ? kGold : kText);
        p.drawText(QPointF(c.x() + r + 5, c.y() + 4), planet.capital ? QStringLiteral("♛ ") + planet.name : planet.name);
    }

    // Fleets: diamonds in the owner color (yours: gold outline and number).
    // Red dashes: enemy fleets heading for one of your worlds.
    QFont small = font;
    small.setPixelSize(9);
    p.setFont(small);
    QHash<int, int> stacked;                    // fleets already drawn around a planet
    for (const Fleet &fleet : m_game->fleets()) {
        const bool mine = fleet.owner == m_game->playerIndex();
        const QColor color = ownerColor(m_game, fleet.owner);
        QPointF at = toScreen(m_game->fleetPos(fleet));
        if (!fleet.moving()) {
            const int index = stacked[fleet.at]++;
            const double angle = (-35.0 + 55.0 * index) * M_PI / 180.0;
            at += QPointF(std::cos(angle), std::sin(angle)) * 20.0;
        }
        if (fleet.target >= 0) {
            const bool threat = !mine && planets[fleet.target].owner == m_game->playerIndex();
            if (mine || threat) {
                QPainterPath route(at);
                for (int id : fleet.route)
                    route.lineTo(toScreen(planets[id].pos));
                p.setPen(QPen(threat ? QColor(255, 90, 72) : color, threat ? 1.6 : 1.2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawPath(route);
            }
        }
        QPolygonF diamond;
        diamond << at + QPointF(0, -6) << at + QPointF(6, 0) << at + QPointF(0, 6) << at + QPointF(-6, 0);
        p.setPen(QPen(mine ? kGold : color.darker(160), mine ? 1.6 : 1.0));
        p.setBrush(color);
        p.drawPolygon(diamond);
        const QString label = mine ? QStringLiteral("F%1 %2+%3").arg(fleet.id).arg(fleet.motherships).arg(fleet.fighters)
                                   : QStringLiteral("%1+%2").arg(fleet.motherships).arg(fleet.fighters);
        p.setPen(mine ? kGold : color.lighter(130));
        p.drawText(at + QPointF(8, 3), label);
    }
    p.setFont(font);

    // Flagship: gold arrow pointing along its route
    const QPointF ship = toScreen(m_game->flagshipPos());
    double angle = -90;
    if (m_game->travelling()) {
        const QPointF to = toScreen(planets[m_game->route().first()].pos);
        angle = std::atan2(to.y() - ship.y(), to.x() - ship.x()) * 180.0 / M_PI;
    }
    p.save();
    p.translate(ship + QPointF(0, m_game->travelling() ? 0 : -16));
    p.rotate(angle);
    QPainterPath arrow;
    arrow.moveTo(9, 0);
    arrow.lineTo(-6, -6);
    arrow.lineTo(-3, 0);
    arrow.lineTo(-6, 6);
    arrow.closeSubpath();
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(kGold);
    p.drawPath(arrow);
    p.restore();

    // Capture progress around a neutral world
    if (m_game->captureProgress() > 0) {
        const QPointF c = toScreen(planets[m_game->flagshipPlanet()].pos);
        p.setPen(QPen(kGold, 3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(c.x() - 15, c.y() - 15, 30, 30), 90 * 16, -int(360 * 16 * m_game->captureProgress()));
    }
}
