# Build the production release first. Link its exact objects to exercise the
# actual provider/hydration paths without changing main.cpp or registering a check.
include(../ei_maper.pro)
QT += testlib
CONFIG += testcase console
TARGET = res_reader_drain_tests
SOURCES = $$PWD/res_reader_drain_tests.cpp
HEADERS =
FORMS =
RESOURCES =
TRANSLATIONS =
isEmpty(RESDRAIN_APP_BUILD): RESDRAIN_APP_BUILD = $$PWD/../build-release-133-resdrain
APP_OBJECTS = $$files($$RESDRAIN_APP_BUILD/release/*.o)
APP_OBJECTS -= $$RESDRAIN_APP_BUILD/release/main.o
isEmpty(APP_OBJECTS): error(Build the production release before this test target)
LIBS += $$APP_OBJECTS

CONFIG -= c++11
CONFIG += c++14
RC_ICONS =
LIBS -= -lglu32 -lopengl32 -ldbghelp -luser32 -lole32
LIBS += -lglu32 -lopengl32 -ldbghelp -luser32 -lole32

PRE_TARGETDEPS += $$APP_OBJECTS
