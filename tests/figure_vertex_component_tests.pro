QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = figure_vertex_component_tests
INCLUDEPATH += ../source/core ../source/render
SOURCES += figure_vertex_component_tests.cpp
HEADERS += ../source/render/figure_vertex_component.h
