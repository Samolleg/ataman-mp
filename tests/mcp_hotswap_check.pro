QT += core network testlib
QT -= gui
CONFIG += console c++11 release
TEMPLATE = app
TARGET = mcp_hotswap_check
SOURCES += mcp_hotswap_check.cpp ../source/app/ai_agent_protocol.cpp
HEADERS += ../source/app/ai_agent_protocol.h
INCLUDEPATH += ../source/app
DEFINES += ENDURANCE_VERSION=\\\"222-test-client\\\"
win32:LIBS += -ladvapi32
