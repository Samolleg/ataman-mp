QT += core testlib
QT -= gui
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = collection_document_tests
INCLUDEPATH += ../source/documents
SOURCES += collection_document_tests.cpp \
    ../source/documents/collection_document.cpp
HEADERS += ../source/documents/collection_document.h
