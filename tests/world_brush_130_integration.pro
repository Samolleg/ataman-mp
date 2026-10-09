include(../ei_maper.pro)
QT += testlib
CONFIG += testcase console release
CONFIG -= debug
TARGET = world_brush_130_integration
SOURCES -= $$PWD/../source/app/main.cpp
SOURCES += $$PWD/world_brush_130_integration.cpp
RC_ICONS = $$PWD/../icon.ico
RESOURCES = $$PWD/../assets/resources/data.qrc \
    $$PWD/../assets/resources/semantic_atlas_presets.qrc \
    $$PWD/../assets/resources/shaders.qrc \
    $$PWD/../assets/resources/terrain_blend_masks.qrc \
    $$PWD/../assets/resources/textures.qrc \
    $$PWD/../translations/translations.qrc
