QT += core testlib
QT -= gui

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = duplicate_id_report_tests

SOURCES += \
    duplicate_id_report_tests.cpp \
    ../duplicate_id_report.cpp

HEADERS += \
    ../duplicate_id_report.h
