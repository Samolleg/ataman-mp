#ifndef DUPLICATE_ID_REPORT_H
#define DUPLICATE_ID_REPORT_H

#include <QString>
#include <QVector>

namespace ei {

QString duplicateIdWarningText(const QVector<uint>& duplicateOccurrences,
                               int previewLimit = 10);

}

#endif // DUPLICATE_ID_REPORT_H
