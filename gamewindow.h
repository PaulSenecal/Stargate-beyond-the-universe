#ifndef GAMEWINDOW_H
#define GAMEWINDOW_H

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include "core/Vessel.h"

class GameWindow : public QWidget {
    Q_OBJECT

public:
    GameWindow(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void gameLoop();

private:
    // Game objects
    std::unique_ptr<Vessel> playerVessel;

    // Time management
    QTimer *timer;
    QElapsedTimer elapsedTimer;
    qint64 lastFrameTime;

    void drawVessel(QPainter& painter);
    void drawUI(QPainter& painter);
};

#endif
