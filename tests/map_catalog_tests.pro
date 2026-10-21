QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = map_catalog_tests

INCLUDEPATH += ../source/resources ../source/core
SOURCES += \
    map_catalog_tests.cpp \
    ../source/resources/map_catalog.cpp \
    ../source/resources/res_archive_index.cpp \
    ../source/resources/map_table_parser.cpp

HEADERS += ../source/resources/map_catalog.h
