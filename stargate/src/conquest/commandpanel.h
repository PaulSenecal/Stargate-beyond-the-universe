#ifndef COMMANDPANEL_H
#define COMMANDPANEL_H

#include <QList>
#include <QRectF>
#include <QWidget>

class ConquestGame;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

// ================================================================
//  TechTreeWidget
//  The technology tree drawn as 3 columns (weapons, defense,
//  industry) of 3 tiers linked by arrows. Colors: done (green),
//  in progress (gold, with its progress), available (click to
//  research), locked (grey, what it needs).
// ================================================================

class TechTreeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TechTreeWidget(ConquestGame *game, QWidget *parent = nullptr);

signals:
    void status(const QString &text, bool ok);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QRectF boxOf(int techIndex) const;
    int techAt(const QPointF &pos) const;

    ConquestGame *m_game = nullptr;
    int m_hover = -1;
};

// ================================================================
//  CommandPanel
//  Conquest command menu over the game (TECH button, 'research',
//  'diplomacy'): the technology tree and the diplomacy cards of the
//  other factions. The game goes on behind it; Escape closes it.
// ================================================================

class CommandPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CommandPanel(ConquestGame *game, QWidget *parent = nullptr);

    enum class Page { Research, Diplomacy };
    void open(Page page);
    void refresh();                 // called by the conquest screen every tick

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showPage(Page page);
    void rebuildDiplomacy();
    void setStatus(const QString &text, bool ok);

    ConquestGame *m_game = nullptr;
    QStackedWidget *m_pages = nullptr;
    QPushButton *m_researchTab = nullptr;
    QPushButton *m_diplomacyTab = nullptr;
    QLabel *m_stock = nullptr;
    QLabel *m_status = nullptr;
    TechTreeWidget *m_tree = nullptr;
    QWidget *m_diplomacy = nullptr;
    QVBoxLayout *m_cards = nullptr;
    QString m_diplomacyKey;         // relations shown, to rebuild only on change
};

#endif // COMMANDPANEL_H
