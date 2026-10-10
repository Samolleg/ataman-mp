QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = backup_manager_tests
include(save_test_backup_root.pri)

INCLUDEPATH += ../source/documents ../source/core
SOURCES += \
    backup_manager_tests.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/documents/backup_manager.h \
    ../source/core/log.h
