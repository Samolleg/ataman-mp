QT += core testlib

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = frame_presentation_telemetry_tests
INCLUDEPATH += ../source/render

SOURCES += frame_presentation_telemetry_tests.cpp \
           ../source/render/frame_presentation_telemetry.cpp

HEADERS += ../source/render/frame_presentation_telemetry.h
