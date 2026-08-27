QT += core widgets testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = ai_graph_document_tests

INCLUDEPATH += ../source ../source/documents ../source/render
SOURCES += \
    ai_graph_document_tests.cpp \
    ../source/documents/ai_graph_document.cpp \
    ../source/documents/ai_graph_preview_builder.cpp \
    ../source/render/ai_graph_preview_invalidation.cpp

HEADERS += \
    ../source/core/map_object_placement_policy.h \
    ../source/documents/ai_graph_document.h \
    ../source/documents/ai_graph_preview_builder.h \
    ../source/render/ai_graph_preview_invalidation.h
