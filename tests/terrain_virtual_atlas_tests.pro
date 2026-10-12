QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = terrain_virtual_atlas_tests

RESOURCES += ../assets/resources/terrain_blend_masks.qrc

INCLUDEPATH += ../source/documents ../source/core ../source/resources
SOURCES += \
    terrain_virtual_atlas_tests.cpp \
    ../source/documents/terrain_mixed_edge.cpp \
    ../source/documents/terrain_recipe_bake.cpp \
    ../source/documents/terrain_virtual_atlas.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/mpr_namespace.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/documents/terrain_virtual_atlas.h \
    ../source/documents/terrain_edit_model.h \
    ../source/documents/mpr_document.h \
    ../source/documents/endurance_document.h \
    ../source/resources/mmp_codec.h \
    ../source/resources/mmp_inspector.h \
    ../source/core/log.h
