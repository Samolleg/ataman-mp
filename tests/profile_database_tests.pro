QT += core testlib gui-private
CONFIG += testcase console c++11
TEMPLATE = app
TARGET = profile_database_tests

INCLUDEPATH += ../source/resources ../source/runtime/providers ../source/documents
SOURCES += \
    profile_database_tests.cpp \
    ../source/documents/database_document.cpp \
    ../source/documents/database_workbook.cpp \
    ../source/documents/lossless_database_member.cpp \
    ../source/documents/res_archive_document.cpp \
    ../source/resources/profile_database.cpp \
    ../source/runtime/providers/profile_database_provider_support.cpp \
    ../source/runtime/providers/base_profile_database_provider.cpp \
    ../source/runtime/providers/jmv_profile_database_provider.cpp

HEADERS += \
    ../source/resources/profile_database.h \
    ../source/documents/database_document.h \
    ../source/documents/database_workbook.h \
    ../source/documents/lossless_database_member.h \
    ../source/documents/res_archive_document.h \
    ../source/runtime/providers/profile_database_provider_support.h \
    ../source/runtime/providers/base_profile_database_provider.h \
    ../source/runtime/providers/jmv_profile_database_provider.h
