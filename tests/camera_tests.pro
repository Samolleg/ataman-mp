QT += core gui widgets testlib

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = camera_tests
INCLUDEPATH += ../source/render ../source/ui

SOURCES += camera_tests.cpp \
           ../source/ui/camera_telephoto_controls.cpp

HEADERS += ../source/render/camera_orbit.h \
           ../source/render/camera_telephoto.h \
           ../source/render/shadow_cascade.h \
           ../source/ui/camera_telephoto_controls.h
