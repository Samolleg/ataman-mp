# Build the application first, then pass BUILD_ROOT=<its shadow-build directory>.
# Runs through --scenario-layer-check with EI_NEIGHBOR_PERF_ROOT/RES and
# EI_AUTHORING_REFERENCE pointing only to owned private fixture copies.
QT += core gui widgets opengl concurrent multimedia network gui-private
CONFIG += c++11 release
TEMPLATE = app
TARGET = world_paint_publication_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT for the tested Release application)
SOURCES += $$PWD/world_paint_publication_check.cpp
DEFINES += EIPUB_CANDIDATE
BUILD_ROOT = $$join(BUILD_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs) {
    INCLUDEPATH += $$quote($$PWD/../source/$$dir)
}
INCLUDEPATH += $$quote($$PWD/../source)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
    !contains(object, .*neighbor_performance_check.*): LIBS += $$quote($$object)
}
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
