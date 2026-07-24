QT += core testlib
QT -= gui
CONFIG += testcase c++11
TEMPLATE = app
TARGET = resource_profile_tests

INCLUDEPATH += ../source/resources
SOURCES += \
    resource_profile_tests.cpp \
    ../source/resources/resource_profile.cpp

HEADERS += \
    ../source/resources/resource_profile.h \
    ../source/runtime/runtime_variant_registry.h
