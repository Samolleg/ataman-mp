QT += core gui widgets testlib concurrent
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = backup_manager_tests

INCLUDEPATH += ../source/documents ../source/core ../source/resources ../source/app
SOURCES += \
    backup_manager_tests.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/resources/res_archive_index.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/documents/backup_manager.h \
    ../source/core/log.h
