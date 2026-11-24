QT       += core gui sql network
QT += widgets opengl openglwidgets multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# Game version: the last vX.Y.Z tag (set by the release workflow on
# master), plus the commits since then; "dev" outside a git checkout.
# Re-run qmake to refresh it.
APP_VERSION = $$system(git -C $$shell_quote($$PWD) describe --tags --always --match "v*" 2>/dev/null)
isEmpty(APP_VERSION): APP_VERSION = dev
DEFINES += APP_VERSION=\\\"$$APP_VERSION\\\"

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    src/Menu/campaign/campaign.cpp \
    src/Menu/gamemenu/commandconsole.cpp \
    src/Menu/gamemenu/gamemenu.cpp \
    src/Menu/gamemenu/hudimagepanel.cpp \
    src/Menu/gamemenu/radarwidget.cpp \
    src/Menu/gamemenu/routemapwidget.cpp \
    src/Menu/gamemenu/shiplibrarydialog.cpp \
    src/Menu/gamemenu/shipstatuswidget.cpp \
    src/Menu/gamemenu/weaponstatuswidget.cpp \
    src/Menu/mainmenu/titlescreen.cpp \
    src/Menu/pause/pausemenu.cpp \
    src/Menu/setting/settingspanel.cpp \
    src/audio/musicplayer.cpp \
    src/conquest/commandpanel.cpp \
    src/conquest/conquestendscreen.cpp \
    src/conquest/conquestgame.cpp \
    src/conquest/conquestscreen.cpp \
    src/conquest/galaxymapwidget.cpp \
    src/audio/soundsystem.cpp \
    src/core/commandmanager.cpp \
    src/core/language.cpp \
    src/core/destinystate.cpp \
    src/core/questsystem.cpp \
    src/db/databasemanager.cpp \
    src/db/repositories/characterrepository.cpp \
    src/db/repositories/componentrepository.cpp \
    src/db/repositories/questrepository.cpp \
    src/db/repositories/resourcerepository.cpp \
    src/db/repositories/saverepository.cpp \
    src/db/repositories/shiplibraryrepository.cpp \
    src/db/repositories/shiprepository.cpp \
    src/db/repositories/worldrepository.cpp \
    src/db/repositories/zonerepository.cpp \
    src/entities/entity.cpp \
    src/entities/vessel.cpp \
    src/render/battleview.cpp \
    src/render/meshbuilder.cpp \
    src/render/planetlook.cpp \
    src/utils/logger.cpp \
    src/world/destinyroute.cpp \
    src/world/eventsystem.cpp \
    src/world/planetgenerator.cpp

HEADERS += \
    mainwindow.h \
    src/Menu/campaign/campaign.h \
    src/Menu/gamemenu/commandconsole.h \
    src/Menu/gamemenu/gamemenu.h \
    src/Menu/gamemenu/hudimagepanel.h \
    src/Menu/gamemenu/radarwidget.h \
    src/Menu/gamemenu/routemapwidget.h \
    src/Menu/gamemenu/shiplibrarydialog.h \
    src/Menu/gamemenu/shipstatuswidget.h \
    src/Menu/gamemenu/weaponstatuswidget.h \
    src/Menu/mainmenu/titlescreen.h \
    src/Menu/menustyle.h \
    src/Menu/pause/pausemenu.h \
    src/Menu/setting/settingspanel.h \
    src/audio/musicplayer.h \
    src/conquest/commandpanel.h \
    src/conquest/conquestendscreen.h \
    src/conquest/conquestgame.h \
    src/conquest/conquestscreen.h \
    src/conquest/galaxymapwidget.h \
    src/audio/soundcue.h \
    src/audio/soundsystem.h \
    src/core/commandmanager.h \
    src/core/language.h \
    src/core/destinystate.h \
    src/core/questsystem.h \
    src/core/assets.h \
    src/core/shipclass.h \
    src/db/databasemanager.h \
    src/db/dbtext.h \
    src/db/dbtexts_i18n.h \
    src/db/repositories/characterrepository.h \
    src/db/repositories/componentrepository.h \
    src/db/repositories/questrepository.h \
    src/db/repositories/resourcerepository.h \
    src/db/repositories/saverepository.h \
    src/db/repositories/shiplibraryrepository.h \
    src/db/repositories/shiprepository.h \
    src/db/repositories/worldrepository.h \
    src/db/repositories/zonerepository.h \
    src/entities/entity.h \
    src/entities/vessel.h \
    src/render/battleview.h \
    src/render/meshbuilder.h \
    src/render/planetlook.h \
    src/utils/logger.h \
    src/world/destinyroute.h \
    src/world/eventsystem.h \
    src/world/planet.h \
    src/world/planetgenerator.h

FORMS += \
    mainwindow.ui \
    src/Menu/campaign/campaign.ui

TRANSLATIONS += \
    stargate_fr.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Optional local assets of the Stargate Reborn mod (git-ignored, personal
# use): pictures, sounds… The game works without them. Can be overridden
# at run time with the STARGATE_ASSETS environment variable.
DEFINES += STARGATE_ASSETS_DIR=\\\"$$PWD/src/autre\\\"

RESOURCES += \
    ../rcs/ressources.qrc
