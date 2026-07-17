QT += core testlib
QT -= gui

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = duplicate_id_report_tests
INCLUDEPATH += ../source/documents

SOURCES += \
    duplicate_id_report_tests.cpp \
    ../source/documents/duplicate_id_report.cpp

HEADERS += \
    ../source/documents/duplicate_id_report.h
