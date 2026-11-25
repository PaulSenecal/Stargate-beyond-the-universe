# Unit tests for the game logic (no UI).
# Open this file in Qt Creator and run it, or from a terminal:
#   qmake tests.pro && make && ./stargate_tests

QT += core sql testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = stargate_tests

GAME = $$PWD/../stargate
INCLUDEPATH += $$GAME

# Absolute paths to the data files used by the tests
DEFINES += COMMANDS_JSON=\\\"$$PWD/../rcs/commands.json\\\" \
           TEMPLATE_DB=\\\"$$GAME/src/db/stagateDataBase.db\\\"

SOURCES += \
    tst_game.cpp \
    $$GAME/src/conquest/conquestgame.cpp \
    $$GAME/src/core/commandmanager.cpp \
    $$GAME/src/core/destinystate.cpp \
    $$GAME/src/core/gameconfig.cpp \
    $$GAME/src/core/campaignlife.cpp \
    $$GAME/src/core/interior.cpp \
    $$GAME/src/core/questsystem.cpp \
    $$GAME/src/db/databasemanager.cpp \
    $$GAME/src/db/repositories/componentrepository.cpp \
    $$GAME/src/db/repositories/questrepository.cpp \
    $$GAME/src/db/repositories/resourcerepository.cpp \
    $$GAME/src/db/repositories/saverepository.cpp \
    $$GAME/src/db/repositories/shiplibraryrepository.cpp \
    $$GAME/src/db/repositories/shiprepository.cpp \
    $$GAME/src/db/repositories/worldrepository.cpp \
    $$GAME/src/world/destinyroute.cpp \
    $$GAME/src/world/eventsystem.cpp \
    $$GAME/src/world/planetgenerator.cpp

HEADERS += \
    $$GAME/src/conquest/conquestgame.h \
    $$GAME/src/core/commandmanager.h \
    $$GAME/src/core/questsystem.h \
    $$GAME/src/world/destinyroute.h
