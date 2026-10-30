QT += core gui widgets testlib concurrent
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = minimap_export_tests
INCLUDEPATH += ../source/documents ../source/resources ../source/core ../source/app
SOURCES += minimap_export_tests.cpp \
    ../source/documents/minimap_bake.cpp \
    ../source/documents/minimap_catalog_registration.cpp \
    ../source/documents/map_table_document.cpp \
    ../source/documents/res_texture_export.cpp \
    ../source/documents/res_save_coordinator.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/documents/synthetic_map_table_archive.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/resources/res_archive_index.cpp \
    ../source/core/log.cpp
HEADERS += ../source/documents/minimap_bake.h \
    ../source/documents/minimap_catalog_registration.h \
    ../source/documents/map_table_document.h \
    ../source/documents/res_texture_export.h \
    ../source/documents/res_archive_document.h \
    ../source/documents/backup_manager.h \
    ../source/documents/synthetic_map_table_archive.h \
    ../source/resources/mmp_codec.h \
    ../source/resources/mmp_inspector.h \
    ../source/core/log.h
