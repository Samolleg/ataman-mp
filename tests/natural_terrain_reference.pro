QT += core
QT -= gui
CONFIG += c++11 console release
TEMPLATE = app
TARGET = natural_terrain_reference
INCLUDEPATH += ../source/documents
SOURCES += natural_terrain_reference.cpp ../source/documents/natural_terrain.cpp
