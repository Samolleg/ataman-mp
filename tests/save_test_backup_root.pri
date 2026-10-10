# Include in native targets that compile or link CBackupManager. Initialization
# precedes main, so default document saves cannot reach a shared user/qttest root.
SOURCES += $$PWD/save_test_backup_root.cpp
