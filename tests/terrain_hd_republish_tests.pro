include(../ei_maper.pro)
QT += testlib
CONFIG += testcase console
TARGET = terrain_hd_republish_tests
SOURCES = $$PWD/terrain_hd_republish_tests.cpp
HEADERS =
FORMS =
RESOURCES =
TRANSLATIONS =
RC_ICONS =
isEmpty(HD_APP_BUILD): error(Set HD_APP_BUILD to the tested production build)
APP_OBJECTS = $$files($$HD_APP_BUILD/release/*.o)
APP_OBJECTS -= $$files($$HD_APP_BUILD/release/main.o)
LIBS += $$APP_OBJECTS
LIBS -= -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
PRE_TARGETDEPS += $$APP_OBJECTS
