QT += core gui testlib
CONFIG += testcase c++11 console
TEMPLATE = app
TARGET = terrain_resolve_audit_tests

INCLUDEPATH += ../source/documents ../source/render ../source/resources

SOURCES += \
    terrain_resolve_audit_tests.cpp \
    ../source/render/terrain_resolve_audit.cpp \
    ../source/documents/terrain_edit_model.cpp \
    ../source/documents/terrain_virtual_atlas.cpp \
    ../source/documents/mpr_document.cpp \
    ../source/documents/mpr_namespace.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/documents/endurance_document.cpp \
    ../source/resources/mmp_codec.cpp

HEADERS += \
    ../source/render/terrain_resolve_audit.h \
    ../source/documents/terrain_edit_model.h \
    ../source/documents/terrain_virtual_atlas.h \
    ../source/documents/mpr_document.h \
    ../source/documents/endurance_document.h \
    ../source/documents/res_archive_document.h \
    ../source/resources/mmp_codec.h
