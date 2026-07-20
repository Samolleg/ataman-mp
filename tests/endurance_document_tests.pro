QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests

INCLUDEPATH += ../source/documents ../source/core ../source/render
SOURCES += \
    endurance_document_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/render/terrain_atlas_analyzer.cpp \
    ../source/documents/staged_save_validator.cpp

HEADERS += \
    ../source/core/landscape_edit_policy.h \
    ../source/core/operation_history_policy.h \
    ../source/documents/endurance_document.h \
    ../source/documents/mpr_document.h \
    ../source/documents/terrain_edit_model.h \
    ../source/render/terrain_atlas_analyzer.h \
    ../source/documents/staged_save_validator.h
