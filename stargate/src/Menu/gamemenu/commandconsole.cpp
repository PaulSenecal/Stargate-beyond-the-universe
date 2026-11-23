#include "commandconsole.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
const QColor kPanel(75, 70, 50);
const QColor kGrid(92, 86, 63);
const QColor kBar(226, 191, 62);
}

CommandConsole::CommandConsole(QWidget *parent)
    : QFrame(parent)
    , m_log(new QTextEdit(this))
    , m_input(new QLineEdit(this))
    , m_completer(new QCompleter(this))
{
    setMinimumHeight(170);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(17);

    m_log->setReadOnly(true);
    m_log->setFont(mono);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->viewport()->setAutoFillBackground(false);
    m_log->document()->setMaximumBlockCount(400);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setStyleSheet(
        "QTextEdit { background: transparent; color: #e8e2c8; }"
        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #8a8160; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    QFont inputFont = mono;
    inputFont.setPixelSize(19);
    inputFont.setBold(true);
    m_input->setFont(inputFont);
    m_input->setFixedHeight(34);
    m_input->setFrame(false);
    m_input->setPlaceholderText(tr("Type a command — 'help' for the list"));
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

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(56, 22, 110, 12);
    layout->setSpacing(18);
    layout->addWidget(m_log, 1);
    layout->addWidget(m_input);

    connect(m_input, &QLineEdit::returnPressed, this, &CommandConsole::submit);
}

void CommandConsole::appendLine(const QString &text, Tone tone)
{
    QString color;
    QString prefix = QStringLiteral("-");
    switch (tone) {
    case Tone::Echo:     color = "#a9a28a"; prefix = QStringLiteral("&gt; "); break;
    case Tone::Info:     color = "#e8e2c8"; break;
    case Tone::Success:  color = "#8be07a"; break;
    case Tone::Warning:  color = "#f2c94c"; break;
    case Tone::Critical: color = "#ff5a48"; break;
    }
    m_log->append(QStringLiteral("<span style=\"color:%1\">%2%3</span>")
                      .arg(color, prefix, text.toHtmlEscaped()));
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void CommandConsole::clearLog()
{
    m_log->clear();
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
    emit commandEntered(command);
}

bool CommandConsole::eventFilter(QObject *watched, QEvent *event)
{
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
