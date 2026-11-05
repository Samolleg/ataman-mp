QT += core gui widgets opengl concurrent multimedia network gui-private testlib
CONFIG += c++14 release console
TEMPLATE = app
TARGET = save_integrity_check
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT)
isEmpty(SOURCE_ROOT): error(Pass SOURCE_ROOT)
BUILD_ROOT = $$join(BUILD_ROOT, " ")
SOURCE_ROOT = $$join(SOURCE_ROOT, " ")
INCLUDEPATH += $$quote($$BUILD_ROOT) $$quote($$SOURCE_ROOT/source)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$SOURCE_ROOT/source/$$dir)
SOURCES += $$PWD/save_integrity_check.cpp $$quote($$SOURCE_ROOT/source/core/process_edit_lock_store.cpp)
DEFINES += EI_MAPPER_LOCK_STORE_TESTING
contains(CONFIG, baseline279) { DEFINES += EI_BASELINE_279 ENDURANCE_VERSION=\\\"279\\\" }
else {
 DEFINES += EI_SAVE_INTEGRITY_TEST ENDURANCE_VERSION=\\\"280\\\"
 SOURCES += $$quote($$SOURCE_ROOT/source/documents/map_save_batch.cpp)
}
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
 !contains(object, .*process_edit_lock_store.*) {
  contains(CONFIG, baseline279)|!contains(object, .*map_save_batch.*) {
   productionLibraries += $$shell_quote($$object)
   PRE_TARGETDEPS += $$quote($$object)
  }
 }
}
write_file($$OUT_PWD/production-objects.rsp, productionLibraries)
LIBS += @$$quote($$OUT_PWD/production-objects.rsp)
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)