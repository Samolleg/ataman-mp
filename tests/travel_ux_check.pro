QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++11 release console
travel_r2: DEFINES += TRAVEL_UX_R2
TEMPLATE = app
TARGET = travel_ux_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of fresh Release)
isEmpty(BASELINE_ROOT): error(Pass extracted225 panel fixture root)
SOURCE_ROOT = $$clean_path($$BUILD_ROOT/../source)
INCLUDEPATH += $$quote($$BUILD_ROOT) $$quote($$SOURCE_ROOT) $$quote($$BASELINE_ROOT)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$SOURCE_ROOT/$$dir)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
objectResponse =
for(object, productionObjects) {
    !contains(object, .*assembly_catalog_check.*) {
        baseline_keys:contains(object, .*view_keybinding.o):next()
        objectResponse += $$quote(\"$${object}\")
        PRE_TARGETDEPS += $$quote($$object)
    }
}
write_file($$OUT_PWD/production-objects.rsp, objectResponse)
LIBS += @production-objects.rsp
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32 -ladvapi32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
SOURCES += travel_ux_check.cpp $$quote($$BASELINE_ROOT/travel_panel225.cpp)
HEADERS += $$quote($$BASELINE_ROOT/travel_panel225.h)
baseline_keys {
    DEFINES += TRAVEL_BASELINE_KEYS
    SOURCES += $$quote($$BASELINE_ROOT/view_keybinding.cpp)
}
