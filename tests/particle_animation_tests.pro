QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = particle_animation_tests

INCLUDEPATH += ../source/render
SOURCES += particle_animation_tests.cpp
HEADERS += ../source/render/particle_animation.h
