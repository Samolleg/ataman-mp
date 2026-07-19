QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = mob_family_validation_tests

INCLUDEPATH += ../source/documents
SOURCES += mob_family_validation_tests.cpp \
           ../source/documents/mob_family_validation.cpp \
           ../source/documents/endurance_document.cpp \
           ../source/documents/script_reference_analyzer.cpp
HEADERS += ../source/documents/mob_family_validation.h \
           ../source/documents/endurance_document.h \
           ../source/documents/script_reference_analyzer.h
