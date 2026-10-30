QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++11 release
TEMPLATE = app
TARGET = atlas_compact_source_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of the tested application)
BUILD_ROOT = $$join(BUILD_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT) $$PWD/../source
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$PWD/../source/$$dir)
SOURCES += atlas_compact_source_check.cpp
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
    !contains(object, .*assembly_catalog_check.*) {
        productionLibraries += $$shell_quote($$object)
        PRE_TARGETDEPS += $$quote($$object)
    }
}
write_file($$OUT_PWD/production-objects.rsp, productionLibraries)
LIBS += @$$quote($$OUT_PWD/production-objects.rsp)
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
