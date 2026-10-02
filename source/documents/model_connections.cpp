#include "model_connections.h"
#include <QJsonArray>
#include <QSet>
#include <QtMath>
namespace ei {
namespace {
bool finite(const QVector3D& v) { return qIsFinite(v.x()) && qIsFinite(v.y()) && qIsFinite(v.z()) && v.lengthSquared()<1e12f; }
QJsonArray vectorJson(const QVector3D& v) { return {v.x(),v.y(),v.z()}; }
bool vectorRead(const QJsonValue& value, QVector3D* v) {
    const auto a=value.toArray(); if(a.size()!=3) return false;
    for(int i=0;i<3;++i) { if(!a[i].isDouble()) return false; (*v)[i]=float(a[i].toDouble()); }
    return finite(*v);
}
QQuaternion frame(const ModelConnection& p) {
    const auto n=p.normal.normalized();
    const auto u=(p.up-n*QVector3D::dotProduct(p.up,n)).normalized();
    return QQuaternion::fromAxes(QVector3D::crossProduct(u,n),u,n).normalized();
}
QString cell(int x,int y) { return QString::number(x)+','+QString::number(y); }
}
bool validConnection(const ModelConnection& p) {
    return !p.id.trimmed().isEmpty() && !p.type.trimmed().isEmpty() && finite(p.position)
        && finite(p.normal) && finite(p.up) && p.normal.lengthSquared()>1e-8f
        && p.up.lengthSquared()>1e-8f && QVector3D::crossProduct(p.normal.normalized(),p.up.normalized()).lengthSquared()>1e-6f;
}
bool readConnections(const QJsonValue& value,QVector<ModelConnection>* points,QString* error) {
    QVector<ModelConnection> parsed; QSet<QString> ids;
    if(value.isUndefined()) { points->clear(); return true; }
    bool ok=value.isArray() && value.toArray().size()<=128;
    if(ok) for(const auto& v:value.toArray()) {
        const auto o=v.toObject(); ModelConnection p; p.extra=o;
        p.id=o.value("id").toString(); p.label=o.value("label").toString(); p.type=o.value("type").toString();
        if(!v.isObject() || !vectorRead(o.value("position"),&p.position) || !vectorRead(o.value("normal"),&p.normal)
            || !vectorRead(o.value("up"),&p.up) || !validConnection(p) || ids.contains(p.id)) { ok=false; break; }
        ids.insert(p.id); parsed.append(p);
    }
    if(!ok) { if(error) *error=QString::fromUtf8("Некорректные точки стыковки: нужны уникальные ID, тип, координаты и непараллельные направление/верх."); return false; }
    *points=parsed; return true;
}
QJsonArray writeConnections(const QVector<ModelConnection>& points) {
    QJsonArray a; for(const auto& p:points) { auto o=p.extra;
        o.insert("id",p.id); o.insert("label",p.label); o.insert("type",p.type);
        o.insert("position",vectorJson(p.position)); o.insert("normal",vectorJson(p.normal)); o.insert("up",vectorJson(p.up)); a.append(o); }
    return a;
}
bool connectModelFrames(const ModelConnection& source,const ModelConnection& target,const ConnectionPose& targetPose,ConnectionPose* result) {
    const auto q=targetPose.rotation;
    if(!result || !validConnection(source) || !validConnection(target) || source.type!=target.type || !finite(targetPose.position)
        || !qIsFinite(q.x()) || !qIsFinite(q.y()) || !qIsFinite(q.z()) || !qIsFinite(q.scalar()) || q.lengthSquared()<1e-8f) return false;
    // Face outward normals towards each other while keeping their up axes aligned.
    const auto r=(targetPose.rotation.normalized()*frame(target)*QQuaternion::fromAxisAndAngle(0,1,0,180)*frame(source).conjugated()).normalized();
    result->rotation=r;
    result->position=targetPose.position+targetPose.rotation.rotatedVector(target.position)-r.rotatedVector(source.position);
    return finite(result->position);
}
void ConnectionIndex::clear() { m_cells.clear(); }
void ConnectionIndex::add(const QVector3D& p,int i) { if(finite(p)) m_cells[cell(qFloor(p.x()/8),qFloor(p.y()/8))].append(i); }
QVector<int> ConnectionIndex::queryNear(const QVector3D& p,float radius) const {
    QVector<int> out; if(!finite(p) || !qIsFinite(radius) || radius<=0 || radius>128) return out;
    for(int x=qFloor((p.x()-radius)/8);x<=qFloor((p.x()+radius)/8);++x)
        for(int y=qFloor((p.y()-radius)/8);y<=qFloor((p.y()+radius)/8);++y) out+=m_cells.value(cell(x,y));
    return out;
}
}
