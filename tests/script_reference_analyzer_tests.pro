QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = script_reference_analyzer_tests

INCLUDEPATH += ../source/documents
SOURCES += \
    script_reference_analyzer_tests.cpp \
    ../source/documents/script_reference_analyzer.cpp

HEADERS += ../source/documents/script_reference_analyzer.h
