QT += core testlib
QT -= gui
CONFIG += console testcase c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = map_spawn_planner_tests

INCLUDEPATH += ../source/resources ../source/runtime
SOURCES += \
    map_spawn_planner_tests.cpp \
    ../source/runtime/map_spawn_planner.cpp
HEADERS += \
    ../source/runtime/map_spawn_planner.h \
    ../source/resources/map_catalog.h
