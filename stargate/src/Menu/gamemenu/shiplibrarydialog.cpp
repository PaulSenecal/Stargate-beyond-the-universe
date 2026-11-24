#include "shiplibrarydialog.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPixmap>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>

#include "src/core/assets.h"

namespace {

const QColor kBackground(14, 14, 16);
const QColor kText(225, 225, 220);
const QColor kDim(140, 140, 135);
const QColor kAccent(240, 190, 60);
const QColor kSpecial(255, 120, 70);
const QColor kTrack(60, 60, 60);

QString normalized(const QString &text)
{
    return text.toLower().remove(QRegularExpression("[^a-z0-9]"));
}

QString kindLabel(const QString &kind)
{
    if (kind == "bolt") return QObject::tr("energy bolts");
    if (kind == "slug") return QObject::tr("kinetic slugs");
    if (kind == "missile") return QObject::tr("missile");
    if (kind == "beam") return QObject::tr("beam");
    if (kind == "kamikaze") return QObject::tr("kamikaze");
    if (kind == "launch") return QObject::tr("launch");
    return kind;
}

QString roleLabel(const QString &role)
{
    if (role.compare("MotherShip", Qt::CaseInsensitive) == 0) return QObject::tr("Mothership");
    if (role.compare("Automated", Qt::CaseInsensitive) == 0) return QObject::tr("Automated fighter");
    return role;
}

// Damage of one use (all shots)
QString damageText(const ShipAttack &a)
{
    if (a.kind == "launch")
        return QObject::tr("%1 x %2").arg(a.shots).arg(a.payload);
    const QString one = QString::number(double(a.damage), 'g', 3);
    return a.shots > 1 ? QObject::tr("%1 x %2 dmg").arg(a.shots).arg(one) : QObject::tr("%1 dmg").arg(one);
}

} // namespace

// ================================================================
//  ShipCardWidget: painted card of one ship
// ================================================================

class ShipCardWidget : public QWidget
{
public:
    explicit ShipCardWidget(QWidget *parent) : QWidget(parent) { setMinimumSize(460, 420); }

    void setShip(const ShipClass &ship, int maxHull)
    {
        m_ship = ship;
        m_maxHull = std::max(1, maxHull);
        m_valid = true;
        const QString picture = Assets::shipPicture(ship.picture);
        m_picture = picture.isEmpty() ? QPixmap() : QPixmap(picture);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), kBackground);
        if (!m_valid)
            return;

        QFont title = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        title.setPixelSize(20);
        title.setBold(true);
        QFont font = title;
        font.setPixelSize(13);
        QFont small = font;
        small.setBold(false);
        small.setPixelSize(12);

        const qreal margin = 16;
        const qreal width = this->width() - 2 * margin;
        qreal y = margin;

        // Ship picture (optional mod assets) in the top-right corner;
        // the header text keeps to its left
        qreal headerWidth = width;
        qreal pictureBottom = y;
        if (!m_picture.isNull()) {
            const qreal pw = std::min<qreal>(240, width * 0.42);
            const qreal ph = pw * m_picture.height() / std::max(1, m_picture.width());
            const QRectF target(margin + width - pw, y, pw, ph);
            p.drawPixmap(target, m_picture, m_picture.rect());
            p.setPen(QPen(QColor(70, 70, 70), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(target);
            headerWidth = width - pw - 14;
            pictureBottom = target.bottom();
        }

        // Header: color chip, name, faction / role
        const QColor tint(m_ship.color);
        p.setPen(Qt::NoPen);
        p.setBrush(tint.isValid() ? tint : kDim);
        p.drawRoundedRect(QRectF(margin, y + 4, 18, 18), 3, 3);
        p.setFont(title);
        p.setPen(kText);
        p.drawText(QPointF(margin + 28, y + 20), m_ship.name.toUpper());
        y += 32;
        p.setFont(small);
        p.setPen(kDim);
        p.drawText(QPointF(margin, y + 12), QObject::tr("%1  |  %2  |  %3")
                                               .arg(m_ship.faction, roleLabel(m_ship.role),
                                                    m_ship.hostile ? QObject::tr("HOSTILE") : QObject::tr("not hostile")));
        y += 24;

        // Hull bar (relative to the toughest ship)
        p.setFont(font);
        p.setPen(kText);
        p.drawText(QPointF(margin, y + 12), QObject::tr("HULL %1").arg(m_ship.hull));
        const QRectF track(margin + 110, y + 3, headerWidth - 110, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(kTrack);
        p.drawRoundedRect(track, 3, 3);
        p.setBrush(kAccent);
        p.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * m_ship.hull / m_maxHull, track.height())), 3, 3);
        y += 26;

        // Description
        p.setFont(small);
        p.setPen(kText);
        const QRectF descRect(margin, y, headerWidth, 200);
        const QRectF used = p.boundingRect(descRect, Qt::TextWordWrap, m_ship.description);
        p.drawText(descRect, Qt::TextWordWrap, m_ship.description);
        y = std::max(y + used.height(), pictureBottom) + 16;

        // Attacks
        p.setFont(font);
        p.setPen(kAccent);
        p.drawText(QPointF(margin, y + 12), QObject::tr("ATTACKS"));
        y += 22;
        if (m_ship.attacks.isEmpty()) {
            p.setFont(small);
            p.setPen(kDim);
            p.drawText(QPointF(margin, y + 12), QObject::tr("No known attack"));
        }
        for (const ShipAttack &a : m_ship.attacks) {
            const QColor accent = a.special ? kSpecial : kAccent;
            const qreal top = y;
            // Name + kind
            p.setFont(font);
            p.setPen(a.special ? kSpecial : kText);
            p.drawText(QPointF(margin + 10, y + 16), a.special ? QObject::tr("%1  [SPECIAL]").arg(a.name) : a.name);
            p.setFont(small);
            p.setPen(kDim);
            const QString right = QObject::tr("%1  -  %2").arg(kindLabel(a.kind), damageText(a));
            p.drawText(QRectF(margin, y + 4, width - 10, 16), Qt::AlignRight, right);
            y += 22;

            // Chance bar
            p.setPen(kText);
            p.drawText(QPointF(margin + 10, y + 11), QObject::tr("Chance %1%").arg(qRound(a.chance * 100)));
            const QRectF bar(margin + 120, y + 2, width - 130, 8);
            p.setPen(Qt::NoPen);
            p.setBrush(kTrack);
            p.drawRoundedRect(bar, 3, 3);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(bar.topLeft(), QSizeF(bar.width() * std::clamp(a.chance, 0.0f, 1.0f), bar.height())), 3, 3);
            y += 16;

            // Frequency + trigger
            p.setPen(kDim);
            QString when = QObject::tr("every %1 s - about %2 per minute")
                               .arg(QString::number(double(a.cooldown), 'g', 3))
                               .arg(QString::number(double(a.usesPerMinute()), 'f', 1));
            if (a.trigger == "damaged")
                when += QObject::tr(" - only below 50% hull");
            if (a.interceptable())
                when += QObject::tr(" - railguns can stop it");
            const QRectF whenRect(margin + 10, y, width - 20, 40);
            const QRectF whenUsed = p.boundingRect(whenRect, Qt::TextWordWrap, when);
            p.drawText(whenRect, Qt::TextWordWrap, when);
            y += whenUsed.height() + 2;

            // Description
            if (!a.description.isEmpty()) {
                const QRectF r(margin + 10, y, width - 20, 60);
                const QRectF h = p.boundingRect(r, Qt::TextWordWrap, a.description);
                p.setPen(kText);
                p.drawText(r, Qt::TextWordWrap, a.description);
                y += h.height();
            }
            y += 8;

            p.setPen(QPen(a.special ? kSpecial : QColor(70, 70, 70), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(margin, top, width, y - top), 4, 4);
            y += 8;
        }
    }

private:
    ShipClass m_ship;
    int m_maxHull = 1;
    QPixmap m_picture;
    bool m_valid = false;
};

// ================================================================
//  ShipLibraryDialog
// ================================================================

ShipLibraryDialog::ShipLibraryDialog(QWidget *parent)
    : QDialog(parent)
    , m_list(new QListWidget(this))
    , m_card(new ShipCardWidget(this))
{
    setWindowTitle(tr("Ship library"));
    resize(900, 660);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, kBackground);
    setPalette(pal);
    setAutoFillBackground(true);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(13);
    m_list->setFont(mono);
    m_list->setFixedWidth(230);
    m_list->setStyleSheet(
        "QListWidget { background: #1c1c1c; color: #e1e1dc; border: 1px solid #464646; }"
        "QListWidget::item { padding: 4px 6px; }"
        "QListWidget::item:selected { background: #f0be3c; color: #111; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    layout->addWidget(m_list);
    layout->addWidget(m_card, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (QListWidgetItem *item = m_list->item(row); item && item->data(Qt::UserRole).isValid())
            select(item->data(Qt::UserRole).toInt());
    });
}

void ShipLibraryDialog::setShips(const QList<ShipClass> &ships)
{
    m_ships = ships;
    m_list->clear();

    QString faction;
    for (int i = 0; i < ships.size(); ++i) {
        if (ships[i].faction != faction) {
            faction = ships[i].faction;
            auto *header = new QListWidgetItem(faction.toUpper(), m_list);
            header->setFlags(Qt::NoItemFlags);
            header->setForeground(kAccent);
        }
        auto *item = new QListWidgetItem(QStringLiteral("  %1").arg(ships[i].name), m_list);
        item->setData(Qt::UserRole, i);
    }
    if (!ships.isEmpty())
        showShip(ships.first().name);
}

bool ShipLibraryDialog::showShip(const QString &name)
{
    const int index = find(m_ships, name);
    if (index < 0)
        return false;
    for (int row = 0; row < m_list->count(); ++row)
        if (m_list->item(row)->data(Qt::UserRole) == index)
            m_list->setCurrentRow(row);
    select(index);
    return true;
}

void ShipLibraryDialog::select(int shipIndex)
{
    int maxHull = 1;
    for (const ShipClass &s : m_ships)
        maxHull = std::max(maxHull, s.hull);
    m_card->setShip(m_ships.value(shipIndex), maxHull);
}

int ShipLibraryDialog::find(const QList<ShipClass> &ships, const QString &name)
{
    const QString wanted = normalized(name);
    if (wanted.isEmpty())
        return -1;
    for (int i = 0; i < ships.size(); ++i)
        if (normalized(ships[i].name) == wanted)
            return i;
    for (int i = 0; i < ships.size(); ++i)
        if (normalized(ships[i].name).contains(wanted))
            return i;
    return -1;
}

QString ShipLibraryDialog::describe(const ShipAttack &a)
{
    QString text = QObject::tr("%1%2: %3, %4, %5% every %6 s (~%7/min)")
                       .arg(a.special ? QStringLiteral("* ") : QString(), a.name, kindLabel(a.kind), damageText(a))
                       .arg(qRound(a.chance * 100))
                       .arg(QString::number(double(a.cooldown), 'g', 3))
                       .arg(QString::number(double(a.usesPerMinute()), 'f', 1));
    if (a.trigger == "damaged")
        text += QObject::tr(", below 50% hull");
    if (a.interceptable())
        text += QObject::tr(", railguns can stop it");
    return text;
}

QStringList ShipLibraryDialog::describe(const ShipClass &ship)
{
    QStringList lines;
    lines << QObject::tr("%1 - %2 %3, hull %4%5")
                 .arg(ship.name.toUpper(), ship.faction, roleLabel(ship.role).toLower())
                 .arg(ship.hull)
                 .arg(ship.hostile ? QObject::tr(", hostile") : QString());
    lines << ship.description;
    for (const ShipAttack &a : ship.attacks)
        lines << QStringLiteral("  ") + describe(a);
    return lines;
}
