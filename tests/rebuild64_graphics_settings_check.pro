QT += core gui widgets
CONFIG += console c++14
CONFIG -= app_bundle
TEMPLATE = app
TARGET = rebuild64_graphics_settings_check
INCLUDEPATH += ../source/render ../source/app
SOURCES += rebuild64_graphics_settings_check.cpp ../source/render/rebuild64_graphics_settings.cpp ../source/app/endurance_ui_scale.cpp
HEADERS += ../source/render/rebuild64_graphics_settings.h
