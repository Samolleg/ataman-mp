QT += core gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = terrain_bake_export
HEADERS += ../../source/core/log.h
RESOURCES += ../../assets/resources/semantic_atlas_presets.qrc ../../assets/resources/terrain_blend_masks.qrc
INCLUDEPATH += ../../source/documents ../../source/core ../../source/render ../../source/resources
SOURCES += export.cpp \
    ../../source/documents/endurance_document.cpp \
    ../../source/documents/mpr_document.cpp \
    ../../source/documents/mpr_namespace.cpp \
    ../../source/documents/terrain_edit_model.cpp \
    ../../source/documents/terrain_virtual_atlas.cpp \
    ../../source/documents/terrain_semantic_field_store.cpp \
    ../../source/documents/res_archive_document.cpp \
    ../../source/resources/mmp_codec.cpp \
    ../../source/resources/mmp_inspector.cpp \
    ../../source/resources/resource_name.cpp \
    ../../source/render/terrain_atlas_analyzer.cpp \
    ../../source/render/terrain_atlas_profile.cpp \
    ../../source/render/terrain_atlas_presets.cpp \
    ../../source/core/log.cpp
