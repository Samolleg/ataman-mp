QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = hot_reload_protocol_tests
INCLUDEPATH += ../source/runtime
SOURCES += hot_reload_protocol_tests.cpp \
    ../source/runtime/hot_reload_protocol.cpp
HEADERS += ../source/runtime/hot_reload_protocol.h
