QT += core testlib
QT -= gui
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = process_edit_lock_tests
INCLUDEPATH += ../source/core
SOURCES += process_edit_lock_tests.cpp
HEADERS += ../source/core/process_edit_lock.h
