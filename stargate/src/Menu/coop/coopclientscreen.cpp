#include "coopclientscreen.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "src/Menu/gamemenu/commandconsole.h"
#include "src/Menu/uipalette.h"
#include "src/network/coopsession.h"

CoopClientScreen::CoopClientScreen(const QString &host, quint16 port, QWidget *parent)
    : QWidget(parent)
    , m_client(new CoopClient(this))
    , m_console(new CommandConsole(this))
    , m_status(new QLabel(this))
    , m_header(new QLabel(this))
    , m_back(new QPushButton(tr("Leave the ship"), this))
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(6, 6, 8));
    setPalette(pal);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(UiPalette::scaled(15));
    m_status->setFont(mono);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_status->setWordWrap(true);
    m_status->setStyleSheet("QLabel { color: #e8e2c8; background: #14171c; border: 1px solid #8a8160; padding: 12px; }");
    m_header->setStyleSheet("QLabel { color: #e2bf3e; font-size: 18px; font-weight: bold; letter-spacing: 2px; }");
    m_back->setStyleSheet("QPushButton { background: #1c1a12; color: #e8e2c8; border: 1px solid #8a8160; padding: 6px 14px; }");
    m_header->setText(tr("CO-PILOT - connecting to %1:%2...").arg(host).arg(port));

    auto *top = new QWidget(this);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(m_back);
    topLayout->addLayout(headerRow);
    topLayout->addWidget(m_status, 1);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(top);
    splitter->addWidget(m_console);
    splitter->setSizes({420, 380});
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->addWidget(splitter);

    connect(m_back, &QPushButton::clicked, this, [this] {
        m_client->disconnectFromHost();
        emit quitToTitleRequested();
    });
    connect(m_console, &CommandConsole::commandEntered, this, [this](const QString &text) {
        m_console->appendLine(text, CommandConsole::Tone::Echo);
        if (!m_client->isConnected())
            m_console->appendLine(tr("Not connected"), CommandConsole::Tone::Warning);
        else
            m_client->sendCommand(text);
    });
    connect(m_client, &CoopClient::connected, this, [this] {
        m_console->appendLine(tr("Connected - waiting for the captain"), CommandConsole::Tone::Info);
    });
    connect(m_client, &CoopClient::welcomed, this, [this](const QString &role, const QStringList &commands) {
        m_header->setText(tr("CO-PILOT - %1 STATION").arg(role.toUpper()));
        m_console->appendLine(tr("Aboard the Destiny at the %1 station. Your orders: %2").arg(role, commands.join(", ")),
                              CommandConsole::Tone::Success);
        QStringList words = commands;
        words.sort();
        m_console->setCompletions(words);
    });
    connect(m_client, &CoopClient::log, this, [this](const QString &text, int tone) {
        m_console->appendLine(text, CommandConsole::Tone(std::clamp(tone, 0, 4)));
    });
    connect(m_client, &CoopClient::status, this, [this](const QStringList &lines) { m_status->setText(lines.join('\n')); });
    connect(m_client, &CoopClient::denied, this, [this](const QString &text) {
        m_console->appendLine(tr("'%1' is not an order of your station").arg(text), CommandConsole::Tone::Warning);
    });
    connect(m_client, &CoopClient::disconnected, this, [this](const QString &reason) {
        m_header->setText(tr("CO-PILOT - disconnected"));
        m_console->appendLine(tr("Disconnected: %1").arg(reason), CommandConsole::Tone::Critical);
    });
    m_console->appendLine(tr("The captain opens the co-op with 'coop host tactical' or 'coop host engineering'"));
    m_client->connectTo(host, port);
}
