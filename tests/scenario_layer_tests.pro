QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = scenario_layer_tests
INCLUDEPATH += ../source/documents
SOURCES += scenario_layer_tests.cpp ../source/documents/scenario_analysis.cpp
HEADERS += ../source/documents/scenario_analysis.h
