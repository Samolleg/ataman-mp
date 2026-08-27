#ifndef DATABASE_WORKBOOK_H
#define DATABASE_WORKBOOK_H
#include <QMap>
#include <QString>
#include "database_document.h"
namespace ei {
QStringList workbookTableKeys(const DatabaseDocument& document);
struct SDatabaseWorkbookImport { QMap<QString,SDatabaseTable> tables; QVector<SDatabaseValidationFinding> findings; };
class DatabaseWorkbook {
public:
    static bool exportXlsx(const DatabaseDocument& document,const QString& path,QString* diagnostic=nullptr);
    static bool importXlsx(const DatabaseDocument& document,const QString& path,SDatabaseWorkbookImport* result,QString* diagnostic=nullptr);
    static QByteArray embeddedTablesLua(const QString& path);
};
}
#endif
