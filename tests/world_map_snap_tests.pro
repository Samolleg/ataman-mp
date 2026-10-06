QT += core gui widgets testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = world_map_snap_tests
INCLUDEPATH += ../source/resources ../source/documents
SOURCES += world_map_snap_tests.cpp \
    ../source/resources/world_map_snap.cpp \
    ../source/resources/world_map_layout.cpp \
    ../source/resources/map_table_parser.cpp \
    ../source/resources/neighbor_map_layout.cpp \
    ../source/documents/world_map_layout_document.cpp
HEADERS += ../source/resources/world_map_snap.h \
    ../source/documents/world_map_layout_command.h \
    ../source/documents/world_map_layout_document.h
