#ifndef CONQUESTENDSCREEN_H
#define CONQUESTENDSCREEN_H

#include <QStringList>
#include <QWidget>

class QLabel;

// ================================================================
//  ConquestEndScreen
//  Shown over the conquest screen when the game ends: VICTORY or
//  DEFEAT, a short summary of the game and the way out (title
//  screen, quit). The galaxy can still be looked at behind it.
// ================================================================

class ConquestEndScreen : public QWidget
{
    Q_OBJECT

public:
    explicit ConquestEndScreen(QWidget *parent = nullptr);

    void showResult(bool victory, const QString &faction, const QStringList &summary);

signals:
    void titleRequested();
    void quitRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLabel *m_heading = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_summary = nullptr;
    bool m_victory = true;
};

#endif // CONQUESTENDSCREEN_H
