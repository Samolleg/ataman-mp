QT += core gui widgets opengl concurrent multimedia network gui-private
CONFIG += c++11 release console
TEMPLATE = app
isEmpty(HOUSEKEEPING_BUILD_ROOT): HOUSEKEEPING_BUILD_ROOT = $$PWD/../build-tests-housekeeping-211/source/build-release-211
BUILD_ROOT = $$HOUSEKEEPING_BUILD_ROOT
SOURCE_ROOT = $$PWD/../source
INCLUDEPATH += $$quote($$BUILD_ROOT) $$quote($$SOURCE_ROOT)
sourceDirs = app core documents layout_components mob objects render resources runtime ui
for(dir, sourceDirs): INCLUDEPATH += $$quote($$SOURCE_ROOT/$$dir)
productionObjects = $$files($$quote($$BUILD_ROOT/release/*.o))
for(object, productionObjects) {
    !contains(object, .*assembly_catalog_check.*) {
        LIBS += $$quote($$relative_path($$object, $$OUT_PWD))
        PRE_TARGETDEPS += $$quote($$object)
    }
}
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32 -lshell32
win32: LIBS += -lmingw32 $$quote($$[QT_INSTALL_LIBS]/libqtmain.a)
TARGET = housekeeping_check
SOURCES += housekeeping_check.cpp
