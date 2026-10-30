QT += core gui opengl testlib
CONFIG += testcase console c++11
TARGET = figure_document_tests
INCLUDEPATH += ../source/core ../source/render ../source/ui
SOURCES += figure_document_tests.cpp \
    ../source/render/figure.cpp ../source/render/part.cpp \
    ../source/render/render_queue.cpp ../source/render/render_performance.cpp \
    ../source/core/log.cpp
HEADERS += ../source/core/log.h
win32:LIBS += -lopengl32
