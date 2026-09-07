QT += core testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = world_map_layout_document_tests

INCLUDEPATH += ../source/documents ../source/resources

SOURCES += \
    world_map_layout_document_tests.cpp \
    ../source/documents/world_map_layout_document.cpp \
    ../source/resources/map_table_parser.cpp \
    ../source/resources/world_map_layout.cpp

HEADERS += \
    ../source/documents/world_map_layout_document.h \
    ../source/resources/map_catalog.h \
    ../source/resources/world_map_layout.h
