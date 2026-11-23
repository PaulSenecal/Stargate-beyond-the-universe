#ifndef RADARWIDGET_H
#define RADARWIDGET_H

#include <QList>
#include <QPointF>
#include <QWidget>
#include <functional>

class QTimer;

// ================================================================
//  RadarWidget
//  Tactical radar of the game menu (replaces navigation.png), same
//  style as the other live panels. Top-down view centred on the
//  Destiny, bow pointing up: hostile contacts (T1, T2… — larger for
//  motherships), locked target, incoming (orange) and outgoing (blue)
//  fire, range rings and a rotating sweep.
//  Positions are ship-local: x = forward, y = starboard.
// ================================================================

struct RadarPicture
{
    struct Contact {
        QPointF local;
        int index = 0;          // 0-based, shown as T1…
        bool mothership = false;
        bool locked = false;
        float health = 100;     // percent
    };
    struct Bolt {
        QPointF local;
        bool fromDestiny = false;
    };

    QList<Contact> contacts;
    QList<Bolt> bolts;
    bool shieldUp = true;
};

class RadarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RadarWidget(QWidget *parent = nullptr);

    void setProvider(std::function<RadarPicture()> provider);

    QSize sizeHint() const override { return {526, 285}; }
    QSize minimumSizeHint() const override { return {200, 140}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::function<RadarPicture()> m_provider;
    RadarPicture m_picture;
    QTimer *m_timer = nullptr;
    qint64 m_frame = 0;
};

#endif // RADARWIDGET_H
