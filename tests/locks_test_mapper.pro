# Test-only mapper: identical production objects except the store root resolver.
# It cannot use real ProgramData; EI_MAPPER_TEST_LOCK_ROOT is mandatory.
QT += core gui widgets opengl concurrent multimedia network gui-private
CONFIG += c++14 release
TEMPLATE = app
TARGET = EIMapperEndurance
isEmpty(BUILD_ROOT): error(Pass BUILD_ROOT of the tested application)
SOURCE_ROOT = $$clean_path($$BUILD_ROOT/../source)
INCLUDEPATH += $$quote($$SOURCE_ROOT/core)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
objectResponse =
for(object, productionObjects) {
    !contains(object, .*process_edit_lock_store.o) {
        objectResponse += $$quote(\"$${object}\")
        PRE_TARGETDEPS += $$quote($$object)
    }
}
write_file($$OUT_PWD/production-objects.rsp, objectResponse)
LIBS += @production-objects.rsp
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32 -ladvapi32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
SOURCES += $$quote($$SOURCE_ROOT/core/process_edit_lock_store.cpp)
DEFINES += EI_MAPPER_LOCK_STORE_TESTING
