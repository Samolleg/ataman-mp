QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = mpr_tile_transaction_corpus
INCLUDEPATH += ../source/documents
SOURCES += mpr_tile_transaction_corpus.cpp \
           ../source/documents/endurance_document.cpp \
           ../source/documents/mpr_document.cpp \
           ../source/documents/terrain_edit_model.cpp
HEADERS += ../source/documents/endurance_document.h \
           ../source/documents/mpr_document.h \
           ../source/documents/terrain_edit_model.h
