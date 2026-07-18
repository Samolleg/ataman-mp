QT += core testlib
CONFIG += testcase c++11 console
CONFIG -= app_bundle
TEMPLATE = app

INCLUDEPATH += ../source/documents ../source/resources ../source/runtime

SOURCES += \
    jmv_map_test_launcher_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/runtime/current_session_backup.cpp \
    ../source/runtime/jmv_map_test_launcher.cpp \
    ../source/runtime/runtime_providers.cpp

HEADERS += \
    ../source/documents/endurance_document.h \
    ../source/runtime/current_session_backup.h \
    ../source/runtime/jmv_map_test_launcher.h \
    ../source/runtime/runtime_providers.h
