QT += core testlib
CONFIG += testcase c++11 console
CONFIG -= app_bundle
TEMPLATE = app
win32:LIBS += user32.lib gdi32.lib psapi.lib

INCLUDEPATH += ../source/documents ../source/resources ../source/runtime

SOURCES += \
    jmv_map_test_launcher_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/map_table_document.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/mpr_namespace.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/synthetic_map_table_archive.cpp \
    ../source/runtime/current_session_backup.cpp \
    ../source/runtime/hot_reload_protocol.cpp \
    ../source/runtime/jmv_map_test_launcher.cpp \
    ../source/runtime/runtime_providers.cpp

HEADERS += \
    ../source/documents/endurance_document.h \
    ../source/documents/map_table_document.h \
    ../source/documents/mpr_document.h \
    ../source/documents/res_archive_document.h \
    ../source/documents/synthetic_map_table_archive.h \
    ../source/runtime/current_session_backup.h \
    ../source/runtime/hot_reload_protocol.h \
    ../source/runtime/jmv_map_test_launcher.h \
    ../source/runtime/runtime_providers.h
