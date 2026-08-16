QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = landscape_tools_widget_tests

INCLUDEPATH += ../source/core ../source/documents ../source/ui

SOURCES += \
    landscape_tools_widget_tests.cpp \
    ../source/ui/landscape_tools.cpp

HEADERS += \
    ../source/core/landscape_edit_policy.h \
    ../source/documents/terrain_edit_model.h \
    ../source/ui/landscape_tools.h
