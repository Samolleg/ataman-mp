#ifndef STAGED_SAVE_VALIDATOR_H
#define STAGED_SAVE_VALIDATOR_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>

struct SStagedSaveValidation
{
    bool success{false};
    QString error;
    QString sha256;
    qint64 size{0};
};

SStagedSaveValidation validateStagedSave(const QByteArray& data,
                                         const QFileInfo& destination);

#endif // STAGED_SAVE_VALIDATOR_H
