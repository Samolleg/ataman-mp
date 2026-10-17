#include "technical_model_catalog.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

namespace ei {
namespace {
QStringList bindingNames(const QJsonValue& value)
{
    QStringList result;
    for(const auto& item : value.toArray()) {
        QString name=item.toString().trimmed().toLower();
        if(name.endsWith(".mmp")) name.chop(4);
        if(!name.isEmpty() && !result.contains(name)) result.append(name);
    }
    return result;
}
}
void TechnicalModelCatalog::refreshPortableBindings()
{
    // Old files can contain several installation/profile keys. Only identical
    // per-model collections may be adopted; never union conflicting libraries.
    QJsonObject candidates;
    QSet<QString> conflicts;
    const auto profiles=m_document.value("profiles").toObject();
    for(auto profile=profiles.begin();profile!=profiles.end();++profile) {
        const auto bindings=profile.value().toObject().value("modelTextures").toObject();
        for(auto it=bindings.begin();it!=bindings.end();++it) {
            const QString key=modelKey(it.key());
            QStringList names=bindingNames(it.value());
            if(key.isEmpty() || names.isEmpty()) continue;
            if(candidates.contains(key)) {
                auto previous=bindingNames(candidates.value(key));
                if(previous!=names) conflicts.insert(key);
            } else candidates.insert(key,it.value());
        }
    }
    for(const auto& key:conflicts) candidates.remove(key);
    // Explicit portable bindings are author data, unlike machine caches.
    const auto portable=m_document.value("modelTextures").toObject();
    for(auto it=portable.begin();it!=portable.end();++it)
        candidates.insert(modelKey(it.key()),it.value());
    m_portableBindings=candidates;
}
QString TechnicalModelCatalog::pathForDatabase(const QString& databasePath)
{
    return databasePath.isEmpty() ? QString()
        : QFileInfo(databasePath).dir().filePath("model-catalog.json");
}
bool TechnicalModelCatalog::loadProfile(const QString& databasePath,
    const QString& legacyPath, const QString& profile, QString* error)
{
    const QString destination = pathForDatabase(databasePath);
    if(destination.isEmpty())
    {
        m_path.clear(); m_document = {}; m_portableBindings = {}; m_valid = false;
        if(error) *error = QCoreApplication::translate("TechnicalModelCatalog",
            "The active profile has no database.res. Configure profile resources first.");
        return false;
    }
    const bool exists = QFileInfo::exists(destination);
    if(!load(destination, error)) return false;
    if(exists || !QFileInfo::exists(legacyPath)) return true;
    TechnicalModelCatalog legacy;
    if(!legacy.load(legacyPath, error)) { m_valid = false; return false; }
    // An exact EXE-side profile migration remains scoped to that profile. A
    // relocated source retains its old entries for unambiguous library adoption;
    // foreign scan caches are never restored under a new installation key.
    m_document = legacy.m_document;
    const auto profiles = legacy.m_document.value("profiles").toObject();
    if(profiles.contains(profile)) {
        m_document.insert("profiles",QJsonObject{{profile,profiles.value(profile)}});
    }
    refreshPortableBindings();
    return true; // Persist on an explicit catalog operation, never on selection.
}
QString TechnicalModelCatalog::modelKey(QString name)
{
    name = name.trimmed().toLower();
    if(name.endsWith(".mod") || name.endsWith(".fig")) name.chop(4);
    return name;
}
QString TechnicalModelCatalog::profileKey(const QStringList& roots)
{
    QStringList normalized;
    for(const QString& root : roots)
        normalized.append(QDir::cleanPath(QFileInfo(root).absoluteFilePath()).replace('\\', '/').toLower());
    return QString::fromLatin1(QCryptographicHash::hash(normalized.join('\n').toUtf8(),
        QCryptographicHash::Sha256).toHex());
}
bool TechnicalModelCatalog::load(const QString& path, QString* error)
{
    ++m_revision;
    m_path = path; m_valid = false; m_original.clear(); m_document = {}; m_portableBindings = {};
    m_existed = QFileInfo::exists(path);
    if(!m_existed)
    {
        m_document.insert("schemaVersion", 1); m_valid = true; return true;
    }
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
    {
        if(error) *error = QCoreApplication::translate("TechnicalModelCatalog", "Cannot read model catalog: %1").arg(path);
        return false;
    }
    m_original = file.readAll();
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(m_original, &parse);
    if(parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("schemaVersion").toInt() != 1
        || (document.object().contains("models") && !document.object().value("models").isObject())
        || (document.object().contains("profiles") && !document.object().value("profiles").isObject())
        || (document.object().contains("modelTextures") && !document.object().value("modelTextures").isObject())
        || (document.object().contains("assemblies") && !document.object().value("assemblies").isObject())
        || (document.object().contains("assemblyInstances") && !document.object().value("assemblyInstances").isObject()))
    {
        if(error) *error = QCoreApplication::translate("TechnicalModelCatalog", "Invalid model catalog. The existing file was preserved: %1").arg(path);
        return false;
    }
    m_document = document.object();
    const auto portable=m_document.value("modelTextures").toObject();
    for(auto it=portable.begin();it!=portable.end();++it) {
        bool valid=it.value().isArray() && !modelKey(it.key()).isEmpty();
        for(const auto& item:it.value().toArray()) valid=valid && item.isString() && !item.toString().trimmed().isEmpty();
        if(!valid) {
            if(error) *error=QCoreApplication::translate("TechnicalModelCatalog","Invalid model catalog. The existing file was preserved: %1").arg(path);
            m_document={}; return false;
        }
    }
    refreshPortableBindings(); m_valid = true; return true;
}
QByteArray TechnicalModelCatalog::stagedBytes() const
{
    if(!m_valid) return {};
    auto document = m_document;
    if(!m_portableBindings.isEmpty() || document.contains("modelTextures"))
        document.insert("modelTextures", m_portableBindings);
    const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    return bytes.size() <= 32*1024*1024 ? bytes : QByteArray();
}

bool TechnicalModelCatalog::save(QString* error)
{
    if(!m_valid) return false;
    QFile current(m_path);
    const bool exists = QFileInfo::exists(m_path);
    if(exists != m_existed || (exists && (!current.open(QIODevice::ReadOnly)
        || current.readAll() != m_original)))
    {
        if(error) *error = QCoreApplication::translate("TechnicalModelCatalog", "Model catalog changed outside this window. Reopen the gallery before saving.");
        return false;
    }
    current.close();
    if(!m_portableBindings.isEmpty() || m_document.contains("modelTextures"))
        m_document.insert("modelTextures",m_portableBindings);
    const QByteArray bytes = QJsonDocument(m_document).toJson(QJsonDocument::Indented);
    if(bytes.size()>32*1024*1024) {
        if(error)*error=QString::fromUtf8("Каталог превышает 32 МиБ. Изменения не записаны.");
        return false;
    }
    if(m_existed && bytes == m_original) return true;
    QSaveFile file(m_path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error) *error = QCoreApplication::translate("TechnicalModelCatalog", "Cannot save model catalog: %1").arg(file.errorString());
        return false;
    }
    m_original = bytes; m_existed = true; return true;
}
QString TechnicalModelCatalog::russianName(const QString& model) const
{
    return m_document.value("models").toObject().value(modelKey(model)).toObject().value("russianName").toString();
}
void TechnicalModelCatalog::setRussianName(const QString& model, const QString& name)
{
    ++m_revision;
    QJsonObject models = m_document.value("models").toObject();
    QJsonObject entry = models.value(modelKey(model)).toObject();
    entry.insert("russianName", name.trimmed()); models.insert(modelKey(model), entry);
    m_document.insert("models", models);
}
bool TechnicalModelCatalog::connections(const QString& model,QVector<ModelConnection>* points,QString* error) const
{
    return readConnections(m_document.value("models").toObject().value(modelKey(model)).toObject().value("connections"),points,error);
}
bool TechnicalModelCatalog::setConnections(const QString& model,const QVector<ModelConnection>& points,QString* error)
{
    if(!m_valid || modelKey(model).isEmpty()) return false;
    QVector<ModelConnection> checked;
    if(!readConnections(writeConnections(points),&checked,error)) return false;
    auto models=m_document.value("models").toObject(); auto entry=models.value(modelKey(model)).toObject();
    entry.insert("connections",writeConnections(checked)); models.insert(modelKey(model),entry);
    m_document.insert("models",models); ++m_revision; return true;
}
void TechnicalModelCatalog::registerModels(const QStringList& names)
{
    ++m_revision;
    QJsonObject models = m_document.value("models").toObject();
    for(const QString& name : names)
        if(!models.contains(modelKey(name))) models.insert(modelKey(name), QJsonObject{{"russianName", ""}});
    m_document.insert("models", models);
}
QStringList TechnicalModelCatalog::categoryIds()
{
    return {"houses", "architecture", "gates", "utensils", "trees", "bushes", "levers_chests", "decor"};
}
QStringList TechnicalModelCatalog::regionIds() { return {"gipat", "ingos", "suslanger"}; }
QVector<ObjectAssembly> TechnicalModelCatalog::assemblies(QString* error) const {
    QVector<ObjectAssembly> result;const auto entries=m_document.value("assemblies").toObject();
    for(auto it=entries.begin();it!=entries.end();++it) {
        ObjectAssembly a;
        if(!readObjectAssembly(it.value().toObject(),&a,error) || a.id!=it.key()) {
            if(error && error->isEmpty())*error=QString::fromUtf8("ID сборки не совпадает с ключом каталога.");
            return {};
        }
        result.append(a);
    }
    return result;
}
bool TechnicalModelCatalog::setAssembly(const ObjectAssembly& assembly,QString* error) {
    ObjectAssembly checked;
    if(!m_valid || !readObjectAssembly(writeObjectAssembly(assembly),&checked,error))return false;
    auto entries=m_document.value("assemblies").toObject();entries.insert(checked.id,writeObjectAssembly(checked));
    m_document.insert("assemblies",entries);++m_revision;return true;
}
bool TechnicalModelCatalog::removeAssembly(const QString& id) {
    auto entries=m_document.value("assemblies").toObject();if(!m_valid || !entries.contains(id))return false;
    entries.remove(id);m_document.insert("assemblies",entries);++m_revision;return true;
}
QJsonObject TechnicalModelCatalog::assemblyInstances(const QString& mobPath) const {
    const QString key=QFileInfo(m_path).dir().relativeFilePath(QFileInfo(mobPath).absoluteFilePath()).replace('\\','/').toCaseFolded();
    return m_document.value("assemblyInstances").toObject().value(key).toObject();
}
void TechnicalModelCatalog::setAssemblyInstances(const QString& mobPath,const QJsonObject& instances) {
    const QString key=QFileInfo(m_path).dir().relativeFilePath(QFileInfo(mobPath).absoluteFilePath()).replace('\\','/').toCaseFolded();
    auto entries=m_document.value("assemblyInstances").toObject();
    if(instances.isEmpty())entries.remove(key);else entries.insert(key,instances);
    m_document.insert("assemblyInstances",entries);++m_revision;
}
QString TechnicalModelCatalog::category(const QString& model) const
{
    return m_document.value("models").toObject().value(modelKey(model)).toObject().value("category").toString();
}
QString TechnicalModelCatalog::region(const QString& model) const
{
    return m_document.value("models").toObject().value(modelKey(model)).toObject().value("region").toString();
}
bool TechnicalModelCatalog::setPlacement(const QString& model, const QString& category, const QString& region)
{
    if(!m_valid || modelKey(model).isEmpty()) return false;
    if(!category.isEmpty() && (!categoryIds().contains(category) || !regionIds().contains(region))) return false;
    if(category.isEmpty() && !region.isEmpty()) return false;
    auto models = m_document.value("models").toObject();
    auto entry = models.value(modelKey(model)).toObject();
    if(category.isEmpty()) { entry.remove("category"); entry.remove("region"); }
    else { entry.insert("category", category); entry.insert("region", region); }
    models.insert(modelKey(model), entry); m_document.insert("models", models);
    return true;
}
bool TechnicalModelCatalog::matches(const QString& model, const QString& name, const QString& query)
{
    const QStringList words = query.simplified().split(' ', QString::SkipEmptyParts);
    const QString text = model + ' ' + name;
    for(const QString& word : words) if(!text.contains(word, Qt::CaseInsensitive)) return false;
    return true;
}
void TechnicalModelCatalog::restoreScan(const QString& profile, TechnicalTextureCatalog* scan) const
{
    scan->restoreCache(m_document.value("profiles").toObject().value(profile).toObject().value("scanCache").toObject());
}
void TechnicalModelCatalog::captureScan(const QString& profile, const TechnicalTextureCatalog& scan)
{
    ++m_revision;
    QJsonObject profiles = m_document.value("profiles").toObject();
    QJsonObject entry = profiles.value(profile).toObject();
    entry.insert("scanCache", scan.saveCache());
    QJsonObject textures = entry.value("modelTextures").toObject();
    const auto observed = scan.observedModelTextures();
    for(auto model = observed.constBegin(); model != observed.constEnd(); ++model)
        if(!textures.contains(model.key())) textures.insert(model.key(), QJsonArray::fromStringList(model.value()));
    entry.insert("modelTextures", textures); profiles.insert(profile, entry); m_document.insert("profiles", profiles);
    refreshPortableBindings();
}
QVector<STechnicalTextureChoice> TechnicalModelCatalog::textureChoices(const QString& profile,
    const QString& model, const QStringList& available, const TechnicalTextureCatalog& scan) const
{
    const QJsonObject bindings = m_document.value("profiles").toObject().value(profile).toObject().value("modelTextures").toObject();
    const QString key = modelKey(model);
    const QJsonValue names=bindings.contains(key) ? bindings.value(key) : m_portableBindings.value(key);
    if(names.isUndefined()) return scan.exactChoices(model, available);
    QVector<STechnicalTextureChoice> result;
    QSet<QString> added;
    for(const QJsonValue& value : names.toArray())
        for(const QString& installed : available)
        {
            QString wanted = value.toString().trimmed();
            if(wanted.endsWith(".mmp", Qt::CaseInsensitive)) wanted.chop(4);
            if(wanted.compare(installed, Qt::CaseInsensitive) == 0 && !added.contains(installed.toLower()))
            {
                STechnicalTextureChoice choice; choice.name = installed;
                result.append(choice); added.insert(installed.toLower()); break;
            }
        }
    return result;
}
void TechnicalModelCatalog::bindTexture(const QString& profile, const QString& model, const QString& texture)
{
    ++m_revision;
    QJsonObject profiles = m_document.value("profiles").toObject();
    QJsonObject entry = profiles.value(profile).toObject();
    QJsonObject bindings = entry.value("modelTextures").toObject();
    const QString key=modelKey(model);
    QJsonArray names = (bindings.contains(key) ? bindings.value(key) : m_portableBindings.value(key)).toArray();
    bool exists = false;
    for(const auto& name : names) if(name.toString().compare(texture, Qt::CaseInsensitive) == 0) exists = true;
    if(!exists) names.append(texture);
    bindings.insert(modelKey(model), names); entry.insert("modelTextures", bindings);
    profiles.insert(profile, entry); m_document.insert("profiles", profiles);
    auto portable=m_document.value("modelTextures").toObject();
    portable.insert(key,names); m_document.insert("modelTextures",portable);
    refreshPortableBindings();
}
bool TechnicalModelCatalog::hasTextureBindings(const QString& profile) const
{
    return !m_portableBindings.isEmpty() || !m_document.value("profiles").toObject()
        .value(profile).toObject().value("modelTextures").toObject().isEmpty();
}
}
