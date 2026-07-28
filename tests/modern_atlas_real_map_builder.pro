QT += core gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = modern_atlas_real_map_builder

RESOURCES += ../assets/resources/semantic_atlas_presets.qrc

INCLUDEPATH += ../source/documents ../source/core ../source/render ../source/resources
SOURCES += \
    modern_atlas_real_map_builder.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/mpr_namespace.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/documents/terrain_virtual_atlas.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/render/terrain_atlas_analyzer.cpp \
    ../source/render/terrain_atlas_profile.cpp \
    ../source/render/terrain_atlas_presets.cpp \
    ../source/core/log.cpp
