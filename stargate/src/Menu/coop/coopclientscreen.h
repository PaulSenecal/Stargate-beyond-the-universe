#ifndef COOPCLIENTSCREEN_H
#define COOPCLIENTSCREEN_H

#include <QWidget>

class CommandConsole;
class CoopClient;
class QLabel;
class QPushButton;

// ================================================================
//  CoopClientScreen
//  The co-pilot's screen: the status of the host's ship (refreshed
//  every second) and a console. Commands go to the host's game, which
//  accepts those of the co-pilot's station (tactical or engineering).
// ================================================================

class CoopClientScreen : public QWidget
{
    Q_OBJECT

public:
    CoopClientScreen(const QString &host, quint16 port, QWidget *parent = nullptr);

signals:
    void quitToTitleRequested();

private:
    CoopClient *m_client = nullptr;
    CommandConsole *m_console = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_header = nullptr;
    QPushButton *m_back = nullptr;
};

#endif // COOPCLIENTSCREEN_H
