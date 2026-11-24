#ifndef GALAXYMAPWIDGET_H
#define GALAXYMAPWIDGET_H

#include <QWidget>

class ConquestGame;

// ================================================================
//  GalaxyMapWidget
//  Map of the conquest galaxy: hyperspace lanes, planets in the color
//  of their owner (number, name, defense, capital crown), the flagship
//  and its route. Click: select a planet. Double click: travel there.
// ================================================================

class GalaxyMapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GalaxyMapWidget(const ConquestGame *game, QWidget *parent = nullptr);

    int selected() const { return m_selected; }
    void select(int planetId);

signals:
    void planetSelected(int planetId);
    void planetActivated(int planetId);         // double click

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QPointF toScreen(const QPointF &map) const;
    int planetAt(const QPointF &pos) const;
    double scale() const;

    const ConquestGame *m_game = nullptr;
    int m_selected = -1;
};

#endif // GALAXYMAPWIDGET_H
