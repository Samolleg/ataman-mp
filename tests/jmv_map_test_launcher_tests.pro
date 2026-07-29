QT += core testlib
CONFIG += testcase c++11 console
CONFIG -= app_bundle
TEMPLATE = app

INCLUDEPATH += ../source/documents ../source/resources ../source/runtime

SOURCES += \
    jmv_map_test_launcher_tests.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/documents/map_table_document.cpp \
    ../source/documents/minimap_catalog_registration.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/synthetic_map_table_archive.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/runtime/current_session_backup.cpp \
    ../source/runtime/hot_reload_protocol.cpp \
    ../source/runtime/jmv_map_test_launcher.cpp \
    ../source/runtime/runtime_providers.cpp

HEADERS += \
    ../source/documents/endurance_document.h \
    ../source/documents/map_table_document.h \
    ../source/documents/minimap_catalog_registration.h \
    ../source/documents/mpr_document.h \
    ../source/documents/res_archive_document.h \
    ../source/documents/synthetic_map_table_archive.h \
    ../source/documents/terrain_edit_model.h \
    ../source/resources/mmp_codec.h \
    ../source/resources/mmp_inspector.h \
    ../source/runtime/current_session_backup.h \
    ../source/runtime/hot_reload_protocol.h \
    ../source/runtime/jmv_map_test_launcher.h \
    ../source/runtime/runtime_providers.h
