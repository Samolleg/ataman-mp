#include "settings_persistence.h"

#include <QSaveFile>

namespace ei {

bool writeSettingsFileAtomically(const QString& path, const QByteArray& data,
                                 QString* diagnostic)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))
    {
        if(diagnostic) *diagnostic = file.errorString();
        return false;
    }
    if(file.write(data) != data.size())
    {
        if(diagnostic) *diagnostic = file.errorString();
        file.cancelWriting();
        return false;
    }
    if(!file.commit())
    {
        if(diagnostic) *diagnostic = file.errorString();
        return false;
    }
    if(diagnostic) diagnostic->clear();
    return true;
}

}
