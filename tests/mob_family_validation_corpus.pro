QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = mob_family_validation_corpus

INCLUDEPATH += ../source/documents ../source/resources ../source/runtime ../source/runtime/providers
SOURCES += mob_family_validation_corpus.cpp \
           ../source/documents/mob_family_validation.cpp \
           ../source/documents/endurance_document.cpp \
           ../source/documents/script_reference_analyzer.cpp \
           ../source/runtime/character_bodypart_runtime.cpp \
           ../source/resources/assembly_inventory.cpp \
           ../source/resources/profile_database.cpp \
           ../source/runtime/providers/profile_database_provider_support.cpp \
           ../source/runtime/providers/base_profile_database_provider.cpp \
           ../source/runtime/providers/jmv_profile_database_provider.cpp
HEADERS += ../source/documents/mob_family_validation.h \
           ../source/documents/endurance_document.h \
           ../source/documents/script_reference_analyzer.h \
           ../source/runtime/character_bodypart_runtime.h \
           ../source/resources/assembly_inventory.h \
           ../source/resources/profile_database.h \
           ../source/runtime/providers/profile_database_provider_support.h \
           ../source/runtime/providers/base_profile_database_provider.h \
           ../source/runtime/providers/jmv_profile_database_provider.h
