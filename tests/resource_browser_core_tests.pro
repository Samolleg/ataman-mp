QT += core testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = resource_browser_core_tests

INCLUDEPATH += ../source/resources

SOURCES += \
    resource_browser_core_tests.cpp \
    ../source/resources/res_archive_index.cpp \
    ../source/resources/resource_catalog.cpp

HEADERS += \
    ../source/resources/res_archive_index.h \
    ../source/resources/resource_catalog.h
