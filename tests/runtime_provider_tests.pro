QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = runtime_provider_tests

INCLUDEPATH += ..
SOURCES += \
    runtime_provider_tests.cpp \
    ../runtime_providers.cpp

HEADERS += \
    ../runtime_providers.h
