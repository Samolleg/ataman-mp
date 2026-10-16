QT += core gui widgets opengl concurrent multimedia network gui-private
CONFIG += c++11 release
TEMPLATE = app
TARGET = lever_approach_integration_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of the tested application)
SOURCES += lever_approach_integration_check.cpp
BUILD_ROOT = $$join(BUILD_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT) $$PWD/../source
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$PWD/../source/$$dir)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
    !contains(object, .*scenario_layer_check.*): LIBS += $$quote($$object)
}
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
