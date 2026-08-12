QT += core testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = resource_browser_core_tests

INCLUDEPATH += ../source/resources ../source/core

SOURCES += \
    resource_browser_core_tests.cpp \
    ../source/resources/res_archive_index.cpp \
    ../source/resources/resource_catalog.cpp \
    ../source/resources/resource_model_preview.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/resources/mmp_codec.cpp

HEADERS += \
    ../source/resources/res_archive_index.h \
    ../source/resources/resource_catalog.h \
    ../source/resources/resource_model_preview.h \
    ../source/resources/mmp_inspector.h \
    ../source/resources/mmp_codec.h
