QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++14 release console
TEMPLATE = app
TARGET = navbake_close284_check
isEmpty(BUILD_ROOT): error(Pass fresh archive Release BUILD_ROOT)
SOURCE_ROOT = $$clean_path($$BUILD_ROOT/../source/source)
INCLUDEPATH += $$BUILD_ROOT $$SOURCE_ROOT
INCLUDEPATH += $$SOURCE_ROOT/app $$SOURCE_ROOT/core $$SOURCE_ROOT/documents $$SOURCE_ROOT/layout_components $$SOURCE_ROOT/mob $$SOURCE_ROOT/objects $$SOURCE_ROOT/render $$SOURCE_ROOT/resources $$SOURCE_ROOT/runtime $$SOURCE_ROOT/ui
NAV_VENDOR = $$clean_path($$BUILD_ROOT/../source/third_party/eirebuild64-nav)
INCLUDEPATH += $$NAV_VENDOR $$NAV_VENDOR/external/recastnavigation/Detour/Include
DEFINES += DT_POLYREF64 EI_MAPPER_LOCK_STORE_TESTING
productionObjects = $$files($$BUILD_ROOT/release/*.o)
objectResponse =
for(object, productionObjects) {
    objectName = $$basename(object)
    !equals(objectName, main.o):!contains(object, .*assembly_catalog_check.*):!contains(object, .*process_edit_lock_store.*) {
        objectResponse += $$quote("$${object}")
        PRE_TARGETDEPS += $$object
    }
}
write_file($$OUT_PWD/production-objects.rsp, objectResponse)
LIBS += @production-objects.rsp -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32 -ladvapi32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
SOURCES += navbake_close284_check.cpp $$SOURCE_ROOT/core/process_edit_lock_store.cpp
