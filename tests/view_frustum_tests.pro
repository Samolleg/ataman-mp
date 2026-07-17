QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = view_frustum_tests

INCLUDEPATH += ../source/render
SOURCES += view_frustum_tests.cpp
HEADERS += ../source/render/view_frustum.h
