QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = settings_persistence_tests

INCLUDEPATH += ../source/app
SOURCES += settings_persistence_tests.cpp \
           ../source/app/settings_persistence.cpp
HEADERS += ../source/app/settings_persistence.h
