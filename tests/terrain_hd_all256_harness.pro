include(../ei_maper.pro)
QT += testlib
CONFIG += console
TARGET = terrain_hd_all256_harness
SOURCES = $$PWD/terrain_hd_all256_harness.cpp
HEADERS =
FORMS =
RESOURCES =
TRANSLATIONS =
RC_ICONS =
isEmpty(HD_APP_BUILD): error(Set HD_APP_BUILD to the tested production build)
APP_OBJECTS = $$files($$HD_APP_BUILD/release/*.o)
APP_OBJECTS -= $$files($$HD_APP_BUILD/release/main.o)
LIBS += $$APP_OBJECTS
PRE_TARGETDEPS += $$APP_OBJECTS
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
