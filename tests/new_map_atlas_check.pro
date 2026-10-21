QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++11 release
QMAKE_LFLAGS_RELEASE -= -Wl,-s
TEMPLATE = app
TARGET = new_map_atlas_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of the tested application)
SOURCES += new_map_atlas_check.cpp
BUILD_ROOT = $$join(BUILD_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT) $$PWD/../source
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$PWD/../source/$$dir)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
objectResponse =
for(object, productionObjects) {
    !contains(object, .*assembly_catalog_check.*) {
        objectResponse += $$quote(\"$${object}\")
        PRE_TARGETDEPS += $$quote($$object)
    }
}
write_file($$OUT_PWD/production-objects.rsp, objectResponse)
LIBS += @production-objects.rsp
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
# The linked production entry point is qMain; QtTest selects console mode.
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
