QT += core gui opengl testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = neighbor_performance_tests
INCLUDEPATH += ../source/core ../source/render
SOURCES += neighbor_performance_tests.cpp ../source/render/prepared_static_visual.cpp
