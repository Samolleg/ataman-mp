QT += core testlib gui-private widgets
CONFIG += testcase console c++11
TEMPLATE = app
win32: LIBS += -lole32
TARGET = profile_database_tests

INCLUDEPATH += ../source/resources ../source/runtime/providers ../source/documents ../source/ui ../source/core
SOURCES += \
    ../source/ui/window_geometry.cpp \
    ../source/core/verified_clipboard.cpp \
    profile_database_tests.cpp \
    ../source/documents/database_document.cpp \
    ../source/documents/database_workbook.cpp \
    ../source/documents/item_spell_constructor.cpp \
    ../source/resources/game_text_catalog.cpp \
    ../source/documents/database_schema.cpp \
    ../source/documents/database_value_codec.cpp \
    ../source/documents/lossless_database_member.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/backup_manager.cpp \
    ../source/core/log.cpp \
    ../source/ui/database_editor_dialog.cpp \
    ../source/resources/profile_database.cpp \
    ../source/resources/database_map_catalog.cpp \
    ../source/runtime/character_bodypart_runtime.cpp \
    ../source/runtime/providers/profile_database_provider_support.cpp \
    ../source/runtime/providers/base_profile_database_provider.cpp \
    ../source/runtime/providers/jmv_profile_database_provider.cpp

HEADERS += \
    ../source/resources/profile_database.h \
    ../source/documents/database_document.h \
    ../source/documents/database_workbook.h \
    ../source/documents/item_spell_constructor.h \
    ../source/resources/game_text_catalog.h \
    ../source/documents/database_schema.h \
    ../source/documents/database_value_codec.h \
    ../source/documents/lossless_database_member.h \
    ../source/documents/res_archive_document.h \
    ../source/documents/backup_manager.h \
    ../source/core/log.h \
    ../source/ui/database_editor_dialog.h \
    ../source/runtime/providers/profile_database_provider_support.h \
    ../source/runtime/providers/base_profile_database_provider.h \
    ../source/runtime/providers/jmv_profile_database_provider.h

RESOURCES += ../assets/resources/data.qrc
