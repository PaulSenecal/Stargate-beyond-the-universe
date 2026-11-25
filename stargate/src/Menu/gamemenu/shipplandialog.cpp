#include "shipplandialog.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

#include "src/Menu/uipalette.h"
#include "src/core/interior.h"

namespace {

QString shown(const QString &name)
{
    return QCoreApplication::translate("Interior", name.toUtf8().constData());
}

QString initials(const QString &name)
{
    QString out;
    for (const QString &part : name.split(' ', Qt::SkipEmptyParts))
        out += part.left(1);
    return out.left(2);
}

} // namespace

// ================================================================
//  ShipPlanView
// ================================================================

ShipPlanView::ShipPlanView(const Interior *interior, QWidget *parent)
    : QWidget(parent)
    , m_interior(interior)
{
    setMinimumSize(560, 380);
    setMouseTracking(false);
}

// Ship-local (x forward, z sideways) -> widget: the bow points right
QTransform ShipPlanView::transform() const
{
    const QRectF b = m_interior->bounds().adjusted(-0.4, -0.4, 0.4, 0.4);
    const double scale = std::min(width() / b.width(), height() / b.height()) * m_zoom;
    QTransform t;
    t.translate(width() / 2.0 + m_pan.x(), height() / 2.0 + m_pan.y());
    t.scale(scale, scale);
    t.translate(-b.center().x(), -b.center().y());
    return t;
}

void ShipPlanView::paintEvent(QPaintEvent *)
{
    ++m_frame;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(14, 16, 20));
    // Grid
    p.setPen(QColor(255, 255, 255, 12));
    for (int x = 0; x < width(); x += 24)
        p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 24)
        p.drawLine(0, y, width(), y);

    const QTransform t = transform();
    const double unit = t.m11();                // pixels per ship unit
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(std::clamp(int(unit * 0.17), 9, 18));
    font.setBold(true);
    QFont small = font;
    small.setBold(false);
    small.setPixelSize(std::max(8, font.pixelSize() - 2));
    const bool pulse = (m_frame / 4) % 2 == 0;

    const QList<Interior::Compartment> &rooms = m_interior->compartments();
    // Doors between open neighbours
    p.setPen(QPen(QColor(150, 150, 140, 120), 1.0));
    for (int i = 0; i < rooms.size(); ++i)
        for (int n : rooms[i].neighbours)
            if (n > i) {
                const QPointF a = t.map(rooms[i].rect.center());
                const QPointF b = t.map(rooms[n].rect.center());
                p.setPen(QPen(rooms[i].sealed || rooms[n].sealed ? QColor(255, 120, 60, 90) : QColor(150, 150, 140, 70),
                              1.0, Qt::DashLine));
                p.drawLine(a, b);
            }

    for (int i = 0; i < rooms.size(); ++i) {
        const Interior::Compartment &c = rooms[i];
        const QRectF r = t.mapRect(c.rect).adjusted(2, 2, -2, -2);
        // Fill: air level (blue) / vacuum (black) / locked (grey hatched)
        if (c.locked) {
            p.setBrush(QBrush(QColor(70, 70, 70), Qt::BDiagPattern));
            p.setPen(QPen(QColor(110, 110, 110), 1.0));
            p.drawRect(r);
        } else {
            const float air = c.air / 100.0f;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(int(25 + 20 * air), int(40 + 60 * air), int(55 + 110 * air)));
            p.drawRect(r);
            if (c.fire > 0.0f) {
                QColor fire = QColor(255, 120, 20);
                fire.setAlphaF(0.35 + 0.45 * c.fire * (pulse ? 1.0 : 0.8));
                p.setBrush(fire);
                p.drawRect(r);
            }
            // Frame: sealed = thick, breach = red, selected = gold
            QPen pen(QColor(170, 175, 170), 1.2);
            if (c.sealed)
                pen = QPen(QColor(255, 150, 60), 3.0);
            if (c.breach || c.vented)
                pen = QPen(UiPalette::critical(), c.sealed ? 3.0 : 2.0, c.vented ? Qt::DotLine : Qt::SolidLine);
            p.setBrush(Qt::NoBrush);
            p.setPen(pen);
            p.drawRect(r);
        }
        if (i == m_selected) {
            p.setPen(QPen(QColor(226, 191, 62), 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(-3, -3, 3, 3));
        }

        // Labels
        p.setFont(font);
        p.setPen(c.locked ? QColor(150, 150, 150) : QColor(235, 235, 225));
        const QRectF text = r.adjusted(4, 3, -4, -3);
        p.drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, shown(c.name));
        QStringList state;
        if (c.locked)
            state << QCoreApplication::translate("ShipPlanView", "CLOSED");
        else {
            if (!c.system.isEmpty())
                state << QStringLiteral("%1 %2%").arg(c.system.toUpper()).arg(qRound(m_health.value(c.system, 100)));
            state << QCoreApplication::translate("ShipPlanView", "air %1%").arg(qRound(c.air));
            if (c.integrity < 99.0f)
                state << QCoreApplication::translate("ShipPlanView", "struct %1%").arg(qRound(c.integrity));
            if (c.fire > 0.0f)
                state << QCoreApplication::translate("ShipPlanView", "FIRE");
            if (c.breach)
                state << QCoreApplication::translate("ShipPlanView", "BREACH");
            if (c.vented)
                state << QCoreApplication::translate("ShipPlanView", "VENTED");
            if (c.leak)
                state << QCoreApplication::translate("ShipPlanView", "LEAK");
            for (const QString &m : c.modules)
                if (const InteriorDef::Module *module = InteriorDef::module(m))
                    state << QStringLiteral("[%1]").arg(shown(module->name));
            if (c.bays > 0)
                state << QCoreApplication::translate("ShipPlanView", "bays %1/%2").arg(c.modules.size()).arg(c.bays);
        }
        p.setFont(small);
        p.setPen(c.fire > 0.0f || c.breach ? QColor(255, 220, 190) : QColor(190, 200, 210));
        p.drawText(text.adjusted(0, QFontMetricsF(font).height() * (unit > 120 ? 1.0 : 1.9), 0, 0),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, state.join(unit > 120 ? "  " : "\n"));
        // Leak: yellow bolt in the corner
        if (c.leak && pulse) {
            p.setPen(QPen(QColor(255, 230, 60), 2.0));
            const QPointF o = r.topRight() + QPointF(-14, 6);
            p.drawPolyline(QPolygonF{o, o + QPointF(-5, 8), o + QPointF(2, 8), o + QPointF(-4, 16)});
        }
        // Boarders: red skulls (dots) with their count
        if (c.boarders > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(UiPalette::hostile());
            const QPointF o = r.bottomRight() + QPointF(-16, -14);
            p.drawEllipse(o, 7, 7);
            p.setPen(Qt::white);
            p.setFont(small);
            p.drawText(QRectF(o.x() - 7, o.y() - 7, 14, 14), Qt::AlignCenter, QString::number(c.boarders));
        }
    }

    // Repair jobs: progress bar in their compartment
    for (const Interior::Job &job : m_interior->jobs()) {
        const int index = m_interior->find(job.compartment);
        if (index < 0)
            continue;
        const QRectF r = t.mapRect(rooms[index].rect).adjusted(6, 0, -6, -6);
        const QRectF bar(r.left(), r.bottom() - 6, r.width(), 5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 40, 40, 220));
        p.drawRect(bar);
        p.setBrush(job.workers.isEmpty() ? QColor(140, 140, 140) : UiPalette::ok());
        const double done = 1.0 - std::clamp(double(job.work / std::max(0.01f, job.total)), 0.0, 1.0);
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * done, bar.height()));
    }

    // Crew: named characters as initials in their compartment
    QHash<QString, int> perRoom;
    p.setFont(small);
    for (const Interior::Person &person : m_interior->people()) {
        if (person.generic)
            continue;
        const int index = m_interior->find(person.location);
        if (index < 0)
            continue;
        const QRectF r = t.mapRect(rooms[index].rect);
        const int k = perRoom[person.location]++;
        const QPointF pos(r.left() + 12 + (k % 4) * 20, r.bottom() - 20 - (k / 4) * 18);
        QColor color = person.injury > 0.0f ? UiPalette::critical()
                     : person.fatigue > 70.0f ? UiPalette::warning()
                     : person.role == InteriorDef::Role::Military ? QColor(120, 200, 120)
                                                                  : QColor(150, 200, 255);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(pos, 8, 8);
        p.setPen(QColor(10, 10, 10));
        p.drawText(QRectF(pos.x() - 8, pos.y() - 8, 16, 16), Qt::AlignCenter, initials(person.name));
    }

    // Bow arrow and legend
    p.setPen(QColor(180, 180, 170));
    p.setFont(small);
    p.drawText(QRectF(8, height() - 20, width() - 16, 16), Qt::AlignLeft,
               QCoreApplication::translate("ShipPlanView", "wheel: zoom  -  drag: move  -  click: details  -  bow to the right"));
}

void ShipPlanView::wheelEvent(QWheelEvent *event)
{
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double zoom = std::clamp(m_zoom * factor, 0.6, 5.0);
    // Zoom around the cursor
    const QPointF cursor = event->position() - QPointF(width() / 2.0, height() / 2.0);
    m_pan = cursor - (cursor - m_pan) * (zoom / m_zoom);
    m_zoom = zoom;
    update();
    event->accept();
}

void ShipPlanView::mousePressEvent(QMouseEvent *event)
{
    m_pressPos = m_lastPos = event->position();
    m_dragged = false;
}

void ShipPlanView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPointF delta = event->position() - m_lastPos;
    m_lastPos = event->position();
    if ((event->position() - m_pressPos).manhattanLength() > 4)
        m_dragged = true;
    m_pan += delta;
    update();
}

void ShipPlanView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragged)
        return;
    const QPointF local = transform().inverted().map(event->position());
    for (int i = 0; i < m_interior->compartments().size(); ++i)
        if (m_interior->compartments()[i].rect.contains(local)) {
            m_selected = i;
            emit compartmentClicked(i);
            update();
            return;
        }
}

// ================================================================
//  ShipPlanDialog
// ================================================================

ShipPlanDialog::ShipPlanDialog(const Interior *interior, HealthProvider health, QWidget *parent)
    : QDialog(parent)
    , m_interior(interior)
    , m_healthProvider(std::move(health))
    , m_view(new ShipPlanView(interior, this))
    , m_details(new QLabel(this))
    , m_summary(new QLabel(this))
    , m_timer(new QTimer(this))
{
    setWindowTitle(tr("Destiny - ship plan"));
    setStyleSheet("QDialog { background: #0e1014; } QLabel { color: #e8e2c8; }");
    resize(1100, 640);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(UiPalette::scaled(13));
    m_details->setFont(mono);
    m_details->setWordWrap(true);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_details->setMinimumWidth(300);
    m_details->setText(tr("Click a compartment for its details.\n\nOrders: repair <room|system>, seal <room>, "
                          "unseal <room>, vent <room>, unlock <room>, install <module> [room], defend <room>, "
                          "crew, jobs"));
    m_summary->setFont(mono);
    auto *side = new QVBoxLayout;
    side->addWidget(m_summary);
    side->addWidget(m_details, 1);
    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addLayout(side);
    connect(m_view, &ShipPlanView::compartmentClicked, this, [this] { refresh(); });
    connect(m_timer, &QTimer::timeout, this, &ShipPlanDialog::refresh);
    m_timer->start(250);
    refresh();
}

void ShipPlanDialog::refresh()
{
    if (!isVisible())
        return;
    if (m_healthProvider)
        m_view->setSystemHealth(m_healthProvider());
    m_view->update();
    int fires = 0, breaches = 0;
    for (const Interior::Compartment &c : m_interior->compartments()) {
        fires += c.fire > 0.0f ? 1 : 0;
        breaches += c.breach ? 1 : 0;
    }
    m_summary->setText(tr("MORALE %1%   WORKERS FREE %2\nJOBS %3   FIRES %4   BREACHES %5\nPOWER LOST %6   BOARDERS %7")
                           .arg(qRound(m_interior->morale()))
                           .arg(m_interior->freeWorkers())
                           .arg(m_interior->jobs().size())
                           .arg(fires)
                           .arg(breaches)
                           .arg(m_interior->powerLoss())
                           .arg(m_interior->boarders()));
    if (m_view->selected() >= 0)
        m_details->setText(describe(*m_interior, m_view->selected()).join('\n'));
}

QStringList ShipPlanDialog::describe(const Interior &interior, int index)
{
    QStringList lines;
    if (index < 0 || index >= interior.compartments().size())
        return lines;
    const Interior::Compartment &c = interior.compartments()[index];
    lines << QStringLiteral("%1 (%2)").arg(shown(c.name).toUpper(), c.id);
    if (c.locked) {
        QStringList cost;
        for (const auto &[resource, quantity] : c.unlockCost)
            cost << QStringLiteral("%1 %2").arg(quantity).arg(resource);
        lines << tr("Closed section - 'unlock %1' (%2, about %3 s of work)")
                     .arg(c.id, cost.join(", "))
                     .arg(qRound(c.unlockWork / 2));
    }
    if (!c.system.isEmpty())
        lines << tr("System: %1").arg(c.system);
    if (!c.capability.isEmpty())
        lines << tr("Role: %1").arg(c.capability);
    lines << tr("Air %1% - structure %2%").arg(qRound(c.air)).arg(qRound(c.integrity));
    if (c.fire > 0.0f)
        lines << tr("FIRE %1%").arg(qRound(c.fire * 100));
    if (c.breach)
        lines << tr("Hull breach: the air escapes");
    if (c.leak)
        lines << tr("Power conduit leaking");
    if (c.vented)
        lines << tr("Open to space ('unseal %1' to close it)").arg(c.id);
    else if (c.sealed)
        lines << tr("Doors sealed");
    if (c.boarders > 0)
        lines << tr("%1 boarder(s)!").arg(c.boarders);
    if (c.bays > 0)
        lines << tr("Module bays: %1/%2").arg(c.modules.size()).arg(c.bays);
    for (const QString &m : c.modules)
        if (const InteriorDef::Module *module = InteriorDef::module(m))
            lines << QStringLiteral("  [%1] %2").arg(shown(module->name), shown(module->effect));
    QStringList people;
    for (const Interior::Person &p : interior.people())
        if (!p.generic && p.location == c.id)
            people << p.name;
    if (!people.isEmpty())
        lines << tr("Here: %1").arg(people.join(", "));
    for (const Interior::Job &j : interior.jobs())
        if (j.compartment == c.id)
            lines << tr("Job #%1 %2: %3% (%4 worker(s))")
                         .arg(j.id)
                         .arg(j.kind)
                         .arg(qRound(100.0f * (1.0f - j.work / std::max(0.01f, j.total))))
                         .arg(j.workers.size());
    return lines;
}
