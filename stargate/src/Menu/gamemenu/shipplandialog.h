#ifndef SHIPPLANDIALOG_H
#define SHIPPLANDIALOG_H

#include <QDialog>
#include <QHash>
#include <QPointF>
#include <functional>

class Interior;
class QLabel;
class QTimer;

// ================================================================
//  ShipPlanView
//  Zoomable plan of the Destiny's compartments, seen from above:
//  air (blue), fire (orange), breaches (red outline), leaking power
//  conduits (yellow), sealed doors (thick frame), vented rooms, closed
//  sections (hatched), boarders, the crew (initials) and the repair
//  jobs (progress bars). Wheel = zoom, drag = move, click = details.
// ================================================================

class ShipPlanView : public QWidget
{
    Q_OBJECT

public:
    explicit ShipPlanView(const Interior *interior, QWidget *parent = nullptr);
    // Health (percent) of the ship systems hosted in the compartments
    void setSystemHealth(const QHash<QString, float> &health) { m_health = health; }
    int selected() const { return m_selected; }

signals:
    void compartmentClicked(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QTransform transform() const;

    const Interior *m_interior = nullptr;
    QHash<QString, float> m_health;
    double m_zoom = 1.0;
    QPointF m_pan;
    QPointF m_pressPos, m_lastPos;
    bool m_dragged = false;
    int m_selected = -1;
    int m_frame = 0;
};

class ShipPlanDialog : public QDialog
{
    Q_OBJECT

public:
    using HealthProvider = std::function<QHash<QString, float>()>;
    ShipPlanDialog(const Interior *interior, HealthProvider health, QWidget *parent = nullptr);

    // Details of a compartment, also used by the console
    static QStringList describe(const Interior &interior, int index);

private:
    void refresh();

    const Interior *m_interior = nullptr;
    HealthProvider m_healthProvider;
    ShipPlanView *m_view = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_summary = nullptr;
    QTimer *m_timer = nullptr;
};

#endif // SHIPPLANDIALOG_H
