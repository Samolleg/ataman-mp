QT += core gui opengl
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = selection_render_tests
INCLUDEPATH += ../source/core ../source/render
SOURCES += selection_render_tests.cpp \
    ../source/render/part.cpp \
    ../source/render/render_queue.cpp \
    ../source/render/render_performance.cpp \
    ../source/core/log.cpp
HEADERS += ../source/core/log.h
win32:LIBS += -lopengl32
