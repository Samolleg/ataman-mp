#pragma once
#include "map_table_document.h"
#include "map_save_batch.h"

class CTravelDocument
{
public:
    bool open(const QString& archivePath, QString* error);
    const QString& path() const {return m_path;}
    const CMapTableDocument& table() const {return m_table;}
    QByteArray snapshot() const {return m_table.serialize();}
    bool restore(const QByteArray& bytes, QString* error=nullptr);
    bool dirty() const {return snapshot()!=m_baseline;}
    void discard() {restore(m_baseline);}
    int uniqueZone(const QString& id) const;
    bool replaceZone(int index, const SMapTableZone& zone, QString* error);
    bool appendZone(const SMapTableZone& zone, QString* error);
    bool prepareSave(SMapSaveFile* file, QString* error) const;
    void acceptSaved() {m_baseline=snapshot();}
private:
    QString m_path;
    QString m_member;
    QByteArray m_baseline;
    CMapTableDocument m_table;
};
