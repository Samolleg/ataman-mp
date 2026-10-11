include(../ei_maper.pro)

CONFIG += console
TARGET = terrain_hd_memory_probe
SOURCES = $$PWD/terrain_hd_memory_probe.cpp
HEADERS =
FORMS =
RESOURCES =
TRANSLATIONS =
RC_ICONS =
isEmpty(HDMEM_APP_BUILD): error(Provide HDMEM_APP_BUILD from the isolated source snapshot)
APP_OBJECTS = $$files($$HDMEM_APP_BUILD/release/*.o)
APP_OBJECTS -= $$HDMEM_APP_BUILD/release/main.o
isEmpty(APP_OBJECTS): error(Build the production release first)
LIBS += $$APP_OBJECTS
LIBS -= -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
PRE_TARGETDEPS += $$APP_OBJECTS
