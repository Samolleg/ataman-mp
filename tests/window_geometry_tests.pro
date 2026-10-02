QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = window_geometry_tests
INCLUDEPATH += ../source/ui
SOURCES += window_geometry_tests.cpp ../source/ui/window_geometry.cpp
HEADERS += ../source/ui/window_geometry.h
