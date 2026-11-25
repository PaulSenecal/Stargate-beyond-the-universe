#ifndef COMMANDCONSOLE_H
#define COMMANDCONSOLE_H

#include <QFrame>
#include <QList>
#include <QStringList>
#include <functional>

class QCompleter;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QUrl;

// ================================================================
//  CommandConsole
//  Bottom console of the game screens: an event log on an olive grid
//  panel and the yellow command bar underneath.
//  - Channels: every line belongs to Combat, Ship (crew, rooms) or
//    Journey (route, galaxy); the tabs above the log filter them. The
//    answers to your orders and the general lines are always shown.
//  - The same line repeated is grouped ("x4"); each line has the game time
//  - Alerts that need an action stay pinned above the log; a click on one
//    writes the order to type ('repair engines')
//  - Words in 'quotes' and targets T2 are clickable: they go to the
//    command bar
//  - Up / Down: command history; Tab: completion; a hint line shows what
//    can be typed next
// ================================================================

class CommandConsole : public QFrame
{
    Q_OBJECT

public:
    enum class Tone { Echo, Info, Success, Warning, Critical };
    enum class Channel { General, Combat, Ship, Journey };

    // An alert pinned above the log
    struct Alert {
        QString text;
        Tone tone = Tone::Warning;
        QString command;            // written in the command bar on click ("" = none)
    };

    explicit CommandConsole(QWidget *parent = nullptr);

    void appendLine(const QString &text, Tone tone = Tone::Info, Channel channel = Channel::General);
    // Help layout: a section title, then "syntax   description" rows
    void appendHeading(const QString &title);
    void appendEntry(const QString &syntax, const QString &description);
    // Aligned table (crew, jobs, planets…)
    void appendTable(const QStringList &headers, const QList<QStringList> &rows);
    void clearLog();
    void setCompletions(const QStringList &words);
    void focusInput();

    // Tab completion: returns the full lines that can complete the text
    using CompletionProvider = std::function<QStringList(const QString &text)>;
    void setCompletionProvider(CompletionProvider provider);
    // Game time shown in front of each line ("D3 14:30")
    void setTimeProvider(std::function<QString()> provider) { m_timeProvider = std::move(provider); }
    // Alerts, asked twice a second
    void setAlertProvider(std::function<QList<Alert>()> provider) { m_alertProvider = std::move(provider); }

signals:
    void commandEntered(const QString &command);
    void lineAppended(const QString &text, CommandConsole::Tone tone);     // co-op mirror

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry {
        enum class Kind { Line, Heading, Row, Html } kind = Kind::Line;
        QString text, extra;        // Row: syntax / description; Html: prebuilt
        Tone tone = Tone::Info;
        Channel channel = Channel::General;
        QString time;
        int count = 1;
    };

    void submit();
    void completeWithTab();
    void updateHint();
    void add(Entry entry);
    bool visible(const Entry &entry) const;
    QString htmlOf(const Entry &entry) const;
    void rebuild();                 // after a change of tab
    void setFilter(Channel channel);
    void updateTabs();
    void updateAlerts();
    void onLink(const QUrl &url);

    QTextBrowser *m_log = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;
    QStringList m_history;
    int m_historyPos = -1;
    CompletionProvider m_completionProvider;
    QString m_lastTabText;      // second Tab on the same text lists the choices
    QLabel *m_hint = nullptr;   // what can be typed next, live

    QList<Entry> m_entries;
    bool m_inCommand = false;   // lines written while an order runs: its answer
    Channel m_filter = Channel::General;        // General = every channel
    QList<QPushButton *> m_tabs;
    int m_unread[4] = {};
    std::function<QString()> m_timeProvider;
    std::function<QList<Alert>()> m_alertProvider;
    QWidget *m_alertBar = nullptr;
    QHBoxLayout *m_alertLayout = nullptr;
    QStringList m_shownAlerts;
};

#endif // COMMANDCONSOLE_H
