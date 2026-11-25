#include "commandconsole.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "src/Menu/uipalette.h"

namespace {
const QColor kPanel(75, 70, 50);
const QColor kGrid(92, 86, 63);
const QColor kBar(226, 191, 62);
constexpr int kMaxEntries = 600;

QString colorOf(CommandConsole::Tone tone)
{
    switch (tone) {
    case CommandConsole::Tone::Echo:     return QStringLiteral("#a9a28a");
    case CommandConsole::Tone::Info:     return QStringLiteral("#e8e2c8");
    case CommandConsole::Tone::Success:  return gameConfig().colorBlind ? QStringLiteral("#6ab8ff") : QStringLiteral("#8be07a");
    case CommandConsole::Tone::Warning:  return QStringLiteral("#f2c94c");
    case CommandConsole::Tone::Critical: return gameConfig().colorBlind ? QStringLiteral("#ff9a1a") : QStringLiteral("#ff5a48");
    }
    return QStringLiteral("#e8e2c8");
}

// 'orders in quotes' and targets T2 become links to the command bar
QString linkify(const QString &escaped, const QString &color)
{
    static const QRegularExpression quoted(QStringLiteral("'([^'&<>]{2,60})'"));
    static const QRegularExpression target(QStringLiteral("\\bT(\\d{1,2})\\b"));
    QString out = escaped;
    // Links keep the color of their line (Qt ignores "inherit")
    out.replace(quoted, QStringLiteral("<a href=\"cmd:\\1\" style=\"color:%1;\">'\\1'</a>").arg(color));
    out.replace(target, QStringLiteral("<a href=\"add:\\1\" style=\"color:%1;\">T\\1</a>").arg(color));
    return out;
}
} // namespace

CommandConsole::CommandConsole(QWidget *parent)
    : QFrame(parent)
    , m_log(new QTextBrowser(this))
    , m_input(new QLineEdit(this))
    , m_completer(new QCompleter(this))
{
    setMinimumHeight(200);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(UiPalette::scaled(17));

    m_log->setReadOnly(true);
    m_log->setOpenLinks(false);
    m_log->setFont(mono);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->viewport()->setAutoFillBackground(false);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setStyleSheet(
        "QTextBrowser { background: transparent; color: #e8e2c8; }"
        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #8a8160; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    connect(m_log, &QTextBrowser::anchorClicked, this, &CommandConsole::onLink);

    QFont inputFont = mono;
    inputFont.setPixelSize(UiPalette::scaled(19));
    inputFont.setBold(true);
    m_input->setFont(inputFont);
    m_input->setFixedHeight(UiPalette::scaled(34));
    m_input->setFrame(false);
    m_input->setPlaceholderText(tr("Type a command — Tab completes, 'help' for the list"));
    m_input->setStyleSheet("QLineEdit { background: transparent; color: #111; }");
    m_input->installEventFilter(this);

    m_completer->setModel(new QStringListModel(m_completer));
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->popup()->setFont(mono);
    m_completer->popup()->setStyleSheet(
        "QListView { background: #1c1a12; color: #e8e2c8; border: 1px solid #e2bf3e; }"
        "QListView::item:selected { background: #e2bf3e; color: #111; }");
    m_input->setCompleter(m_completer);

    // Live hint: the words that can follow what is typed
    m_hint = new QLabel(this);
    QFont hintFont = mono;
    hintFont.setPixelSize(UiPalette::scaled(13));
    m_hint->setFont(hintFont);
    m_hint->setStyleSheet("QLabel { color: #b9b08a; }");
    m_hint->setTextFormat(Qt::RichText);

    // Tabs filtering the log by channel
    QFont tabFont = mono;
    tabFont.setPixelSize(UiPalette::scaled(13));
    tabFont.setBold(true);
    auto *tabRow = new QHBoxLayout;
    tabRow->setSpacing(6);
    for (int i = 0; i < 4; ++i) {
        auto *tab = new QPushButton(this);
        tab->setCheckable(true);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFont(tabFont);
        tab->setStyleSheet(
            "QPushButton { background: rgba(20, 19, 14, 150); color: #c9c3a8; border: 1px solid #6d6548;"
            "              border-radius: 3px; padding: 2px 10px; }"
            "QPushButton:checked { background: #e2bf3e; color: #111; border-color: #e2bf3e; }"
            "QPushButton:hover { border-color: #e2bf3e; }");
        connect(tab, &QPushButton::clicked, this, [this, i] { setFilter(Channel(i)); });
        tabRow->addWidget(tab);
        m_tabs << tab;
    }
    tabRow->addStretch(1);

    // Alerts pinned above the log
    m_alertBar = new QWidget(this);
    m_alertLayout = new QHBoxLayout(m_alertBar);
    m_alertLayout->setContentsMargins(0, 0, 0, 0);
    m_alertLayout->setSpacing(6);
    m_alertBar->hide();
    auto *alertTimer = new QTimer(this);
    connect(alertTimer, &QTimer::timeout, this, &CommandConsole::updateAlerts);
    alertTimer->start(500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(56, 18, 110, 12);
    layout->setSpacing(6);
    layout->addLayout(tabRow);
    layout->addWidget(m_alertBar);
    layout->addWidget(m_log, 1);
    layout->addWidget(m_hint);
    layout->addSpacing(6);
    layout->addWidget(m_input);
    connect(m_input, &QLineEdit::textEdited, this, &CommandConsole::updateHint);

    connect(m_input, &QLineEdit::returnPressed, this, &CommandConsole::submit);
    updateTabs();
}

// ================================================================
//  Lines
// ================================================================

void CommandConsole::appendLine(const QString &text, Tone tone, Channel channel)
{
    Entry e;
    e.text = text;
    e.tone = tone;
    e.channel = channel;
    add(e);
    emit lineAppended(text, tone);
}

void CommandConsole::appendHeading(const QString &title)
{
    Entry e;
    e.kind = Entry::Kind::Heading;
    e.text = title;
    add(e);
    emit lineAppended(title.toUpper(), Tone::Info);
}

void CommandConsole::appendEntry(const QString &syntax, const QString &description)
{
    Entry e;
    e.kind = Entry::Kind::Row;
    e.text = syntax;
    e.extra = description;
    add(e);
    emit lineAppended(QStringLiteral("  %1  %2").arg(syntax, description), Tone::Info);
}

void CommandConsole::appendTable(const QStringList &headers, const QList<QStringList> &rows)
{
    // A real table: columns stay aligned whatever the text
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\" style=\"margin-left:12px;\"><tr>");
    for (const QString &h : headers)
        html += QStringLiteral("<td style=\"color:#e2bf3e; font-weight:bold; padding-right:18px;\">%1</td>").arg(h.toHtmlEscaped());
    html += QStringLiteral("</tr>");
    QStringList plain{headers.join(" | ")};
    for (const QStringList &row : rows) {
        html += QStringLiteral("<tr>");
        for (int i = 0; i < row.size(); ++i)
            html += QStringLiteral("<td style=\"color:%1; padding-right:18px;\">%2</td>")
                        .arg(i == 0 ? QStringLiteral("#7fd4ff") : QStringLiteral("#e8e2c8"),
                             linkify(row[i].toHtmlEscaped(), i == 0 ? QStringLiteral("#7fd4ff") : QStringLiteral("#e8e2c8")));
        html += QStringLiteral("</tr>");
        plain << row.join(" | ");
    }
    html += QStringLiteral("</table>");
    Entry e;
    e.kind = Entry::Kind::Html;
    e.text = html;
    add(e);
    for (const QString &line : plain)
        emit lineAppended(line, Tone::Info);
}

void CommandConsole::add(Entry entry)
{
    // The answers to an order are shown whatever the tab
    if (m_inCommand)
        entry.channel = Channel::General;
    entry.time = m_timeProvider ? m_timeProvider() : QString();

    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    const int position = bar->value();

    // The same line again: one line with a counter
    if (entry.kind == Entry::Kind::Line && !m_entries.isEmpty()) {
        Entry &last = m_entries.last();
        if (last.kind == Entry::Kind::Line && last.text == entry.text && last.tone == entry.tone
            && last.channel == entry.channel && entry.tone != Tone::Echo) {
            ++last.count;
            last.time = entry.time;
            if (visible(last)) {
                QTextCursor cursor(m_log->document()->lastBlock());
                cursor.select(QTextCursor::BlockUnderCursor);
                cursor.removeSelectedText();
                m_log->append(htmlOf(last));
            }
            bar->setValue(atBottom ? bar->maximum() : position);
            return;
        }
    }

    m_entries << entry;
    if (m_entries.size() > kMaxEntries) {
        m_entries.removeFirst();
        if (m_log->document()->blockCount() > kMaxEntries) {
            QTextCursor cursor(m_log->document()->firstBlock());
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();
            cursor.deleteChar();
        }
    }
    if (visible(entry)) {
        m_log->append(htmlOf(entry));
        bar->setValue(atBottom || entry.kind != Entry::Kind::Line ? bar->maximum() : position);
    } else {
        ++m_unread[int(entry.channel)];
        updateTabs();
    }
}

bool CommandConsole::visible(const Entry &entry) const
{
    return m_filter == Channel::General || entry.channel == Channel::General || entry.channel == m_filter;
}

QString CommandConsole::htmlOf(const Entry &e) const
{
    const QString time = e.time.isEmpty() ? QString()
                       : QStringLiteral("<span style=\"color:#8a8466; font-size:small;\">%1 </span>").arg(e.time.toHtmlEscaped());
    switch (e.kind) {
    case Entry::Kind::Heading:
        return QStringLiteral("<br><span style=\"color:#e2bf3e; font-weight:bold;\">&#9632; %1</span>")
            .arg(e.text.toUpper().toHtmlEscaped());
    case Entry::Kind::Row: {
        QString padded = e.text.toHtmlEscaped();
        for (int i = int(e.text.size()); i < 30; ++i)
            padded += QStringLiteral("&nbsp;");
        return QStringLiteral("&nbsp;&nbsp;<a href=\"cmd:%1\" style=\"color:#7fd4ff; font-weight:bold; text-decoration:none;\">%2</a> "
                              "<span style=\"color:#c9c3a8;\">%3</span>")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(e.text.section(' ', 0, 0))), padded, e.extra.toHtmlEscaped());
    }
    case Entry::Kind::Html:
        return e.text;
    case Entry::Kind::Line:
        break;
    }
    const QString prefix = e.tone == Tone::Echo ? QStringLiteral("&gt; ") : QStringLiteral("- ");
    const QString count = e.count > 1 ? QStringLiteral(" <span style=\"color:#e2bf3e;\">x%1</span>").arg(e.count) : QString();
    const QString body = e.tone == Tone::Echo ? e.text.toHtmlEscaped() : linkify(e.text.toHtmlEscaped(), colorOf(e.tone));
    return QStringLiteral("%1<span style=\"color:%2\">%3%4</span>%5").arg(time, colorOf(e.tone), prefix, body, count);
}

void CommandConsole::rebuild()
{
    m_log->clear();
    QStringList html;
    for (const Entry &e : m_entries)
        if (visible(e))
            html << htmlOf(e);
    m_log->setHtml(html.join(QStringLiteral("<br>")));
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void CommandConsole::clearLog()
{
    m_entries.clear();
    std::fill(std::begin(m_unread), std::end(m_unread), 0);
    m_log->clear();
    updateTabs();
}

// ================================================================
//  Tabs, alerts, links
// ================================================================

void CommandConsole::setFilter(Channel channel)
{
    m_filter = channel;
    m_unread[int(channel)] = 0;
    if (channel == Channel::General)
        std::fill(std::begin(m_unread), std::end(m_unread), 0);
    updateTabs();
    rebuild();
}

void CommandConsole::updateTabs()
{
    const QString names[4] = {tr("All"), tr("Combat"), tr("Ship"), tr("Journey")};
    for (int i = 0; i < m_tabs.size(); ++i) {
        m_tabs[i]->setChecked(int(m_filter) == i);
        const int unread = i == 0 ? 0 : m_unread[i];
        m_tabs[i]->setText(unread > 0 && int(m_filter) != i && m_filter != Channel::General
                               ? QStringLiteral("%1 (%2)").arg(names[i]).arg(unread)
                               : names[i]);
    }
}

void CommandConsole::updateAlerts()
{
    const QList<Alert> alerts = m_alertProvider ? m_alertProvider() : QList<Alert>();
    QStringList keys;
    for (const Alert &a : alerts)
        keys << a.text + '|' + a.command + '|' + QString::number(int(a.tone));
    if (keys == m_shownAlerts)
        return;
    m_shownAlerts = keys;
    while (QLayoutItem *item = m_alertLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(UiPalette::scaled(13));
    font.setBold(true);
    for (const Alert &a : alerts) {
        auto *chip = new QPushButton(a.command.isEmpty() ? a.text : QStringLiteral("%1  ▸").arg(a.text), m_alertBar);
        chip->setFont(font);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setCursor(a.command.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        chip->setToolTip(a.command.isEmpty() ? QString() : tr("Click: '%1'").arg(a.command));
        const QString color = colorOf(a.tone);
        chip->setStyleSheet(QStringLiteral("QPushButton { background: rgba(10, 8, 4, 190); color: %1; border: 1px solid %1;"
                                           "  border-radius: 3px; padding: 2px 8px; }"
                                           "QPushButton:hover { background: %1; color: #111; }")
                                .arg(color));
        const QString command = a.command;
        connect(chip, &QPushButton::clicked, this, [this, command] {
            if (command.isEmpty())
                return;
            m_input->setText(command);
            m_input->setFocus();
            updateHint();
        });
        m_alertLayout->addWidget(chip);
    }
    m_alertLayout->addStretch(1);
    m_alertBar->setVisible(!alerts.isEmpty());
}

void CommandConsole::onLink(const QUrl &url)
{
    const QString value = QUrl::fromPercentEncoding(url.path().toUtf8());
    if (url.scheme() == QLatin1String("cmd")) {
        m_input->setText(value + (value.contains(' ') ? QString() : QStringLiteral(" ")));
    } else if (url.scheme() == QLatin1String("add")) {
        // A target number after what is typed ("attack " + 2)
        QString text = m_input->text();
        if (!text.isEmpty() && !text.endsWith(' '))
            text += ' ';
        m_input->setText(text + value);
    }
    m_input->setFocus();
    updateHint();
}

void CommandConsole::updateHint()
{
    const QString text = m_input->text();
    if (!m_completionProvider || text.trimmed().isEmpty()) {
        m_hint->clear();
        return;
    }
    const QStringList candidates = m_completionProvider(text);
    QStringList words;
    for (const QString &c : candidates) {
        const QString word = c.section(' ', -1);
        if (!word.isEmpty() && !words.contains(word))
            words << word;
    }
    if (words.isEmpty()) {
        m_hint->clear();
        return;
    }
    const int shown = std::min(int(words.size()), 10);
    QString line = tr("Tab &#8594; ") + words.mid(0, shown).join(QStringLiteral("&nbsp;&nbsp;"));
    if (words.size() > shown)
        line += tr("&nbsp;&nbsp;(+%1)").arg(words.size() - shown);
    m_hint->setText(line);
}


void CommandConsole::setCompletions(const QStringList &words)
{
    static_cast<QStringListModel *>(m_completer->model())->setStringList(words);
}

void CommandConsole::focusInput()
{
    m_input->setFocus();
}

void CommandConsole::submit()
{
    const QString command = m_input->text().trimmed();
    if (command.isEmpty())
        return;
    if (m_history.isEmpty() || m_history.last() != command)
        m_history.append(command);
    m_historyPos = -1;
    m_input->clear();
    m_hint->clear();
    // A new command: go back to the bottom to read its result
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    m_inCommand = true;             // what the order writes is its answer
    emit commandEntered(command);
    m_inCommand = false;
}

void CommandConsole::setCompletionProvider(CompletionProvider provider)
{
    m_completionProvider = std::move(provider);
}

void CommandConsole::completeWithTab()
{
    if (!m_completionProvider)
        return;
    const QString text = m_input->text();
    const QStringList candidates = m_completionProvider(text);
    if (candidates.isEmpty()) {
        m_lastTabText.clear();
        return;
    }
    if (candidates.size() == 1) {
        m_input->setText(candidates.first() + ' ');
        m_lastTabText.clear();
        updateHint();
        return;
    }

    // Several choices: complete up to their common prefix…
    QString common = candidates.first();
    for (const QString &c : candidates) {
        int i = 0;
        while (i < common.size() && i < c.size() && common.at(i).toLower() == c.at(i).toLower())
            ++i;
        common.truncate(i);
    }
    if (common.size() > text.size()) {
        m_input->setText(common);
        m_lastTabText = common;
        updateHint();
        return;
    }

    // …and list them when Tab is pressed again without progress
    if (m_lastTabText == text) {
        QStringList words;
        for (const QString &c : candidates)
            words << c.section(' ', -1);
        appendLine(tr("Possible: %1").arg(words.join("   ")), Tone::Info);
    }
    m_lastTabText = text;
}

bool CommandConsole::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
        m_completer->popup()->hide();
        completeWithTab();
        return true;    // keep the focus in the command bar
    }
    if (watched == m_input && event->type() == QEvent::KeyPress && !m_completer->popup()->isVisible()) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Up && !m_history.isEmpty()) {
            m_historyPos = m_historyPos < 0 ? int(m_history.size()) - 1 : qMax(0, m_historyPos - 1);
            m_input->setText(m_history.at(m_historyPos));
            return true;
        }
        if (key == Qt::Key_Down && m_historyPos >= 0) {
            ++m_historyPos;
            if (m_historyPos >= m_history.size()) {
                m_historyPos = -1;
                m_input->clear();
            } else {
                m_input->setText(m_history.at(m_historyPos));
            }
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommandConsole::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect();
    const QRect in = m_input->geometry();

    // Olive log panel with a rounded notch at the top right
    const qreal bottom = in.top() - 10;
    QPainterPath panel;
    panel.moveTo(r.left(), r.top() + 6);
    panel.lineTo(r.right() - 150, r.top() + 6);
    panel.cubicTo(r.right() - 95, r.top() + 6, r.right() - 95, r.top() + 62, r.right() - 40, r.top() + 62);
    panel.lineTo(r.right() - 40, bottom);
    panel.lineTo(r.left() + 20, bottom);
    panel.lineTo(r.left(), bottom - 20);
    panel.closeSubpath();
    p.fillPath(panel, kPanel);

    p.save();
    p.setClipPath(panel);
    p.setPen(QPen(kGrid, 1));
    for (qreal x = r.left() + 28; x < r.right(); x += 34)
        p.drawLine(QPointF(x, r.top()), QPointF(x, bottom));
    for (qreal y = r.top() + 20; y < bottom; y += 34)
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    p.restore();

    // Yellow slanted command bar behind the line edit
    const QPolygonF bar{
        QPointF(in.left() - 36, in.bottom() + 6),
        QPointF(in.left() - 6,  in.top() - 6),
        QPointF(in.right() + 70, in.top() - 6),
        QPointF(in.right() + 30, in.bottom() + 6),
    };
    p.setPen(Qt::NoPen);
    p.setBrush(kBar);
    p.drawPolygon(bar);
}
