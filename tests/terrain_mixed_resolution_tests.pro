include(../ei_maper.pro)
QT += testlib
CONFIG += testcase console
TARGET = terrain_mixed_resolution_tests
SOURCES = $$PWD/terrain_mixed_resolution_tests.cpp
HEADERS =
FORMS =
RESOURCES =
TRANSLATIONS =
RC_ICONS =
isEmpty(MIXED_APP_BUILD): MIXED_APP_BUILD = $$PWD/../build-release-137-mixed
APP_OBJECTS = $$files($$MIXED_APP_BUILD/release/*.o)
APP_OBJECTS -= $$MIXED_APP_BUILD/release/main.o
isEmpty(APP_OBJECTS): error(Build the production release first)
LIBS += $$APP_OBJECTS
LIBS -= -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
PRE_TARGETDEPS += $$APP_OBJECTS
