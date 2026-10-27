#-------------------------------------------------
#
# Project created by QtCreator 2019-01-27T13:50:17
#
#-------------------------------------------------

QT       += core gui opengl concurrent multimedia gui-private network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = EIMapperEndurance
TEMPLATE = app

ENDURANCE_VERSION = $$cat($$PWD/VERSION, lines)
DEFINES += ENDURANCE_VERSION=\\\"$${ENDURANCE_VERSION}\\\"

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
#DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++14
# C++14 is required by the unmodified Navigation/Types.h aggregates.
NAV_VENDOR = $$PWD/third_party/eirebuild64-nav
INCLUDEPATH += $$NAV_VENDOR $$NAV_VENDOR/external/recastnavigation/Detour/Include
DEFINES += DT_POLYREF64
SOURCES += $$files($$NAV_VENDOR/external/recastnavigation/Detour/Source/*.cpp)
!system(powershell.exe -NoProfile -ExecutionPolicy Bypass -File $$shell_quote($$PWD/tools/check_nav_vendor.ps1)): error(Nav vendor lock mismatch)

INCLUDEPATH += \
    $$PWD/source \
    $$PWD/source/app \
    $$PWD/source/core \
    $$PWD/source/documents \
    $$PWD/source/layout_components \
    $$PWD/source/mob \
    $$PWD/source/objects \
    $$PWD/source/render \
    $$PWD/source/resources \
    $$PWD/source/runtime \
    $$PWD/source/ui

SOURCES += $$files($$PWD/source/*.cpp, true)
SOURCES -= $$PWD/source/runtime/jmv_bridge/jmv_launch_bridge.cpp
SOURCES -= $$PWD/source/runtime/base_runtime_host/base_runtime_host.cpp
HEADERS += $$files($$PWD/source/*.h, true)
FORMS += $$files($$PWD/source/*.ui, true)
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32
win32: LIBS += -lole32 -ladvapi32
# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RC_ICONS = icon.ico

RESOURCES += \
    assets/resources/data.qrc \
    assets/resources/semantic_atlas_presets.qrc \
    assets/resources/shaders.qrc \
    assets/resources/terrain_blend_masks.qrc \
    assets/resources/textures.qrc

TRANSLATIONS += translations/ei_mapper_ru.ts
RESOURCES += translations/translations.qrc
