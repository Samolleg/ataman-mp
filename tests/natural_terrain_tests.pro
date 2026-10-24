QT += core
QT -= gui
CONFIG += c++11 console release
TEMPLATE = app
TARGET = natural_terrain_tests
INCLUDEPATH += ../source/documents
SOURCES += natural_terrain_tests.cpp ../source/documents/natural_terrain.cpp
