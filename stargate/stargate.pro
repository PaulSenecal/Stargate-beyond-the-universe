QT       += core gui sql network
QT += widgets opengl openglwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

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
    src/core/commandmanager.cpp \
    src/core/destinystate.cpp \
    src/db/databasemanager.cpp \
    src/db/repositories/characterrepository.cpp \
    src/db/repositories/componentrepository.cpp \
    src/db/repositories/resourcerepository.cpp \
    src/db/repositories/saverepository.cpp \
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
    src/core/commandmanager.h \
    src/core/destinystate.h \
    src/db/databasemanager.h \
    src/db/repositories/characterrepository.h \
    src/db/repositories/componentrepository.h \
    src/db/repositories/resourcerepository.h \
    src/db/repositories/saverepository.h \
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
    stargate_fr_FR.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    ../rcs/ressources.qrc
