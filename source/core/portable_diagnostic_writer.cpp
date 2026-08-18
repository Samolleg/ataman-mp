#include "portable_diagnostic_writer.h"

#include <QFile>

bool ei::writePortableDiagnostic(const QString& path, const QByteArray& contents,
                                 QString* error)
{
    QFile output(path);
    if(!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if(error)
            *error = output.errorString();
        return false;
    }
    const qint64 written = output.write(contents);
    output.close();
    if(written != contents.size() || output.error() != QFileDevice::NoError)
    {
        if(error)
            *error = output.errorString();
        return false;
    }
    if(error)
        error->clear();
    return true;
}
