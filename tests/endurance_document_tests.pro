QT += core gui widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = endurance_document_tests
include(save_test_backup_root.pri)

RESOURCES += \
    ../assets/resources/semantic_atlas_presets.qrc \
    ../assets/resources/terrain_blend_masks.qrc

INCLUDEPATH += ../source/documents ../source/core ../source/render ../source/resources ../source/app
SOURCES += \
    endurance_document_tests.cpp \
    ../source/render/neighbor_scene_data.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/technical_texture_catalog.cpp \
    ../source/documents/technical_model_catalog.cpp \
    ../source/documents/object_assembly.cpp \
    ../source/documents/model_connections.cpp \
    ../source/documents/ai_graph_persistence.cpp \
    ../source/documents/ai_graph_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/mpr_namespace.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/documents/terrain_semantic_field_store.cpp \
    ../source/documents/terrain_virtual_atlas.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/resource_name.cpp \
    ../source/render/terrain_atlas_analyzer.cpp \
    ../source/render/terrain_atlas_archive_binding.cpp \
    ../source/render/terrain_atlas_profile.cpp \
    ../source/render/terrain_paint_diagnostic.cpp \
    ../source/render/overlay_material_animation.cpp \
    ../source/render/terrain_atlas_presets.cpp \
    ../source/documents/staged_save_validator.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/documents/terrain_atlas_recovery_store.cpp \
    ../source/core/portable_diagnostic_writer.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/core/landscape_edit_policy.h \
    ../source/core/terrain_atlas_shape_policy.h \
    ../source/core/terrain_save_flow_policy.h \
    ../source/core/terrain_catalog_load_policy.h \
    ../source/core/terrain_tile_information_policy.h \
    ../source/core/operation_history_policy.h \
    ../source/core/tile_brush_stroke_policy.h \
    ../source/documents/endurance_document.h \
    ../source/documents/technical_texture_catalog.h \
    ../source/documents/technical_model_catalog.h \
    ../source/documents/object_assembly.h \
    ../source/documents/model_connections.h \
    ../source/documents/ai_graph_persistence.h \
    ../source/documents/ai_graph_document.h \
    ../source/documents/mpr_document.h \
    ../source/documents/terrain_edit_model.h \
    ../source/documents/terrain_semantic_field_store.h \
    ../source/documents/terrain_virtual_atlas.h \
    ../source/documents/res_archive_document.h \
    ../source/resources/mmp_codec.h \
    ../source/resources/resource_name.h \
    ../source/render/terrain_atlas_analyzer.h \
    ../source/render/terrain_atlas_archive_binding.h \
    ../source/render/terrain_atlas_profile.h \
    ../source/render/terrain_paint_diagnostic.h \
    ../source/render/overlay_material_animation.h \
    ../source/documents/staged_save_validator.h \
    ../source/documents/backup_manager.h \
    ../source/documents/terrain_atlas_recovery_store.h \
    ../source/core/portable_diagnostic_writer.h \
    ../source/core/log.h
