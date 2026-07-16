QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = backup_manager_tests

INCLUDEPATH += ..
SOURCES += \
    backup_manager_tests.cpp \
    ../backup_manager.cpp \
    ../log.cpp

HEADERS += \
    ../backup_manager.h \
    ../log.h
