QT += core gui widgets concurrent testlib
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = res_archive_document_tests

INCLUDEPATH += ../source/documents ../source/core ../source/app ../source/resources

SOURCES += \
    res_archive_document_tests.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/map_table_document.cpp \
    ../source/documents/minimap_catalog_registration.cpp \
    ../source/documents/synthetic_map_table_archive.cpp \
    ../source/documents/res_save_coordinator.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/resources/mmp_inspector.cpp \
    ../source/resources/mmp_codec.cpp \
    ../source/resources/res_file.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/documents/res_archive_document.h \
    ../source/documents/map_table_document.h \
    ../source/documents/minimap_catalog_registration.h \
    ../source/documents/synthetic_map_table_archive.h \
    ../source/documents/res_save_coordinator.h \
    ../source/documents/backup_manager.h \
    ../source/resources/mmp_inspector.h \
    ../source/resources/mmp_codec.h \
    ../source/resources/res_file.h \
    ../source/core/log.h

SOURCES += ../source/documents/travel_document.cpp ../source/documents/map_save_batch.cpp \
           ../source/documents/navigation_save_events.cpp
HEADERS += ../source/documents/navigation_save_events.h
# Current save/backup implementations also validate streamed RES and MPR data.
SOURCES += ../source/resources/res_archive_index.cpp \
           ../source/documents/endurance_document.cpp \
           ../source/documents/mpr_document.cpp \
           ../source/documents/mpr_namespace.cpp \
           ../source/documents/terrain_edit_model.cpp \
           ../source/documents/terrain_virtual_atlas.cpp

INCLUDEPATH += ../source/ui
SOURCES += ../source/ui/travel_panel.cpp
HEADERS += ../source/ui/travel_panel.h ../source/ui/world_coordinate_edit.h
