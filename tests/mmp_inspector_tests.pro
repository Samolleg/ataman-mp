QT += core gui testlib

CONFIG += testcase console c++11
TEMPLATE = app
TARGET = mmp_inspector_tests
INCLUDEPATH += ../source/resources

SOURCES += \
    mmp_inspector_tests.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/resources/mmp_codec.cpp

HEADERS += \
    ../source/resources/mmp_inspector.h \
    ../source/resources/mmp_codec.h
