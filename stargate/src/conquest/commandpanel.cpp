#include "commandpanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtMath>

#include "conquestgame.h"
#include "src/Menu/menustyle.h"

using namespace Conquest;

namespace {

const QColor kGold(226, 191, 62);
const QColor kText(232, 226, 200);
const QColor kDim(140, 134, 110);
const QColor kDone(95, 191, 106);

} // namespace

// ================================================================
//  TechTreeWidget
// ================================================================

TechTreeWidget::TechTreeWidget(ConquestGame *game, QWidget *parent)
    : QWidget(parent)
    , m_game(game)
{
    setMouseTracking(true);
    setMinimumSize(720, 420);
}

QRectF TechTreeWidget::boxOf(int techIndex) const
{
    const TechInfo &t = techInfos()[techIndex];
    constexpr double margin = 16, header = 34, gapX = 34, gapY = 30;
    const double w = (width() - 2 * margin - 2 * gapX) / 3.0;
    const double h = std::min(120.0, (height() - header - 2 * margin - 2 * gapY) / 3.0);
    return QRectF(margin + t.branch * (w + gapX), header + margin + (t.tier - 1) * (h + gapY), w, h);
}

int TechTreeWidget::techAt(const QPointF &pos) const
{
    for (int i = 0; i < techInfos().size(); ++i)
        if (boxOf(i).contains(pos))
            return i;
    return -1;
}

void TechTreeWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = techAt(event->position());
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TechTreeWidget::mousePressEvent(QMouseEvent *event)
{
    const int index = techAt(event->position());
    if (index < 0)
        return;
    const QString id = techInfos()[index].id;
    QString error;
    if (m_game->startResearch(id, &error))
        emit status(tr("Research started: %1").arg(ConquestGame::techName(id)), true);
    else
        emit status(tr("%1: %2").arg(ConquestGame::techName(id), error), false);
    update();
}

void TechTreeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QStringList branches{tr("WEAPONS"), tr("DEFENSE"), tr("INDUSTRY")};
    const Faction &me = m_game->player();
    QFont font = p.font();

    // Branch titles
    font.setPixelSize(15);
    font.setBold(true);
    p.setFont(font);
    p.setPen(kGold);
    for (int b = 0; b < 3; ++b) {
        const QRectF first = boxOf(b * 3);
        p.drawText(QRectF(first.left(), 8, first.width(), 30), Qt::AlignCenter, branches[b]);
    }

    // Arrows between the tiers of a branch
    for (int i = 0; i < techInfos().size(); ++i) {
        if (techInfos()[i].tier == 3)
            continue;
        const QRectF from = boxOf(i), to = boxOf(i + 1);
        const bool done = me.techs.contains(techInfos()[i].id);
        p.setPen(QPen(done ? kDone : kDim, 2));
        const QPointF a(from.center().x(), from.bottom() + 3), b(to.center().x(), to.top() - 4);
        p.drawLine(a, b);
        QPainterPath head;
        head.moveTo(b);
        head.lineTo(b + QPointF(-6, -8));
        head.lineTo(b + QPointF(6, -8));
        head.closeSubpath();
        p.fillPath(head, done ? kDone : kDim);
    }

    // Technology boxes
    for (int i = 0; i < techInfos().size(); ++i) {
        const TechInfo &t = techInfos()[i];
        const QRectF box = boxOf(i);
        const bool done = me.techs.contains(t.id);
        const bool running = me.research == t.id;
        QString why;
        const bool available = !done && !running && m_game->canResearch(m_game->playerIndex(), t.id, &why);
        // Only the missing previous tier is worth saying (not "busy", not the cost)
        why.clear();
        for (const TechInfo &previous : techInfos())
            if (previous.branch == t.branch && previous.tier == t.tier - 1 && !me.techs.contains(previous.id))
                why = tr("needs %1").arg(ConquestGame::techName(previous.id));

        QColor fill(21, 21, 21), border(68, 68, 68), title = kDim;
        if (done) {
            fill = QColor(24, 48, 28);
            border = kDone;
            title = kText;
        } else if (running) {
            fill = QColor(52, 44, 18);
            border = kGold;
            title = kText;
        } else if (available) {
            fill = QColor(28, 26, 18);
            border = i == m_hover ? kGold : QColor(138, 129, 96);
            title = kText;
        }
        p.setPen(QPen(border, i == m_hover || running ? 2.2 : 1.4));
        p.setBrush(fill);
        p.drawRoundedRect(box, 6, 6);

        const QRectF inner = box.adjusted(10, 8, -10, -8);
        font.setPixelSize(14);
        font.setBold(true);
        p.setFont(font);
        p.setPen(title);
        p.drawText(QRectF(inner.left(), inner.top(), inner.width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1. %2").arg(i + 1).arg(ConquestGame::techName(t.id)));
        font.setPixelSize(12);
        font.setBold(false);
        p.setFont(font);
        p.setPen(done || running || available ? kText : kDim);
        p.drawText(QRectF(inner.left(), inner.top() + 24, inner.width(), inner.height() - 48),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, ConquestGame::techEffect(t.id));

        // Bottom line: state, cost or what is missing
        const QRectF bottom(inner.left(), inner.bottom() - 18, inner.width(), 18);
        if (running) {
            const QRectF bar(bottom.left(), bottom.top() + 6, bottom.width() * 0.62, 7);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(70, 62, 30));
            p.drawRect(bar);
            p.setBrush(kGold);
            p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * me.researchProgress, bar.height()));
            p.setPen(kGold);
            p.drawText(bottom, Qt::AlignRight | Qt::AlignVCenter,
                       tr("%1%").arg(qRound(me.researchProgress * 100)));
        } else if (done) {
            p.setPen(kDone);
            p.drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter, tr("RESEARCHED"));
        } else {
            p.setPen(available ? kGold : kDim);
            const int player = m_game->playerIndex();
            const QString cost = tr("%1 naquadah - %2 s").arg(m_game->researchCost(player, t)).arg(m_game->researchTime(player, t));
            p.drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter,
                       available ? tr("%1 - click to research").arg(cost)
                                 : why.isEmpty() ? cost : cost + QStringLiteral(" - ") + why);
        }
    }
}

// ================================================================
//  CommandPanel
// ================================================================

CommandPanel::CommandPanel(ConquestGame *game, QWidget *parent)
    : QWidget(parent)
    , m_game(game)
{
    setStyleSheet(MenuStyle::sheet()
                  + QStringLiteral("QPushButton#tab { min-width: 150px; padding: 6px 18px; }"
                                   "QPushButton#tab:checked { background: #e2bf3e; color: #111; }"
                                   "QFrame#card { background: rgba(28, 26, 18, 230); border: 1px solid #5c5642;"
                                   "              border-radius: 6px; }"));
    setFocusPolicy(Qt::StrongFocus);

    auto *panel = new QFrame(this);
    panel->setObjectName("panel");

    // Tabs, stock and close
    m_researchTab = new QPushButton(tr("RESEARCH"), panel);
    m_diplomacyTab = new QPushButton(tr("DIPLOMACY"), panel);
    for (QPushButton *tab : {m_researchTab, m_diplomacyTab}) {
        tab->setObjectName("tab");
        tab->setCheckable(true);
        tab->setFocusPolicy(Qt::NoFocus);
    }
    m_stock = new QLabel(panel);
    m_stock->setObjectName("field");
    m_stock->setMinimumWidth(260);
    m_stock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *close = new QPushButton(tr("Close (Esc)"), panel);
    close->setObjectName("tab");
    close->setFocusPolicy(Qt::NoFocus);
    auto *top = new QHBoxLayout;
    top->addWidget(m_researchTab);
    top->addWidget(m_diplomacyTab);
    top->addStretch(1);
    top->addWidget(m_stock);
    top->addSpacing(16);
    top->addWidget(close);

    m_pages = new QStackedWidget(panel);
    m_tree = new TechTreeWidget(game, m_pages);
    m_diplomacy = new QWidget(m_pages);
    m_cards = new QVBoxLayout(m_diplomacy);
    m_cards->setSpacing(10);
    m_pages->addWidget(m_tree);
    m_pages->addWidget(m_diplomacy);

    m_status = new QLabel(panel);
    m_status->setObjectName("note");
    m_status->setMinimumHeight(20);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(12);
    layout->addLayout(top);
    layout->addWidget(m_pages, 1);
    layout->addWidget(m_status);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(40, 30, 40, 30);
    outer->addWidget(panel);

    connect(m_researchTab, &QPushButton::clicked, this, [this] { showPage(Page::Research); });
    connect(m_diplomacyTab, &QPushButton::clicked, this, [this] { showPage(Page::Diplomacy); });
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    connect(m_tree, &TechTreeWidget::status, this, &CommandPanel::setStatus);
    if (parent)
        parent->installEventFilter(this);
    hide();
}

void CommandPanel::open(Page page)
{
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    m_status->clear();
    show();
    raise();
    showPage(page);
    setFocus();
}

void CommandPanel::showPage(Page page)
{
    m_pages->setCurrentIndex(page == Page::Research ? 0 : 1);
    m_researchTab->setChecked(page == Page::Research);
    m_diplomacyTab->setChecked(page == Page::Diplomacy);
    m_diplomacyKey.clear();
    refresh();
}

void CommandPanel::setStatus(const QString &text, bool ok)
{
    m_status->setStyleSheet(ok ? "color: #8be07a;" : "color: #ff8a6a;");
    m_status->setText(text);
    refresh();
}

void CommandPanel::refresh()
{
    if (!isVisible())
        return;
    const Faction &me = m_game->player();
    m_stock->setText(tr("Naquadah %1  -  Trinium %2").arg(int(me.naquadah)).arg(int(me.trinium)));
    if (m_pages->currentIndex() == 0)
        m_tree->update();
    else
        rebuildDiplomacy();
}

// One card per faction, rebuilt only when something shown changes
void CommandPanel::rebuildDiplomacy()
{
    QString key;
    for (int f = 0; f < m_game->factions().size(); ++f) {
        const Faction &faction = m_game->factions()[f];
        key += QStringLiteral("%1/%2/%3/%4/%5;")
                   .arg(faction.allied)
                   .arg(qCeil(faction.allyCooldown))
                   .arg(m_game->planetCount(f))
                   .arg(m_game->fleetCount(f))
                   .arg(faction.techs.size());
    }
    if (key == m_diplomacyKey)
        return;
    m_diplomacyKey = key;

    while (QLayoutItem *item = m_cards->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int f = 0; f < m_game->factions().size(); ++f) {
        if (f == m_game->playerIndex())
            continue;
        const Faction &faction = m_game->factions()[f];
        const bool allied = m_game->isAllied(f);
        const bool gone = m_game->planetCount(f) == 0 && m_game->fleetCount(f) == 0;

        auto *card = new QFrame(m_diplomacy);
        card->setObjectName("card");
        auto *stripe = new QFrame(card);
        stripe->setFixedWidth(8);
        stripe->setStyleSheet(QStringLiteral("background: %1; border-radius: 3px;").arg(faction.color));
        auto *name = new QLabel(QStringLiteral("<span style='font-size:17px; font-weight:bold; color:%1'>%2</span>"
                                               "&nbsp;&nbsp;<span style='color:%3'>%4</span>")
                                    .arg(faction.color, faction.id.toHtmlEscaped(),
                                         gone ? "#8c866e" : allied ? "#8be07a" : "#ff8a6a",
                                         gone ? tr("eliminated") : allied ? tr("ALLY") : tr("AT WAR")),
                                card);
        auto *strength = new QLabel(tr("%1 world(s) - %2 fleet(s) - %3 technolog(ies)")
                                        .arg(m_game->planetCount(f))
                                        .arg(m_game->fleetCount(f))
                                        .arg(faction.techs.size()),
                                    card);
        strength->setObjectName("field");
        auto *trait = new QLabel(ConquestGame::traitSummary(faction.id), card);
        trait->setObjectName("note");
        trait->setWordWrap(true);

        auto *button = new QPushButton(card);
        button->setFocusPolicy(Qt::NoFocus);
        if (gone) {
            button->setText(tr("No one to talk to"));
            button->setEnabled(false);
        } else if (allied) {
            button->setText(tr("Break the alliance"));
            button->setObjectName("danger");
        } else if (faction.allyCooldown > 0) {
            button->setText(tr("They will listen again in %1 s").arg(qCeil(faction.allyCooldown)));
            button->setEnabled(false);
        } else {
            button->setText(tr("Propose an alliance"));
        }
        const QString id = faction.id;
        connect(button, &QPushButton::clicked, this, [this, id, allied] {
            QString answer;
            const bool ok = allied ? m_game->breakAlliance(id, &answer) : m_game->proposeAlliance(id, &answer);
            if (ok)
                setStatus(allied ? tr("Alliance with the %1 broken").arg(id)
                                 : tr("Alliance sealed with the %1").arg(id),
                          !allied);
            else
                setStatus(answer, false);
        });

        auto *text = new QVBoxLayout;
        text->setSpacing(4);
        text->addWidget(name);
        text->addWidget(strength);
        text->addWidget(trait);
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(10, 10, 14, 10);
        row->setSpacing(14);
        row->addWidget(stripe);
        row->addLayout(text, 1);
        row->addWidget(button, 0, Qt::AlignVCenter);
        m_cards->addWidget(card);
    }
    auto *hint = new QLabel(tr("A faction accepts an alliance against a common enemy at least as strong as "
                               "itself, if you do not hold 40% of the galaxy. Above 45%, allies break away."),
                            m_diplomacy);
    hint->setObjectName("note");
    hint->setWordWrap(true);
    m_cards->addWidget(hint);
    m_cards->addStretch(1);
}

void CommandPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 150));
}

void CommandPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        hide();
    else
        QWidget::keyPressEvent(event);
}

// Follows the size of the conquest screen
bool CommandPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}
