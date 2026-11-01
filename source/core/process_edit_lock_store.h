#ifndef PROCESS_EDIT_LOCK_STORE_H
#define PROCESS_EDIT_LOCK_STORE_H
#include <QtCore>

namespace mapper_edit_locks {
// Machine-wide ProgramData store. The override exists only in test binaries.
QString directory();
bool prepareDirectory(const QString& directory, QString* error);
// Read-only migration probe: never create, remove or acquire an old lock.
bool existingOwner(const QString& filename);
}
#endif
