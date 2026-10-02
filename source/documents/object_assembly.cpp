#include "object_assembly.h"
#include "technical_model_catalog.h"
#include "types.h"
#include <QJsonArray>
#include <QUuid>
#include <QCryptographicHash>
#include <cmath>
namespace ei {
QJsonObject assemblyWorldObject(const QJsonObject& object) {
    return object.contains("World object") ? object.value("World object").toObject() : object;
}
void setAssemblyWorldObject(QJsonObject* object, const QJsonObject& world) {
    if(object->contains("World object")) object->insert("World object",world); else *object=world;
}
static bool vector(const QJsonValue& value,int count) {
    if(!value.isArray() || value.toArray().size()!=count) return false;
    for(const auto& component:value.toArray()) if(!component.isDouble() || !std::isfinite(component.toDouble()) || qAbs(component.toDouble())>1e7) return false;
    return true;
}
bool readObjectAssembly(const QJsonObject& json,ObjectAssembly* result,QString* error) {
    auto fail=[&](const QString& message){if(error)*error=message;return false;};
    ObjectAssembly a; a.id=json.value("id").toString();a.name=json.value("name").toString().trimmed();
    a.category=json.value("category").toString();a.region=json.value("region").toString();
    if(json.value("schemaVersion").toInt()!=1 || QUuid(a.id).isNull() || a.name.isEmpty() || a.name.size()>200)
        return fail(QString::fromUtf8("Некорректное название или формат сборки."));
    if((!a.category.isEmpty() && (!TechnicalModelCatalog::categoryIds().contains(a.category) || !TechnicalModelCatalog::regionIds().contains(a.region)))
        || (a.category.isEmpty() && !a.region.isEmpty())) return fail(QString::fromUtf8("Некорректная категория сборки."));
    const auto parts=json.value("parts").toArray();
    if(parts.isEmpty() || parts.size()>512) return fail(QString::fromUtf8("Сборка должна содержать от 1 до 512 деталей."));
    for(int i=0;i<parts.size();++i) {
        const auto p=parts[i].toObject();AssemblyPart part;part.object=p.value("object").toObject();
        const auto wo=assemblyWorldObject(part.object);const int type=wo.value("Base object").toObject().value("Node type").toInt();
        // Only architecture and technical records. Creature/trap behavior has
        // other ID references and is intentionally not silently copied here.
        if(type!=eWorldObject && type!=eLever && type!=eTorch && type!=eLight && type!=eSound && type!=eParticle)
            return fail(QString::fromUtf8("Сборки поддерживают объекты мира, рычаги, факелы, свет, звук и частицы. Существа и ловушки не поддерживаются."));
        const auto base=wo.value("Base object").toObject();
        if(!vector(p.value("offset"),3) || !vector(base.value("Position"),3) || !vector(base.value("Rotation"),4))
            return fail(QString::fromUtf8("Некорректное положение или вращение детали."));
        double norm=0;for(const auto& v:base.value("Rotation").toArray())norm+=v.toDouble()*v.toDouble();
        if(norm<0.000001)return fail(QString::fromUtf8("Вращение детали не может быть нулевым кватернионом."));
        const auto offset=p.value("offset").toArray();part.offset={float(offset[0].toDouble()),float(offset[1].toDouble()),float(offset[2].toDouble())};
        if(!p.value("parentPart").isDouble() || p.value("parentPart").toDouble()!=p.value("parentPart").toInt(-2))
            return fail(QString::fromUtf8("Некорректная ссылка на родительскую деталь."));
        part.parentPart=p.value("parentPart").toInt(-2);
        if(part.parentPart< -1 || part.parentPart>=parts.size() || part.parentPart==i) return fail(QString::fromUtf8("Родительская деталь отсутствует."));
        const auto provenance=part.object.value("_Endurance raw provenance").toObject();
        if(provenance.value("sourceSubtreeBase64").toString().isEmpty() || provenance.value("cleanSubtreeBase64").toString().isEmpty())
            return fail(QString::fromUtf8("У детали нет полной исходной записи MOB. Сначала сохраните MOB и повторите создание сборки."));
        const auto source=QByteArray::fromBase64(provenance.value("sourceSubtreeBase64").toString().toLatin1());
        const auto hash=QByteArray::fromHex(provenance.value("sourceSubtreeSha256").toString().toLatin1());
        if(hash.size()!=32 || QCryptographicHash::hash(source,QCryptographicHash::Sha256)!=hash)
            return fail(QString::fromUtf8("Повреждена полная исходная запись детали MOB."));
        a.parts.append(part);
    }
    for(int i=0;i<a.parts.size();++i) {QSet<int> visited;int index=i;while(index>=0) {if(visited.contains(index))return fail(QString::fromUtf8("Цикл родительских связей в сборке."));visited.insert(index);index=a.parts[index].parentPart;}}
    a.extra=json;if(result)*result=a;return true;
}
QJsonObject writeObjectAssembly(const ObjectAssembly& a) {
    auto json=a.extra;json.insert("schemaVersion",1);json.insert("id",a.id);json.insert("name",a.name);
    json.insert("category",a.category);json.insert("region",a.region);QJsonArray parts;
    for(const auto& part:a.parts) parts.append(QJsonObject{{"object",part.object},{"offset",QJsonArray{part.offset.x(),part.offset.y(),part.offset.z()}},{"parentPart",part.parentPart}});
    json.insert("parts",parts);return json;
}
}
