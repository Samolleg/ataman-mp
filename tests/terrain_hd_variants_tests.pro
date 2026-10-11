QT += core gui
QT -= widgets
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = terrain_hd_variants_tests
SOURCES += $$PWD/terrain_hd_variants_tests.cpp \
    $$PWD/../source/documents/terrain_hd_variants.cpp \
    $$PWD/../source/resources/mmp_codec.cpp
HEADERS += $$PWD/../source/documents/terrain_hd_variants.h
