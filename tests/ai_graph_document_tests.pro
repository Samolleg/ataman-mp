QT += core testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = ai_graph_document_tests

INCLUDEPATH += ../source/documents
SOURCES += \
    ai_graph_document_tests.cpp \
    ../source/documents/ai_graph_document.cpp

HEADERS += ../source/documents/ai_graph_document.h
