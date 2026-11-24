#ifndef CAMPAIGN_H
#define CAMPAIGN_H
#include <QMainWindow>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>

#include "src/core/commandmanager.h"

namespace Ui {
class Campaign;
}

class Campaign : public QMainWindow
{
    Q_OBJECT

public:
    explicit Campaign(QWidget *parent = nullptr);
    ~Campaign();

private slots:
    void handleCommand();
    void displayCommandResult(bool valid, const QString &message);
    void showAvailableCommands();

private:
    void setupUI();
    void initializeCommandManager();
    void createConnections();
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    QLineEdit *m_commandInput;
    QTextEdit *m_outputDisplay;
    QPushButton *m_showCommandsButton;
    CommandManager *m_commandManager;

    Ui::Campaign *ui;
};

#endif // CAMPAIGN_H
