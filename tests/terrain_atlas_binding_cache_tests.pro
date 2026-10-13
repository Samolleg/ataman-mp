include(../ei_maper.pro)
QT += testlib
CONFIG += testcase console
TARGET = terrain_atlas_binding_cache_tests
SOURCES = $$PWD/terrain_atlas_binding_cache_tests.cpp
HEADERS =
FORMS =
RESOURCES =
TRANSLATIONS =
RC_ICONS =
isEmpty(BINDING_APP_BUILD): error(Provide the tested production build)
APP_OBJECTS = $$files($$BINDING_APP_BUILD/release/*.o)
APP_OBJECTS -= $$files($$BINDING_APP_BUILD/release/main.o)
isEmpty(APP_OBJECT_RESPONSE): LIBS += $$APP_OBJECTS
else: LIBS += $$quote(@$$APP_OBJECT_RESPONSE)
LIBS -= -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
PRE_TARGETDEPS += $$APP_OBJECTS
