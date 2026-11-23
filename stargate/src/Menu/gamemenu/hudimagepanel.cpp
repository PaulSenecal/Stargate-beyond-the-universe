#include "hudimagepanel.h"

#include <QDebug>
#include <QPainter>

HudImagePanel::HudImagePanel(const QString &imagePath, QWidget *parent)
    : QWidget(parent)
    , m_pixmap(imagePath)
{
    if (m_pixmap.isNull())
        qWarning() << "[HUD] Missing image:" << imagePath;
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize HudImagePanel::sizeHint() const
{
    return m_pixmap.isNull() ? QSize(400, 240) : m_pixmap.size();
}

QSize HudImagePanel::minimumSizeHint() const
{
    return {120, 60};
}

void HudImagePanel::paintEvent(QPaintEvent *)
{
    if (m_pixmap.isNull())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QRect target(QPoint(0, 0), m_pixmap.size().scaled(size(), Qt::KeepAspectRatio));
    target.moveCenter(rect().center());
    p.drawPixmap(target, m_pixmap);
}
