QT += core gui widgets testlib concurrent
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = gallery_thumbnail_cache_tests
INCLUDEPATH += ../source/ui ../source/runtime ../source/core
SOURCES += gallery_thumbnail_cache_tests.cpp \
    ../source/ui/gallery_thumbnail_cache.cpp ../source/runtime/persistent_visual_cache.cpp
HEADERS += ../source/ui/gallery_thumbnail_cache.h ../source/runtime/persistent_visual_cache.h
