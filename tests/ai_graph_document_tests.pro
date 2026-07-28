QT += core testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = ai_graph_document_tests

INCLUDEPATH += ../source/documents
SOURCES += \
    ai_graph_document_tests.cpp \
    ../source/documents/ai_graph_document.cpp \
    ../source/documents/ai_graph_preview_builder.cpp

HEADERS += \
    ../source/core/map_object_placement_policy.h \
    ../source/documents/ai_graph_document.h \
    ../source/documents/ai_graph_preview_builder.h
