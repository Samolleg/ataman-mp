QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = mob_projection_transaction_corpus
INCLUDEPATH += ../source/documents
SOURCES += mob_projection_transaction_corpus.cpp \
           ../source/documents/endurance_document.cpp
HEADERS += ../source/documents/endurance_document.h
