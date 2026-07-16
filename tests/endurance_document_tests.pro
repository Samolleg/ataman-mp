QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests

INCLUDEPATH += ..
SOURCES += \
    endurance_document_tests.cpp \
    ../endurance_document.cpp \
    ../staged_save_validator.cpp

HEADERS += \
    ../endurance_document.h \
    ../staged_save_validator.h
