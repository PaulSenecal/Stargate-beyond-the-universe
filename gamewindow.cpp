#include "GameWindow.h"
#include <QPainter>
#include <QMouseEvent>

GameWindow::GameWindow(QWidget *parent) : QWidget(parent) {
    // Fenêtre
    setWindowTitle("Game Mecha - v0.0.1");
    setFixedSize(800, 600);
    setMouseTracking(true);

    // Fond noir spatial
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);

    // Créer le vaisseau au centre
    playerVessel = std::make_unique<Vessel>(400, 300);

    // Timer pour game loop (60 FPS)
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &GameWindow::gameLoop);
    timer->start(16); // ~60 FPS

    elapsedTimer.start();
    lastFrameTime = 0;
}

void GameWindow::gameLoop() {
    // Calculer deltaTime
    qint64 currentTime = elapsedTimer.elapsed();
    double deltaTime = (currentTime - lastFrameTime) / 1000.0;
    lastFrameTime = currentTime;

    // Update
    playerVessel->update(deltaTime);

    // Forcer le redessin
    update();
}

void GameWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Dessiner le vaisseau
    drawVessel(painter);

    // Dessiner l'UI
    drawUI(painter);
}

void GameWindow::drawVessel(QPainter& painter) {
    // Dessiner un triangle pour le vaisseau
    painter.save();

    QPointF pos = playerVessel->position;
    painter.translate(pos);

    // Couleur selon l'énergie
    int greenLevel = (playerVessel->energy / playerVessel->maxEnergy) * 255;
    painter.setBrush(QColor(255 - greenLevel, greenLevel, 0));
    painter.setPen(QPen(Qt::white, 2));

    // Triangle pointant vers le haut
    QPolygonF triangle;
    triangle << QPointF(0, -15)     // Pointe
             << QPointF(-10, 10)     // Base gauche
             << QPointF(10, 10);     // Base droite

    painter.drawPolygon(triangle);

    // Petit réacteur si en mouvement
    if (playerVessel->isMoving) {
        painter.setPen(QPen(Qt::cyan, 3));
        painter.drawLine(QPointF(-5, 10), QPointF(-5, 15));
        painter.drawLine(QPointF(5, 10), QPointF(5, 15));
    }

    painter.restore();
}

void GameWindow::drawUI(QPainter& painter) {
    // Barre d'énergie
    painter.setPen(QPen(Qt::white, 2));
    painter.drawText(10, 20, "Energy:");

    // Barre de fond
    painter.setBrush(Qt::darkGray);
    painter.drawRect(70, 10, 200, 15);

    // Barre d'énergie actuelle
    float energyRatio = playerVessel->energy / playerVessel->maxEnergy;
    int barWidth = 200 * energyRatio;

    QColor energyColor = energyRatio > 0.3f ? Qt::green : Qt::red;
    painter.setBrush(energyColor);
    painter.drawRect(70, 10, barWidth, 15);

    // Texte énergie
    QString energyText = QString("%1 / %2")
                             .arg((int)playerVessel->energy)
                             .arg((int)playerVessel->maxEnergy);
    painter.setPen(Qt::white);
    painter.drawText(280, 22, energyText);

    // Position debug
    painter.drawText(10, 580, QString("Pos: %1, %2")
                                  .arg((int)playerVessel->position.x())
                                  .arg((int)playerVessel->position.y()));
}

void GameWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Déplacer le vaisseau vers le clic
        playerVessel->moveTo(event->pos());
    }
}

void GameWindow::resizeEvent(QResizeEvent *) {
    // Garder le vaisseau au centre si on resize
    playerVessel->position = QPointF(width()/2, height()/2);
}
