QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++14 release console
TEMPLATE = app
TARGET = rebuild64_tools_r2_check
isEmpty(BUILD_ROOT): error(Pass fresh production BUILD_ROOT)
isEmpty(SOURCE_ROOT): error(Pass archived SOURCE_ROOT)
BUILD_ROOT = $$join(BUILD_ROOT, " ")
SOURCE_ROOT = $$join(SOURCE_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT) $$quote($$SOURCE_ROOT/source)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$SOURCE_ROOT/source/$$dir)
SOURCES += $$PWD/rebuild64_tools_r2_check.cpp $$quote($$SOURCE_ROOT/source/core/process_edit_lock_store.cpp)
DEFINES += EI_MAPPER_LOCK_STORE_TESTING EI_SAVE_INTEGRITY_TEST
SOURCES += $$quote($$SOURCE_ROOT/source/documents/map_save_batch.cpp)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
 !contains(object, .*/main.o):!contains(object, .*process_edit_lock_store.*):!contains(object, .*map_save_batch.*) {
  productionLibraries += $$shell_quote($$object)
  PRE_TARGETDEPS += $$quote($$object)
 }
}
write_file($$OUT_PWD/production-objects.rsp, productionLibraries)
LIBS += @$$quote($$OUT_PWD/production-objects.rsp)
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32 -ladvapi32 -lpsapi -ldxgi
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
