QT += core testlib
QT -= gui
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = profile_visual_provider_tests

INCLUDEPATH += ../source/resources ../source/runtime ../source/runtime/providers
SOURCES += \
    profile_visual_provider_tests.cpp \
    ../source/resources/profile_database.cpp \
    ../source/runtime/profile_visual_provider.cpp \
    ../source/runtime/providers/profile_visual_resolver_support.cpp \
    ../source/runtime/providers/base_profile_visual_provider.cpp \
    ../source/runtime/providers/jmv_profile_visual_provider.cpp \
    ../source/runtime/providers/profile_database_provider_support.cpp \
    ../source/runtime/providers/base_profile_database_provider.cpp \
    ../source/runtime/providers/jmv_profile_database_provider.cpp

HEADERS += \
    ../source/resources/profile_database.h \
    ../source/runtime/runtime_providers.h \
    ../source/runtime/profile_visual_provider.h \
    ../source/runtime/providers/profile_visual_resolver_support.h \
    ../source/runtime/providers/base_profile_visual_provider.h \
    ../source/runtime/providers/jmv_profile_visual_provider.h
