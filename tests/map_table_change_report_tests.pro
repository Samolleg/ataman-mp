QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = map_table_change_report_tests
INCLUDEPATH += ../source/documents
SOURCES += map_table_change_report_tests.cpp \
    ../source/documents/map_table_change_report.cpp \
    ../source/documents/map_table_document.cpp
HEADERS += ../source/documents/map_table_change_report.h \
    ../source/documents/map_table_document.h
