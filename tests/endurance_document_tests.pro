QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests

RESOURCES += ../assets/resources/semantic_atlas_presets.qrc

INCLUDEPATH += ../source/documents ../source/core ../source/render ../source/resources ../source/app
SOURCES += \
    endurance_document_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/mpr_namespace.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/documents/terrain_virtual_atlas.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/render/terrain_atlas_analyzer.cpp \
    ../source/render/terrain_atlas_profile.cpp \
    ../source/render/terrain_atlas_presets.cpp \
    ../source/documents/staged_save_validator.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/core/portable_diagnostic_writer.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/core/landscape_edit_policy.h \
    ../source/core/terrain_tile_information_policy.h \
    ../source/core/operation_history_policy.h \
    ../source/documents/endurance_document.h \
    ../source/documents/mpr_document.h \
    ../source/documents/terrain_edit_model.h \
    ../source/documents/terrain_virtual_atlas.h \
    ../source/documents/res_archive_document.h \
    ../source/resources/mmp_codec.h \
    ../source/render/terrain_atlas_analyzer.h \
    ../source/render/terrain_atlas_profile.h \
    ../source/documents/staged_save_validator.h \
    ../source/documents/backup_manager.h \
    ../source/core/portable_diagnostic_writer.h \
    ../source/core/log.h
