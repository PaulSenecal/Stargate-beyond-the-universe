#ifndef REPLAYDIALOG_H
#define REPLAYDIALOG_H

#include <QDialog>
#include <QList>

#include "src/render/battleview.h"

class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QListWidget;

// ================================================================
//  ReplayDialog
//  Replay of the last battle, seen from above like a big radar: your
//  ship, the enemies (red) and the friendly ships (blue), every shot,
//  beams as lines; hull and shield bars; the log of each moment.
//  Play / pause, speed x1 x2 x4, and a timeline to jump anywhere.
// ================================================================

class ReplayView : public QWidget
{
    Q_OBJECT

public:
    explicit ReplayView(QWidget *parent = nullptr);
    void setFrame(const ReplayFrame *frame) { m_frame = frame; update(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const ReplayFrame *m_frame = nullptr;
};

class ReplayDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReplayDialog(QWidget *parent = nullptr);
    void setReplay(const QList<ReplayFrame> &frames, const QString &faction);

private:
    void showFrame(int index);
    void togglePlay();

    QList<ReplayFrame> m_frames;
    ReplayView *m_view = nullptr;
    QSlider *m_timeline = nullptr;
    QPushButton *m_play = nullptr;
    QPushButton *m_speed = nullptr;
    QLabel *m_time = nullptr;
    QListWidget *m_log = nullptr;
    QTimer *m_timer = nullptr;
    int m_rate = 1;
};

#endif // REPLAYDIALOG_H
