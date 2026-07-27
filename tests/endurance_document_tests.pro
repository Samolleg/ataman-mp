QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests

INCLUDEPATH += ../source/documents ../source/core ../source/render ../source/app
SOURCES += \
    endurance_document_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/render/terrain_atlas_analyzer.cpp \
    ../source/render/terrain_atlas_profile.cpp \
    ../source/documents/staged_save_validator.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/core/landscape_edit_policy.h \
    ../source/core/operation_history_policy.h \
    ../source/core/tile_brush_stroke_policy.h \
    ../source/documents/endurance_document.h \
    ../source/documents/mpr_document.h \
    ../source/documents/terrain_edit_model.h \
    ../source/render/terrain_atlas_analyzer.h \
    ../source/render/terrain_atlas_profile.h \
    ../source/documents/staged_save_validator.h \
    ../source/documents/backup_manager.h \
    ../source/core/log.h
