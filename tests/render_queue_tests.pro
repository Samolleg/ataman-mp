QT += core gui opengl testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = render_queue_tests

INCLUDEPATH += ../source ../source/core ../source/render

SOURCES += render_queue_tests.cpp \
    ../source/render/render_material.cpp \
    ../source/render/render_environment.cpp

HEADERS += ../source/render/render_material.h \
    ../source/render/render_queue.h \
    ../source/render/render_scheduling.h \
    ../source/render/render_environment.h
