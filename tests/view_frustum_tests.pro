QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = view_frustum_tests

INCLUDEPATH += ..
SOURCES += view_frustum_tests.cpp
HEADERS += ../view_frustum.h
