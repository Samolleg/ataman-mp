QT += core gui widgets opengl concurrent multimedia network gui-private
CONFIG += c++11 release
TEMPLATE = app
TARGET = object_edit_performance_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT for the tested Release application)
BUILD_ROOT = $$join(BUILD_ROOT, " ")
!isEmpty(BASELINE_INCLUDE) {
    BASELINE_INCLUDE = $$join(BASELINE_INCLUDE, " ")
    INCLUDEPATH += $$quote($$BASELINE_INCLUDE)
    DEFINES += EI_EDIT_BASELINE
}
INCLUDEPATH += $$quote($$BUILD_ROOT)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$PWD/../source/$$dir)
INCLUDEPATH += $$quote($$PWD/../source)
SOURCES += $$PWD/object_edit_performance_check.cpp
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
    !contains(object, .*assembly_catalog_check.*): LIBS += $$quote($$object)
}
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
