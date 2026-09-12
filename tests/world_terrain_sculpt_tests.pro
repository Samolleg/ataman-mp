QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = world_terrain_sculpt_tests

RESOURCES += ../assets/resources/terrain_blend_masks.qrc

INCLUDEPATH += ../source/documents ../source/core ../source/resources
SOURCES += \
    world_terrain_sculpt_tests.cpp \
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

SOURCES += ../source/documents/world_terrain_sculpt.cpp ../source/documents/world_terrain_session.cpp
HEADERS += ../source/documents/world_terrain_sculpt.h ../source/documents/world_terrain_session.h
