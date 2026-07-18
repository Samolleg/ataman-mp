QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = unit_animation_tests
INCLUDEPATH += ../source/runtime ../source/resources
SOURCES += unit_animation_tests.cpp \
    ../source/runtime/unit_animation.cpp \
    ../source/runtime/profile_visual_provider.cpp \
    ../source/runtime/providers/base_profile_visual_provider.cpp \
    ../source/runtime/providers/jmv_profile_visual_provider.cpp \
    ../source/runtime/providers/profile_visual_resolver_support.cpp \
    ../source/resources/profile_database.cpp \
    ../source/runtime/providers/base_profile_database_provider.cpp \
    ../source/runtime/providers/jmv_profile_database_provider.cpp \
    ../source/runtime/providers/profile_database_provider_support.cpp
HEADERS += ../source/runtime/unit_animation.h \
    ../source/runtime/runtime_providers.h \
    ../source/runtime/profile_visual_provider.h
