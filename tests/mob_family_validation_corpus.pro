QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = mob_family_validation_corpus

INCLUDEPATH += ../source/documents
SOURCES += mob_family_validation_corpus.cpp \
           ../source/documents/mob_family_validation.cpp \
           ../source/documents/endurance_document.cpp \
           ../source/documents/script_reference_analyzer.cpp
HEADERS += ../source/documents/mob_family_validation.h \
           ../source/documents/endurance_document.h \
           ../source/documents/script_reference_analyzer.h
