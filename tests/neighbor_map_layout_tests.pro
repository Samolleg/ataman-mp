QT += core testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = neighbor_map_layout_tests

INCLUDEPATH += ../source/resources

SOURCES += \
    neighbor_map_layout_tests.cpp \
    ../source/resources/map_table_parser.cpp \
    ../source/resources/neighbor_map_layout.cpp

HEADERS += \
    ../source/resources/map_catalog.h \
    ../source/resources/neighbor_map_layout.h
