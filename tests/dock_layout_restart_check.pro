QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++14 release console
TEMPLATE = app
TARGET = dock_layout_restart_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of the tested application)
isEmpty(SOURCE_ROOT): error(Pass SOURCE_ROOT)
INCLUDEPATH += $$quote($$BUILD_ROOT) $$quote($$SOURCE_ROOT)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$SOURCE_ROOT/$$dir)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
objectResponse =
for(object, productionObjects) {
    !contains(object, .*assembly_catalog_check.*):!contains(object, .*process_edit_lock_store.o) {
        objectResponse += $$quote(\"$${object}\")
        PRE_TARGETDEPS += $$quote($$object)
    }
}
write_file($$OUT_PWD/production-objects.rsp, objectResponse)
LIBS += @production-objects.rsp
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32 -ladvapi32 -lgdi32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
SOURCES += dock_layout_restart_check.cpp
SOURCES += $$quote($$SOURCE_ROOT/core/process_edit_lock_store.cpp)
DEFINES += EI_MAPPER_LOCK_STORE_TESTING
