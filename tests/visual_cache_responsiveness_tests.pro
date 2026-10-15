QT += core gui widgets concurrent
CONFIG += c++11 console
TEMPLATE = app
TARGET = visual_cache_responsiveness_tests
INCLUDEPATH += $$PWD/../source/runtime $$PWD/../source/core
SOURCES += $$PWD/visual_cache_responsiveness_tests.cpp $$PWD/../source/runtime/persistent_visual_cache.cpp
