#include "shipstatuswidget.h"
#include "src/db/dbtext.h"

#include <QFontDatabase>
#include <QHash>
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
const QColor kOk(60, 220, 90);
const QColor kDamaged(240, 190, 60);
const QColor kCritical(255, 70, 60);
const QColor kShield(90, 170, 255);
const QColor kFire(255, 130, 30);

// Ship-local bounds of the schematic (x forward, z sideways)
constexpr qreal kMinX = -4.6, kMaxX = 8.2, kHalfZ = 5.9;

// Where each component sits aboard (matches the 3D model)
const QHash<QString, QPointF> kComponentPos = {
    {"Life Support",       {-1.2,  3.0}},
    {"Shield Generator",   { 0.4,  0.0}},
    {"FTL Drive",          {-2.6,  1.2}},
    {"Sublight Engine",    {-3.9,  0.0}},
    {"Stargate Interface", { 0.8,  0.6}},
    {"Power Core",         {-1.6,  0.0}},
    {"Railgun",            { 3.0,  0.35}},
    {"Energy Cannon",      { 5.6,  0.0}},
    {"Missile Bay",        {-2.2, -3.6}},
    {"Medical Bay",        {-0.5,  1.8}},
    {"Science Lab",        {-0.5, -1.8}},
    {"Cargo Hold",         {-3.0, -2.5}},
};

QColor healthColor(float pct)
{
    if (pct >= 70) return kOk;
    if (pct >= 35) return kDamaged;
    return kCritical;
}
} // namespace

ShipStatusWidget::ShipStatusWidget(QWidget *parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !m_provider)
            return;
        m_data = m_provider();
        ++m_frame;
        update();
    });
    m_timer->start(100);
}

void ShipStatusWidget::setProvider(std::function<ShipStatusData()> provider)
{
    m_provider = std::move(provider);
    if (m_provider)
        m_data = m_provider();
    update();
}

void ShipStatusWidget::paintEvent(QPaintEvent *)
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
    const qreal pulse = 0.5 + 0.5 * std::sin(m_frame * 0.9);

    // --- Frame (cut corner at the top right, like the HUD screens)
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    QPainterPath frame;
    frame.moveTo(r.left(), r.top());
    frame.lineTo(r.right() - 18, r.top());
    frame.lineTo(r.right(), r.top() + 18);
    frame.lineTo(r.right(), r.bottom());
    frame.lineTo(r.left() + 18, r.bottom());
    frame.lineTo(r.left(), r.bottom() - 18);
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

    // --- Header: hull and shield
    p.setFont(font);
    p.setPen(kText);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignLeft | Qt::AlignVCenter, tr("DESTINY STATUS"));
    const QString shield = m_data.shieldUp ? QStringLiteral("%1%").arg(qRound(m_data.shield)) : tr("OFF");
    const QString status = tr("HULL %1%   SHIELD %2").arg(qRound(m_data.hull)).arg(shield);
    p.setPen(healthColor(m_data.hull));
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignRight | Qt::AlignVCenter, status);
    y += line + 2;

    // --- Legend
    p.setFont(small);
    qreal x = left;
    auto legend = [&](const QColor &color, bool ring, const QString &text) {
        const QPointF c(x + 5, y + smallLine / 2);
        if (ring) {
            p.setPen(QPen(color, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 4, 4);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(c, 3.5, 3.5);
        }
        p.setPen(kDim);
        const qreal w = QFontMetricsF(small).horizontalAdvance(text);
        p.drawText(QRectF(x + 13, y, w + 2, smallLine), Qt::AlignVCenter, text);
        x += 13 + w + 12;
    };
    legend(kOk, false, tr("OK"));
    legend(kDamaged, false, tr("Damaged"));
    legend(kCritical, false, tr("Critical"));
    legend(kCritical, true, tr("Under attack"));
    y += smallLine + 4;

    // --- Layout: schematic on top, component list below
    const int rows = (int(m_data.components.size()) + 1) / 2;
    const qreal listHeight = rows * (smallLine + 2) + 4;
    const QRectF schematic(left, y, right - left, r.bottom() - 10 - listHeight - y);
    if (schematic.height() > 30) {
        // Bounds: the Destiny drawing, or the silhouette of the ship and its shield
        const bool own = !m_data.silhouette.isNull();
        QRectF bounds(kMinX, -kHalfZ, kMaxX - kMinX, 2 * kHalfZ);
        if (own)
            bounds = m_data.silhouetteRect.united(QRectF(m_data.shieldCenter.x() - m_data.shieldRadii.width(),
                                                         m_data.shieldCenter.y() - m_data.shieldRadii.height(),
                                                         2 * m_data.shieldRadii.width(), 2 * m_data.shieldRadii.height()));
        const qreal halfZ = std::max(-bounds.top(), bounds.bottom());
        const qreal scale = std::min(schematic.width() / bounds.width(), schematic.height() / (2 * halfZ));
        const QPointF origin(schematic.center().x() - bounds.center().x() * scale, schematic.center().y());
        auto toScreen = [&](const QPointF &local) {   // x forward -> right, z -> down
            return QPointF(origin.x() + local.x() * scale, origin.y() + local.y() * scale);
        };

        // Shield bubble (ellipse from the 3D model)
        const QPointF shieldCenter = toScreen(own ? m_data.shieldCenter : QPointF(1.95, 0.0));
        if (m_data.shieldUp && m_data.shield > 0) {
            p.setPen(QPen(QColor(kShield.red(), kShield.green(), kShield.blue(), 60 + int(m_data.shield)), 1.2, Qt::DashLine));
            p.setBrush(QColor(kShield.red(), kShield.green(), kShield.blue(), 18));
            p.drawEllipse(shieldCenter, (own ? m_data.shieldRadii.width() : 6.9) * scale,
                          (own ? m_data.shieldRadii.height() : 5.8) * scale);
        }

        // Hull: silhouette of the 3D model, tinted like a blueprint
        if (own) {
            QImage tinted(m_data.silhouette.size(), QImage::Format_ARGB32_Premultiplied);
            tinted.fill(QColor(150, 160, 165));
            QPainter mask(&tinted);
            mask.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            mask.drawImage(0, 0, m_data.silhouette);
            mask.end();
            const QRectF target(toScreen(m_data.silhouetteRect.topLeft()), toScreen(m_data.silhouetteRect.bottomRight()));
            p.setOpacity(0.75);
            p.drawImage(target, tinted);
            p.setOpacity(1.0);
        }

        // Destiny drawing: spire + rear fan
        QPainterPath hull;
        hull.moveTo(toScreen({0.0, -0.9}));
        hull.lineTo(toScreen({8.0, 0.0}));
        hull.lineTo(toScreen({0.0, 0.9}));
        const QPointF fanCenter(1.5, 0.0);
        for (int i = 0; i <= 20; ++i) {
            const qreal a = qDegreesToRadians(130.0 + 100.0 * i / 20);
            hull.lineTo(toScreen({fanCenter.x() + 5.6 * std::cos(a), -5.6 * std::sin(a)}));
        }
        hull.closeSubpath();
        if (!own) {
            p.setPen(QPen(QColor(200, 200, 200, 170), 1.3));
            p.setBrush(QColor(60, 60, 58));
            p.drawPath(hull);
        }

        // Fires
        for (const QPointF &f : m_data.fires) {
            const qreal flicker = 2.5 + 1.5 * std::sin(m_frame * 1.7 + f.x() * 3);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kFire.red(), kFire.green(), kFire.blue(), 200));
            p.drawEllipse(toScreen(f), flicker, flicker);
        }

        // Components
        for (const ShipStatusData::Component &c : m_data.components) {
            QPointF local = kComponentPos.value(c.name, QPointF(0, 0));
            if (own)                                // same place relative to the hull
                local = QPointF(m_data.silhouetteRect.left() + (local.x() - kMinX) / (kMaxX - kMinX) * m_data.silhouetteRect.width(),
                                local.y() / kHalfZ * m_data.silhouetteRect.height() / 2);
            const QPointF at = toScreen(local);
            const float pct = 100.0f * c.hp / std::max(1, c.maxHp);
            if (c.underAttack) {
                p.setPen(QPen(kCritical, 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(at, 7 + 4 * pulse, 7 + 4 * pulse);
            }
            p.setPen(QPen(QColor(20, 20, 20), 1));
            p.setBrush(c.hp <= 0 ? QColor(90, 20, 20) : healthColor(pct));
            p.drawEllipse(at, 4.5, 4.5);
            if (c.hp <= 0) {
                p.setPen(QPen(kCritical, 1.5));
                p.drawLine(at + QPointF(-4, -4), at + QPointF(4, 4));
                p.drawLine(at + QPointF(-4, 4), at + QPointF(4, -4));
            }
        }

        // Recent impacts: expanding rings
        for (const ShipStatusData::Hit &h : m_data.hits) {
            const qreal t = std::clamp(qreal(h.age) / 1.5, 0.0, 1.0);
            const QColor c = h.shield ? kShield : kCritical;
            p.setPen(QPen(QColor(c.red(), c.green(), c.blue(), int(255 * (1 - t))), 1.8));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(toScreen(h.local), 3 + 12 * t, 3 + 12 * t);
        }

        // Orientation tag
        p.setFont(small);
        p.setPen(kDim);
        p.drawText(QRectF(schematic.right() - 60, schematic.top(), 60, smallLine), Qt::AlignRight, tr("BOW >"));
    }

    // --- Component list (two columns)
    p.setFont(small);
    const qreal listTop = r.bottom() - 8 - listHeight;
    const qreal columnWidth = (right - left - 12) / 2;
    for (int i = 0; i < m_data.components.size(); ++i) {
        const ShipStatusData::Component &c = m_data.components.at(i);
        const float pct = 100.0f * c.hp / std::max(1, c.maxHp);
        const qreal cx = left + (i / rows) * (columnWidth + 12);
        const qreal cy = listTop + (i % rows) * (smallLine + 2);
        const qreal barWidth = std::min<qreal>(54, columnWidth * 0.3);
        const qreal nameWidth = columnWidth - barWidth - 40;

        p.setPen(c.underAttack ? kCritical : kText);
        const QString name = c.underAttack ? tr("HIT ") + DbText::tr(c.name) : DbText::tr(c.name);
        p.drawText(QRectF(cx, cy, nameWidth, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(small).elidedText(name, Qt::ElideRight, nameWidth));

        const QRectF bar(cx + nameWidth + 4, cy + smallLine / 2 - 3, barWidth, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 60));
        p.drawRect(bar);
        p.setBrush(c.hp <= 0 ? kCritical : healthColor(pct));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(pct, 0.0f, 100.0f) / 100, bar.height()));

        p.setPen(c.hp <= 0 ? kCritical : kDim);
        p.drawText(QRectF(bar.right() + 4, cy, 36, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   c.hp <= 0 ? tr("OFF") : QStringLiteral("%1%").arg(qRound(pct)));
    }
}
