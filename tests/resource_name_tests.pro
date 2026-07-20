QT += core gui testlib

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = resource_name_tests
INCLUDEPATH += ../source/resources

SOURCES += \
    resource_name_tests.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/resource_name.cpp \
    ../source/resources/unit_appearance.cpp

HEADERS += \
    ../source/resources/resource_name.h \
    ../source/resources/mmp_codec.h \
    ../source/resources/unit_appearance.h
