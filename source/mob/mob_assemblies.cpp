#include "mob.h"
#include "map_save_batch.h"
#include "technical_model_catalog.h"
#include "profile_database.h"
#include "log.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QUuid>
#include <QCache>
#include <QDateTime>
namespace {
QString catalogPath() {
    auto db=ei::ProfileDatabaseContext::instance().current();
    return ei::TechnicalModelCatalog::pathForDatabase(db ? db->sourceArchivePath : QString());
}
QString digest(const QByteArray& bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
ei::TechnicalModelCatalog* cachedCatalog(const QString& path,QString* error) {
    // Opening a family may load dozens of MOBs. Parse the profile JSON once,
    // not once per MOB; never perform this work on object selection.
    static QCache<QString,ei::TechnicalModelCatalog> cache(2);
    QFileInfo info(path);const QString identity=info.absoluteFilePath().toCaseFolded()+"|"+QString::number(info.size())+"|"+QString::number(info.lastModified().toMSecsSinceEpoch());
    if(auto* found=cache.object(identity))return found;
    auto* catalog=new ei::TechnicalModelCatalog;
    if(!catalog->load(path,error)){delete catalog;return nullptr;}cache.insert(identity,catalog);return catalog;
}
}
QByteArray CMob::assemblyProjection() const {
    QJsonArray state;
    for(auto* node:m_aNode)if(!node->isMarkDeleted() && !node->assemblyTag().isEmpty())
        state.append(QJsonObject{{"id",double(node->mapId())},{"tag",node->assemblyTag()}});
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}
void CMob::restoreAssemblyInstances() {
    const QString path=catalogPath();if(path.isEmpty() || !QFileInfo::exists(path))return;
    QString error;auto* catalog=cachedCatalog(path,&error);if(!catalog){ei::log(eLogWarning,error);return;}
    const auto instance=catalog->assemblyInstances(m_filePath.absoluteFilePath());if(instance.isEmpty())return;
    // A changed file must not attach old editor groups to unrelated reused IDs.
    if(instance.value("mobSha256").toString()!=digest(m_originalData)) {
        ei::log(eLogWarning,"Assembly groups were not restored: the MOB changed outside their saved snapshot.");return;
    }
    QSet<CNode*> assigned;
    for(const auto& value:instance.value("groups").toArray()) {
        const auto group=value.toObject();const auto tag=group.value("tag").toObject();
        if(QUuid(tag.value("instanceId").toString()).isNull() || tag.value("name").toString().isEmpty())continue;
        QList<CNode*> members;bool valid=true;
        for(const auto& v:group.value("members").toArray()) {
            const auto member=v.toObject();CNode* matched=nullptr;int count=0;
            for(auto* node:m_aNode)if(node->mapId()==member.value("id").toVariant().toUInt()
                && int(node->nodeType())==member.value("type").toInt() && node->modelName()==member.value("model").toString()) {matched=node;++count;}
            if(count!=1 || assigned.contains(matched) || members.contains(matched)) {valid=false;break;}
            members.append(matched);
        }
        if(!valid || members.isEmpty())continue;
        for(auto* node:members){node->setAssemblyTag(tag);assigned.insert(node);}
    }
}
bool CMob::saveAssemblyInstances(const QString& destination,QString* error) {
    const bool saved=writeAssemblyInstances(destination,error);m_assemblyMetadataPending=!saved;
    if(saved)m_cleanAssemblyProjection=assemblyProjection();else setDirty(true);return saved;
}
bool CMob::prepareAssemblySave(const QByteArray& savedBytes,QMap<QString,ei::TechnicalModelCatalog>* catalogs,QString* error,const QString& destination) {
    QJsonObject grouped;
    for(auto* node:m_aNode) if(!node->isMarkDeleted() && !node->assemblyTag().isEmpty()) {
        const auto tag=node->assemblyTag();const auto id=tag.value("instanceId").toString();auto group=grouped.value(id).toObject();
        group.insert("tag",tag);auto members=group.value("members").toArray();
        members.append(QJsonObject{{"id",double(node->mapId())},{"type",int(node->nodeType())},{"model",node->modelName()}});
        group.insert("members",members);grouped.insert(id,group);
    }
    const QString path=catalogPath();
    if(grouped.isEmpty() && (path.isEmpty() || !QFileInfo::exists(path)))return true;
    if(path.isEmpty()){if(error)*error=QString::fromUtf8("Нет database.res профиля для сохранения группировки сборок.");return false;}
    ei::TechnicalModelCatalog catalog;
    if(catalogs->contains(path))catalog=catalogs->value(path);else if(!catalog.load(path,error))return false;
    const auto target=destination.isEmpty()?m_filePath.absoluteFilePath():destination;
    if(grouped.isEmpty() && catalog.assemblyInstances(target).isEmpty())return true;
    QJsonArray groups;for(auto it=grouped.begin();it!=grouped.end();++it)groups.append(it.value());
    const auto instances=groups.isEmpty()?QJsonObject():QJsonObject{{"mobSha256",digest(savedBytes)},{"groups",groups}};
    if(catalog.assemblyInstances(target)==instances)return true;
    catalog.setAssemblyInstances(target,instances);
    if(catalog.stagedBytes().isEmpty()){if(error)*error=QString::fromUtf8("Каталог сборок не подготовлен: ")+path;return false;}
    catalogs->insert(path,catalog);return true;
}

void CMob::acceptAssemblySave(){m_assemblyMetadataPending=false;m_cleanAssemblyProjection=assemblyProjection();}
bool CMob::writeAssemblyInstances(const QString& destination,QString* error){
    // Save As temporarily uses its destination identity in the same preparation.
    const auto source=m_filePath;m_filePath=QFileInfo(destination);
    QMap<QString,ei::TechnicalModelCatalog> catalogs;
    const bool prepared=prepareAssemblySave(m_originalData,&catalogs,error);m_filePath=source;
    if(!prepared)return false;
    for(auto& catalog:catalogs)if(!catalog.save(error))return false;
    return true;
}

bool CMob::appendAssemblySaveFiles(const QMap<QString,ei::TechnicalModelCatalog>& catalogs,QVector<SMapSaveFile>* files,QString* error)
{
    for(auto it=catalogs.cbegin();it!=catalogs.cend();++it){
        SMapSaveFile file;file.path=it.key();file.existed=it.value().sourceExisted();file.before=it.value().originalBytes();file.after=it.value().stagedBytes();
        if(file.after.isEmpty()){if(error)*error=QString::fromUtf8("Каталог сборок не подготовлен: ")+file.path;return false;}
        files->append(file);
    }
    return true;
}
