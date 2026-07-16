QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = mob_projection_transaction_corpus
INCLUDEPATH += ..
SOURCES += mob_projection_transaction_corpus.cpp \
           ../endurance_document.cpp
HEADERS += ../endurance_document.h
