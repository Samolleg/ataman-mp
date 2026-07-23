QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = map_table_document_tests

INCLUDEPATH += ../source/documents
SOURCES += \
    map_table_document_tests.cpp \
    ../source/documents/map_table_document.cpp

HEADERS += ../source/documents/map_table_document.h
