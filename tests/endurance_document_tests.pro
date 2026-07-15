QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests

INCLUDEPATH += ..
SOURCES += \
    endurance_document_tests.cpp \
    ../endurance_document.cpp

HEADERS += ../endurance_document.h
