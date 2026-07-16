QT += core testlib
QT -= gui

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = resource_name_tests

SOURCES += \
    resource_name_tests.cpp \
    ../resource_name.cpp \
    ../unit_appearance.cpp

HEADERS += \
    ../resource_name.h \
    ../unit_appearance.h
