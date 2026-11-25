#include "conquestendscreen.h"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "src/Menu/menustyle.h"

ConquestEndScreen::ConquestEndScreen(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet(MenuStyle::sheet());

    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    m_heading = new QLabel(panel);
    m_heading->setAlignment(Qt::AlignCenter);
    m_subtitle = new QLabel(panel);
    m_subtitle->setObjectName("subtitle");
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setWordWrap(true);
    m_summary = new QLabel(panel);
    m_summary->setObjectName("field");
    m_summary->setAlignment(Qt::AlignCenter);
    auto *title = new QPushButton(tr("Back to the title screen"), panel);
    auto *quit = new QPushButton(tr("Quit game"), panel);
    quit->setObjectName("danger");

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(48, 32, 48, 32);
    layout->setSpacing(14);
    layout->addWidget(m_heading);
    layout->addWidget(m_subtitle);
    layout->addSpacing(6);
    layout->addWidget(m_summary);
    layout->addSpacing(10);
    layout->addWidget(title);
    layout->addWidget(quit);

    auto *outer = new QVBoxLayout(this);
    outer->addStretch(1);
    outer->addWidget(panel, 0, Qt::AlignHCenter);
    outer->addStretch(1);

    connect(title, &QPushButton::clicked, this, &ConquestEndScreen::titleRequested);
    connect(quit, &QPushButton::clicked, this, &ConquestEndScreen::quitRequested);
    if (parent)
        parent->installEventFilter(this);
    hide();
}

void ConquestEndScreen::showResult(bool victory, const QString &faction, const QStringList &summary)
{
    m_victory = victory;
    m_heading->setText(victory ? tr("VICTORY") : tr("DEFEAT"));
    m_heading->setStyleSheet(QStringLiteral("color: %1; font-size: 44px; font-weight: bold; letter-spacing: 8px;")
                                 .arg(victory ? "#e2bf3e" : "#ff5a48"));
    m_subtitle->setText(victory ? tr("The galaxy belongs to the %1").arg(faction)
                                : tr("The %1 have no world left").arg(faction));
    m_summary->setText(summary.join('\n'));
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    show();
    raise();
}

void ConquestEndScreen::showCustom(bool victory, const QString &heading, const QString &subtitle,
                                   const QStringList &summary)
{
    showResult(victory, QString(), summary);
    m_heading->setText(heading);
    m_subtitle->setText(subtitle);
}

void ConquestEndScreen::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_victory ? QColor(10, 8, 0, 175) : QColor(20, 0, 0, 185));
}

// Follows the size of the conquest screen
bool ConquestEndScreen::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}
