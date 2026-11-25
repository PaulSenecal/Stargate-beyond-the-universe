#ifndef SHIPLIBRARYDIALOG_H
#define SHIPLIBRARYDIALOG_H

#include <QDialog>
#include <QList>
#include <QStringList>

#include "src/core/shipclass.h"

class QListWidget;
class ShipCardWidget;

// ================================================================
//  ShipLibraryDialog
//  Ship library opened with the 'library' command: every known ship
//  grouped by faction (left) and the card of the selected one
//  (right): hull, role, description and each attack with its damage,
//  chance of use, frequency and trigger. Special attacks stand out.
// ================================================================

class ShipLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShipLibraryDialog(QWidget *parent = nullptr);

    void setShips(const QList<ShipClass> &ships);
    // Selects a ship by (partial) name; false if none matches
    bool showShip(const QString &name);

    // Every ship the game knows (database + mods; the built-in ones without database)
    static QList<ShipClass> loadShips();
    // Opens the library over `parent` (title screen, pause menu…)
    static ShipLibraryDialog *open(QWidget *parent, const QString &ship = QString());

    // Index of the ship matching a typed name ("dart", "hatak"…), -1 if none
    static int find(const QList<ShipClass> &ships, const QString &name);
    // Text version of a card, for the console
    static QStringList describe(const ShipClass &ship);
    static QString describe(const ShipAttack &attack);

private:
    void select(int shipIndex);

    QList<ShipClass> m_ships;
    QListWidget *m_list = nullptr;
    ShipCardWidget *m_card = nullptr;
};

#endif // SHIPLIBRARYDIALOG_H
