#include "mainwindow.h"
#include "src/Menu/setting/settingspanel.h"
#include "src/core/gameconfig.h"
#include "src/core/modloader.h"
#include "src/core/language.h"
#include "src/db/databasemanager.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    // The 3D battle view needs an OpenGL 3.3 core context (macOS: 4.1 core).
    // Must be set before the QApplication is created.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);

    // Interface language: saved choice, else the system language
    Language::apply(Language::current());

    // Difficulty, game speed, accessibility: config.json next to the saves
    GameConfig::instance().load();
    // Mods: JSON files in the "mods" folder next to the saves
    ModLoader::instance().load();
    GameConfig::instance().applyOverrides(ModLoader::instance().config());

    // Player save database (copied from the template on first launch).
    // The game still runs without it, with default values.
    if (!DatabaseManager::instance().openRuntimeCopy(":/db/stargate.db", "stargate.db"))
        qWarning() << "[DB] Database unavailable, running without persistence";

    MainWindow w;
    if (SettingsPanel::fullScreenSetting())
        w.showFullScreen();
    else
        w.showMaximized();
    return a.exec();
}
