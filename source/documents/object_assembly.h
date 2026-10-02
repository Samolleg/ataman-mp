#ifndef OBJECT_ASSEMBLY_H
#define OBJECT_ASSEMBLY_H
#include <QJsonObject>
#include <QVector3D>
#include <QVector>
namespace ei {
// Editor catalog only. No grouping fields are serialized into the game's MOB.
struct AssemblyPart {
    QJsonObject object;
    QVector3D offset;
    int parentPart = -1;
};
struct ObjectAssembly {
    QString id, name, category, region;
    QVector<AssemblyPart> parts;
    QJsonObject extra;
};
QJsonObject assemblyWorldObject(const QJsonObject& descriptor);
void setAssemblyWorldObject(QJsonObject* descriptor, const QJsonObject& world);
bool readObjectAssembly(const QJsonObject& object, ObjectAssembly* result, QString* error = nullptr);
QJsonObject writeObjectAssembly(const ObjectAssembly& assembly);
}
#endif
