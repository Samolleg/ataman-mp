QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++14 release console
TEMPLATE = app
TARGET = nav64_legend230_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of fresh Release)
SOURCE_ROOT = $$clean_path($$BUILD_ROOT/../source)
INCLUDEPATH += $$quote($$BUILD_ROOT) $$quote($$SOURCE_ROOT)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$SOURCE_ROOT/$$dir)
NAV_VENDOR = $$clean_path($$BUILD_ROOT/../third_party/eirebuild64-nav)
INCLUDEPATH += $$quote($$NAV_VENDOR) $$quote($$NAV_VENDOR/external/recastnavigation/Detour/Include)
DEFINES += DT_POLYREF64
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
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32 -ladvapi32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
SOURCES += nav64_legend230_check.cpp
