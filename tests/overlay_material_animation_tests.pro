QT += core gui testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = overlay_material_animation_tests

INCLUDEPATH += ../source ../source/core ../source/render

SOURCES += overlay_material_animation_tests.cpp \
    ../source/render/overlay_material_animation.cpp

HEADERS += ../source/render/overlay_material_animation.h \
    ../source/core/types.h
