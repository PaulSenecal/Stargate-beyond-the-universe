#include "campaign.h"
#include "ui_campaign.h"

Campaign::Campaign(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Campaign)
{
    ui->setupUi(this);
    setupUI();
    initializeCommandManager();
    createConnections();
}

Campaign::~Campaign()
{
    delete ui;
}


void Campaign::setupUI()
{
    // Configuration de la fenêtre principale
    setWindowTitle("Command Control System");
    resize(800, 600);

    // Widget central et layout
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    m_mainLayout = new QVBoxLayout(m_centralWidget);

    // Création des widgets
    QLabel *commandLabel = new QLabel("Enter Command:", this);
    m_commandInput = new QLineEdit(this);
    m_commandInput->setPlaceholderText("Type your command here...");

    m_showCommandsButton = new QPushButton("Show Available Commands", this);

    m_outputDisplay = new QTextEdit(this);
    m_outputDisplay->setReadOnly(true);

    // Ajout des widgets au layout
    m_mainLayout->addWidget(commandLabel);
    m_mainLayout->addWidget(m_commandInput);
    m_mainLayout->addWidget(m_showCommandsButton);
    m_mainLayout->addWidget(m_outputDisplay);

    // Configuration de la barre de status
    //statusBar()->showMessage("Ready");
}

void Campaign::initializeCommandManager()
{
    m_commandManager = new CommandManager(this);
    if (!m_commandManager->loadCommandsFromFile(":/commandList/commands.json")) {
        m_outputDisplay->append("Error: Could not load commands file");
        statusBar()->showMessage("Failed to load commands", 3000);
    } else {
        m_outputDisplay->append("Commands loaded successfully");
    }
}

void Campaign::createConnections()
{
    // Connexion pour l'entrée de commande
    connect(m_commandInput, &QLineEdit::returnPressed,
            this, &Campaign::handleCommand);

    // Connexion pour la validation des commandes
    connect(m_commandManager, &CommandManager::commandValidated,
            this, &Campaign::displayCommandResult);

    // Connexion pour afficher les commandes disponibles
    connect(m_showCommandsButton, &QPushButton::clicked,
            this, &Campaign::showAvailableCommands);
}

void Campaign::handleCommand()
{
    QString command = m_commandInput->text().trimmed();
    if (command.isEmpty()) {
        return;
    }

    m_outputDisplay->append("> " + command);
    m_commandManager->validateCommand(command);
    m_commandInput->clear();
}

void Campaign::displayCommandResult(bool valid, const QString &message)
{
    QString formattedMessage = valid ?
                                   "✓ " + message :
                                   "✗ Error: " + message;

    /*m_outputDisplay->append(formattedMessage);
    statusBar()->showMessage(formattedMessage, 3000);*/

    // Style conditionnel pour le message
    QString color = valid ? "green" : "red";
    m_outputDisplay->append(QString("<p style='color: %1'>%2</p>")
                                .arg(color)
                                .arg(formattedMessage));
}

void Campaign::showAvailableCommands()
{
    QStringList commands = m_commandManager->getAvailableCommands();
    m_outputDisplay->append("\nAvailable Commands:");
    for (const QString &cmd : commands) {
        m_outputDisplay->append("- " + cmd);
    }
}
