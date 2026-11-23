#ifndef ROUTEMAPWIDGET_H
#define ROUTEMAPWIDGET_H

#include <QColor>
#include <QList>
#include <QWidget>

// ================================================================
//  RouteMapWidget
//  Live map of the game menu (replaces the static maps.png):
//   - header: galaxy, gate window, legend
//   - route strip: stops of the current galaxy (past, current, next)
//   - current system: star, orbits and planets. Green ring = gate
//     available, red ring = gate closed or planet already explored,
//     yellow marker = team on the planet
//   - red triangles: hostile ships in the sector
// ================================================================

struct RouteMapData
{
    struct PlanetMark {
        QString letter;         // A, B, C
        QString address;        // G000003A
        QString nickname;
        QString classification;
        QColor tint;
        bool explored = false;
        bool team = false;
    };

    QString galaxyCode;         // G01
    QString galaxyName;
    QList<int> stops;           // sequence numbers of the galaxy's stops
    int currentSequence = 0;
    QList<PlanetMark> planets;
    bool gateOpen = false;
    double gateRemaining = 0;
    int hostiles = 0;
};

class RouteMapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RouteMapWidget(QWidget *parent = nullptr);

    void setData(const RouteMapData &data);

    QSize sizeHint() const override { return {470, 317}; }
    QSize minimumSizeHint() const override { return {200, 140}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    RouteMapData m_data;
};

#endif // ROUTEMAPWIDGET_H
