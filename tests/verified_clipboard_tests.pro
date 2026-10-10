QT += core gui gui-private widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = verified_clipboard_tests
INCLUDEPATH += ../source/core
SOURCES += verified_clipboard_tests.cpp ../source/core/verified_clipboard.cpp
HEADERS += ../source/core/verified_clipboard.h
win32: LIBS += -lole32 -luser32 -lgdi32 -luuid
