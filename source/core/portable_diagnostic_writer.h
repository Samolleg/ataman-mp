#ifndef PORTABLE_DIAGNOSTIC_WRITER_H
#define PORTABLE_DIAGNOSTIC_WRITER_H

#include <QByteArray>
#include <QString>

namespace ei {

// Portable tester folders can permit ordinary writes while denying the
// delete/rename sequence used by QSaveFile. Diagnostic reports are intentionally
// replaceable in place so repeated marks remain useful in those folders.
bool writePortableDiagnostic(const QString& path, const QByteArray& contents,
                             QString* error = nullptr);

}

#endif // PORTABLE_DIAGNOSTIC_WRITER_H
