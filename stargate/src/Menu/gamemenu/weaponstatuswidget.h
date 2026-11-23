#ifndef WEAPONSTATUSWIDGET_H
#define WEAPONSTATUSWIDGET_H

#include <QList>
#include <QWidget>
#include <functional>

#include "src/render/battleview.h"

class QTimer;

// ================================================================
//  WeaponStatusWidget
//  Live weapon panel of the game menu (replaces weaponStatus.png),
//  same style as the route map and the ship status:
//   - cooldowns of the main tower, pulse volley and tower burst,
//     with the command that fires them
//   - health of the weapon components, "HIT" / "OFF"
//   - locked target and its hull, hostiles and kills
// ================================================================

struct WeaponStatusData
{
    struct Weapon {
        QString name;
        int hp = 0;
        int maxHp = 1;
        bool underAttack = false;
    };

    WeaponReadiness readiness;
    QList<Weapon> weapons;
};

class WeaponStatusWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WeaponStatusWidget(QWidget *parent = nullptr);

    void setProvider(std::function<WeaponStatusData()> provider);

    QSize sizeHint() const override { return {501, 274}; }
    QSize minimumSizeHint() const override { return {200, 140}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::function<WeaponStatusData()> m_provider;
    WeaponStatusData m_data;
    QTimer *m_timer = nullptr;
    qint64 m_frame = 0;
};

#endif // WEAPONSTATUSWIDGET_H
