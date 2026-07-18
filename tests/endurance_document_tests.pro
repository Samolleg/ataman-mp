QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests

INCLUDEPATH += ../source/documents
SOURCES += \
    endurance_document_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/documents/staged_save_validator.cpp

HEADERS += \
    ../source/documents/endurance_document.h \
    ../source/documents/mpr_document.h \
    ../source/documents/terrain_edit_model.h \
    ../source/documents/staged_save_validator.h
