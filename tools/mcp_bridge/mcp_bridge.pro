QT += core network
QT -= gui
CONFIG += console c++11 release
CONFIG -= app_bundle
TARGET = EIMapperMcp
TEMPLATE = app
SOURCES += main.cpp ../../source/app/ai_agent_protocol.cpp
HEADERS += ../../source/app/ai_agent_protocol.h
INCLUDEPATH += ../../source/app
win32:LIBS += -ladvapi32
VERSION_NUMBER = $$cat(../../VERSION, lines)
DEFINES += ENDURANCE_VERSION=\\\"$${VERSION_NUMBER}\\\"
