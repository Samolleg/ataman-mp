QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++11 release
TEMPLATE = app
TARGET = world_mixed_save_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of the tested application)
SOURCES += world_mixed_save_check.cpp
BUILD_ROOT = $$join(BUILD_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT) $$PWD/../source
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$PWD/../source/$$dir)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
    !contains(object, .*scenario_layer_check.*): LIBS += $$quote($$object)
}
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
# The linked production entry point is qMain; QtTest selects console mode.
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
