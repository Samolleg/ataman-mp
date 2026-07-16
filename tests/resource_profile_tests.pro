QT += core testlib
QT -= gui
CONFIG += testcase c++11
TEMPLATE = app
TARGET = resource_profile_tests

INCLUDEPATH += ..
SOURCES += \
    resource_profile_tests.cpp \
    ../resource_profile.cpp

HEADERS += ../resource_profile.h
