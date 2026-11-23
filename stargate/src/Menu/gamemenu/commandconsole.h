#ifndef COMMANDCONSOLE_H
#define COMMANDCONSOLE_H

#include <QFrame>
#include <QStringList>

class QCompleter;
class QLineEdit;
class QTextEdit;

// ================================================================
//  CommandConsole
//  Bottom console of the game menu: an event log on an olive grid
//  panel and the yellow command bar underneath.
//  - Up / Down: command history
//  - Auto-completion on the known command names
// ================================================================

class CommandConsole : public QFrame
{
    Q_OBJECT

public:
    enum class Tone { Echo, Info, Success, Warning, Critical };

    explicit CommandConsole(QWidget *parent = nullptr);

    void appendLine(const QString &text, Tone tone = Tone::Info);
    void clearLog();
    void setCompletions(const QStringList &words);
    void focusInput();

signals:
    void commandEntered(const QString &command);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void submit();

    QTextEdit *m_log = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;
    QStringList m_history;
    int m_historyPos = -1;
};

#endif // COMMANDCONSOLE_H
