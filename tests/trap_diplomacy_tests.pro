QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = trap_diplomacy_tests
INCLUDEPATH += ../source/core
SOURCES += trap_diplomacy_tests.cpp \
    ../source/core/trap_diplomacy.cpp
HEADERS += ../source/core/trap_diplomacy.h
