QT += core testlib
QT -= gui
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = terrain_shadow_inputs_tests
INCLUDEPATH += $$PWD/../source/render
SOURCES += $$PWD/terrain_shadow_inputs_tests.cpp
