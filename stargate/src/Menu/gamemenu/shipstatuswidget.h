#ifndef SHIPSTATUSWIDGET_H
#define SHIPSTATUSWIDGET_H

#include <QList>
#include <QPointF>
#include <QImage>
#include <QWidget>
#include <functional>

class QTimer;

// ================================================================
//  ShipStatusWidget
//  Live damage panel of the game menu (replaces shipStatus.png),
//  same style as the route map:
//   - header: hull and shield, legend
//   - top-down schematic of the Destiny (spire + rear fan, as in the
//     3D view) with one dot per installed component, coloured by
//     health; impacts as red rings, shield hits as blue flashes,
//     fires in orange; a component just hit pulses
//   - list of the components with health bars, "HIT" when struck
//  Positions are in the ship's local plane (x forward, z sideways).
// ================================================================

struct ShipStatusData
{
    struct Component {
        QString name;
        int hp = 0;
        int maxHp = 1;
        bool underAttack = false;   // its system was hit recently
    };
    struct Hit {
        QPointF local;              // (x, z)
        float age = 0;              // seconds
        bool shield = false;
    };

    float hull = 100;
    float shield = 100;
    bool shieldUp = true;
    QList<Component> components;
    QList<QPointF> fires;           // (x, z)
    QList<Hit> hits;
    // Schematic of the ship (from its 3D model); empty = the Destiny drawing
    QImage silhouette;
    QRectF silhouetteRect;          // (x, z) area of the silhouette
    QPointF shieldCenter;
    QSizeF shieldRadii;
};

class ShipStatusWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ShipStatusWidget(QWidget *parent = nullptr);

    // Pulled ~10 times per second while visible (animations)
    void setProvider(std::function<ShipStatusData()> provider);

    QSize sizeHint() const override { return {516, 281}; }
    QSize minimumSizeHint() const override { return {200, 140}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::function<ShipStatusData()> m_provider;
    ShipStatusData m_data;
    QTimer *m_timer = nullptr;
    qint64 m_frame = 0;
};

#endif // SHIPSTATUSWIDGET_H
