#include "weaponstatuswidget.h"
#include "src/db/dbtext.h"

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
const QColor kOk(60, 220, 90);
const QColor kCharging(240, 190, 60);
const QColor kCritical(255, 70, 60);

QColor healthColor(float pct)
{
    if (pct >= 70) return kOk;
    if (pct >= 35) return kCharging;
    return kCritical;
}
} // namespace

WeaponStatusWidget::WeaponStatusWidget(QWidget *parent)
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

void WeaponStatusWidget::setProvider(std::function<WeaponStatusData()> provider)
{
    m_provider = std::move(provider);
    if (m_provider)
        m_data = m_provider();
    update();
}

void WeaponStatusWidget::paintEvent(QPaintEvent *)
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
    const WeaponReadiness &w = m_data.readiness;

    // --- Frame (cut corners on the left, like the original screen)
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    QPainterPath frame;
    frame.moveTo(r.left() + 18, r.top());
    frame.lineTo(r.right(), r.top());
    frame.lineTo(r.right(), r.bottom());
    frame.lineTo(r.left() + 18, r.bottom());
    frame.lineTo(r.left(), r.bottom() - 18);
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

    const qreal left = r.left() + 14;
    const qreal right = r.right() - 12;
    qreal y = r.top() + 6;

    // --- Header
    p.setFont(font);
    p.setPen(kText);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignLeft | Qt::AlignVCenter, tr("WEAPONS"));
    p.setPen(w.hostiles > 0 ? kCritical : kOk);
    p.drawText(QRectF(left, y, right - left, line), Qt::AlignRight | Qt::AlignVCenter,
               tr("HOSTILES %1   KILLS %2").arg(w.hostiles).arg(w.kills));
    y += line + 6;

    // --- Cooldowns
    const qreal labelWidth = (right - left) * 0.30;
    const qreal stateWidth = (right - left) * 0.22;
    const qreal barLeft = left + labelWidth;
    const qreal barWidth = right - left - labelWidth - stateWidth - 8;
    auto cooldown = [&](const QString &label, const QString &command, float remaining, float max,
                        const QString &readyText) {
        const bool ready = remaining <= 0.0f;
        p.setFont(font);
        p.setPen(kText);
        p.drawText(QRectF(left, y, labelWidth, line), Qt::AlignLeft | Qt::AlignVCenter, label);

        const QRectF bar(barLeft, y + line / 2 - 4, barWidth, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 60));
        p.drawRect(bar);
        const float charge = std::clamp(1.0f - remaining / std::max(0.01f, max), 0.0f, 1.0f);
        p.setBrush(ready ? kOk : kCharging);
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * charge, bar.height()));

        p.setPen(ready ? kOk : kCharging);
        p.drawText(QRectF(bar.right() + 8, y, stateWidth, line), Qt::AlignLeft | Qt::AlignVCenter,
                   ready ? readyText : QStringLiteral("%1 s").arg(remaining, 0, 'f', 1));
        y += line;
        p.setFont(small);
        p.setPen(kDim);
        p.drawText(QRectF(left + 8, y - 2, labelWidth + barWidth, smallLine), Qt::AlignLeft | Qt::AlignVCenter, command);
        y += smallLine + 2;
    };
    cooldown(tr("MAIN TOWER"), tr("attack <n> / maintower"), w.mainCooldown, w.mainMax, tr("READY"));
    cooldown(tr("PULSE"), tr("pulseweapon"), w.pulseCooldown, w.pulseMax, tr("READY"));
    cooldown(tr("TOWERS"),
             w.turretsOn ? tr("auto-fire %1% - burst: secondarytower").arg(qRound(w.efficiency))
                         : tr("auto-fire OFF ('turrets on') - burst: secondarytower"),
             w.burstCooldown, w.burstMax, w.turretsOn ? tr("AUTO") : tr("OFF"));

    // Railguns: point defense, always on
    p.setFont(font);
    p.setPen(kText);
    p.drawText(QRectF(left, y, labelWidth, line), Qt::AlignLeft | Qt::AlignVCenter, tr("RAILGUNS"));
    {
        const QRectF bar(barLeft, y + line / 2 - 4, barWidth, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 60));
        p.drawRect(bar);
        p.setBrush(healthColor(w.efficiency));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(w.efficiency, 0.0f, 100.0f) / 100, bar.height()));
        const bool alarm = w.incomingMissiles > 0;
        p.setPen(!w.railgunsOn ? kCritical : alarm && (m_frame / 3) % 2 == 0 ? kCritical : kOk);
        p.drawText(QRectF(bar.right() + 8, y, stateWidth, line), Qt::AlignLeft | Qt::AlignVCenter,
                   !w.railgunsOn ? tr("OFF") : alarm ? tr("%1 MSL").arg(w.incomingMissiles) : tr("PD ON"));
    }
    y += line;
    p.setFont(small);
    p.setPen(kDim);
    p.drawText(QRectF(left + 8, y - 2, right - left - 8, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
               tr("point defense %1 km - %2 missiles shot down - vs fighters x2.5")
                   .arg(qRound(w.railgunReach * 10)).arg(w.interceptions));
    y += smallLine + 4;

    // Energy: power left for the shields / weapon recharge
    p.setFont(small);
    p.setPen(w.freePower > 10 ? kOk : kDim);
    p.drawText(QRectF(left, y, right - left, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
               tr("POWER free %1 - priority %2 ('power shields|weapons|balanced')")
                   .arg(w.freePower)
                   .arg(w.priority == "shields" ? tr("shields") : w.priority == "weapons" ? tr("weapons") : tr("balanced")));
    y += smallLine + 4;

    // Hangar: fighters to launch ('launch'), friendly ships in the battle
    if (w.dronesMax > 0) {
        p.setFont(font);
        p.setPen(kText);
        p.drawText(QRectF(left, y, labelWidth, line), Qt::AlignLeft | Qt::AlignVCenter, tr("DRONES"));
        const QRectF bar(barLeft, y + line / 2 - 4, barWidth, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 60));
        p.drawRect(bar);
        p.setBrush(QColor(255, 210, 90));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * w.drones / std::max(1, w.dronesMax), bar.height()));
        p.setPen(w.drones > 0 ? kOk : kCharging);
        p.drawText(QRectF(bar.right() + 8, y, stateWidth, line), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1/%2").arg(w.drones).arg(w.dronesMax));
        y += line;
        p.setFont(small);
        p.setPen(kDim);
        p.drawText(QRectF(left + 8, y - 2, right - left - 8, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("drones <n> - salvo through the shields, rebuilt out of battle"));
        y += smallLine + 4;
    }
    if (w.hangarMax > 0 || w.allies > 0) {
        p.setFont(font);
        p.setPen(kText);
        p.drawText(QRectF(left, y, labelWidth, line), Qt::AlignLeft | Qt::AlignVCenter, tr("HANGAR"));
        const QRectF bar(barLeft, y + line / 2 - 4, barWidth, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 60));
        p.drawRect(bar);
        p.setBrush(QColor(90, 170, 255));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * w.hangar / std::max(1, w.hangarMax), bar.height()));
        p.setPen(w.hangar > 0 ? kOk : kCharging);
        p.drawText(QRectF(bar.right() + 8, y, stateWidth, line), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1/%2").arg(w.hangar).arg(w.hangarMax));
        y += line;
        p.setFont(small);
        p.setPen(kDim);
        p.drawText(QRectF(left + 8, y - 2, right - left - 8, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("launch [n] - %1 fighter(s) out, %2 friendly ship(s) in battle").arg(w.fightersOut).arg(w.allies));
        y += smallLine + 4;
    }

    // --- Weapon components
    p.setFont(small);
    for (const WeaponStatusData::Weapon &weapon : m_data.weapons) {
        const float pct = 100.0f * weapon.hp / std::max(1, weapon.maxHp);
        const bool blink = weapon.underAttack && (m_frame / 3) % 2 == 0;
        p.setPen(weapon.underAttack ? kCritical : kText);
        p.drawText(QRectF(left, y, labelWidth, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(small).elidedText(blink ? tr("HIT ") + DbText::tr(weapon.name) : DbText::tr(weapon.name), Qt::ElideRight, labelWidth));
        const QRectF bar(barLeft, y + smallLine / 2 - 3, barWidth, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 60));
        p.drawRect(bar);
        p.setBrush(weapon.hp <= 0 ? kCritical : healthColor(pct));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(pct, 0.0f, 100.0f) / 100, bar.height()));
        p.setPen(weapon.hp <= 0 ? kCritical : kDim);
        p.drawText(QRectF(bar.right() + 8, y, stateWidth, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
                   weapon.hp <= 0 ? tr("OFF") : QStringLiteral("%1%").arg(qRound(pct)));
        y += smallLine + 2;
    }

    // --- Locked target (bottom)
    const qreal boxHeight = line + smallLine + 10;
    const QRectF box(left, std::max(y + 4, r.bottom() - 8 - boxHeight), right - left, boxHeight);
    if (box.bottom() > r.bottom() - 2)
        return;
    p.setPen(QPen(w.hasTarget ? kCritical : QColor(90, 90, 90), 1));
    p.setBrush(QColor(0, 0, 0, 80));
    p.drawRect(box);

    p.setFont(font);
    if (!w.hasTarget) {
        p.setPen(kDim);
        p.drawText(box.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   w.hostiles > 0 ? tr("NO TARGET LOCKED - scan <n>") : tr("NO CONTACT"));
        return;
    }
    // Reticle
    const QPointF c(box.left() + 18, box.center().y());
    p.setPen(QPen(kCritical, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, 8, 8);
    p.drawLine(c + QPointF(-12, 0), c + QPointF(-5, 0));
    p.drawLine(c + QPointF(5, 0), c + QPointF(12, 0));
    p.drawLine(c + QPointF(0, -12), c + QPointF(0, -5));
    p.drawLine(c + QPointF(0, 5), c + QPointF(0, 12));

    const qreal textLeft = box.left() + 38;
    p.setPen(kCritical);
    const QRectF nameRect(textLeft, box.top() + 4, box.right() - textLeft - 8, line);
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(p.font()).elidedText(tr("TARGET T%1  %2").arg(w.targetIndex + 1).arg(w.targetClass.toUpper()),
                                                  Qt::ElideRight, nameRect.width()));
    const QRectF hp(textLeft, box.top() + 6 + line, box.right() - textLeft - 60, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 60, 60));
    p.drawRect(hp);
    p.setBrush(kOk);
    p.drawRect(QRectF(hp.left(), hp.top(), hp.width() * std::clamp(w.targetHealth, 0.0f, 100.0f) / 100, hp.height()));
    p.setFont(small);
    p.setPen(kDim);
    p.drawText(QRectF(hp.right() + 6, hp.center().y() - smallLine / 2, 50, smallLine), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1%").arg(qRound(w.targetHealth)));
}
