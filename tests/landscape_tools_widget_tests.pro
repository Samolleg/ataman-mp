QT += core gui widgets opengl testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = landscape_tools_widget_tests

INCLUDEPATH += ../source/app ../source/core ../source/documents ../source/ui

SOURCES += \
    landscape_tools_widget_tests.cpp \
    ../source/ui/landscape_tools.cpp
SOURCES += ../source/ui/ui_theme.cpp

HEADERS += \
    ../source/app/loaded_map_panel_policy.h \
    ../source/core/landscape_edit_policy.h \
    ../source/documents/terrain_edit_model.h \
    ../source/ui/landscape_tools.h
