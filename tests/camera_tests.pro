QT += core gui testlib

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = camera_tests
INCLUDEPATH += ../source/render

SOURCES += camera_tests.cpp

HEADERS += ../source/render/camera_orbit.h \
           ../source/render/camera_telephoto.h \
           ../source/render/shadow_cascade.h
