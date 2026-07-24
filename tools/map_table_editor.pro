QT += core gui widgets
CONFIG += c++11 release
TEMPLATE = app
TARGET = EIMapTableEditor
DESTDIR = release

INCLUDEPATH += \
    ../source/ui \
    ../source/documents \
    ../source/core \
    ../source/app

SOURCES += \
    map_table_editor_main.cpp \
    ../source/ui/map_table_editor_dialog.cpp \
    ../source/documents/map_table_document.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/res_save_coordinator.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/core/log.cpp

HEADERS += \
    ../source/ui/map_table_editor_dialog.h \
    ../source/documents/map_table_document.h \
    ../source/documents/res_archive_document.h \
    ../source/documents/res_save_coordinator.h \
    ../source/documents/backup_manager.h \
    ../source/core/log.h
