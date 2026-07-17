QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = runtime_provider_tests

INCLUDEPATH += ../source/runtime
SOURCES += \
    runtime_provider_tests.cpp \
    ../source/runtime/runtime_providers.cpp

HEADERS += \
    ../source/runtime/runtime_providers.h
