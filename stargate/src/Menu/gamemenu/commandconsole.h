#ifndef COMMANDCONSOLE_H
#define COMMANDCONSOLE_H

#include <QFrame>
#include <QStringList>
#include <functional>

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

    // Tab completion: returns the full lines that can complete the text
    using CompletionProvider = std::function<QStringList(const QString &text)>;
    void setCompletionProvider(CompletionProvider provider);

signals:
    void commandEntered(const QString &command);
    void lineAppended(const QString &text, CommandConsole::Tone tone);     // co-op mirror

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void submit();
    void completeWithTab();

    QTextEdit *m_log = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;
    QStringList m_history;
    int m_historyPos = -1;
    CompletionProvider m_completionProvider;
    QString m_lastTabText;      // second Tab on the same text lists the choices
};

#endif // COMMANDCONSOLE_H
