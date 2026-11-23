#ifndef HUDIMAGEPANEL_H
#define HUDIMAGEPANEL_H

#include <QPixmap>
#include <QWidget>

// ================================================================
//  HudImagePanel
//  Displays one HUD screen image (map, navigation, ship status…)
//  scaled to the available space while keeping its aspect ratio.
// ================================================================

class HudImagePanel : public QWidget
{
    Q_OBJECT

public:
    explicit HudImagePanel(const QString &imagePath, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_pixmap;
};

#endif // HUDIMAGEPANEL_H
