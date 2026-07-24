QT += core gui widgets testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = res_archive_document_tests

INCLUDEPATH += ../source/documents ../source/core ../source/app

SOURCES += \
    res_archive_document_tests.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/map_table_document.cpp \
    ../source/documents/synthetic_map_table_archive.cpp \
    ../source/documents/res_save_coordinator.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/documents/res_archive_document.h \
    ../source/documents/map_table_document.h \
    ../source/documents/synthetic_map_table_archive.h \
    ../source/documents/res_save_coordinator.h \
    ../source/documents/backup_manager.h \
    ../source/core/log.h
