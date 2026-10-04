#include "object_randomization_check.h"
#include "view.h"
#include "worldobj.h"
#include "light.h"
#include "mob.h"
#include "property.h"
#include "settings.h"
#include "tree_view.h"
#include "progressview.h"
#include "tile_form.h"
#include "scene.h"
#include <QDataStream>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextStream>
#include <QSignalBlocker>
#include <cmath>

namespace {
class MorphObject:public CWorldObj {
public:
    MorphObject(ei::CFigure* figure) {m_modelName="random_test_figure";updateFigure(figure);}
};
QJsonObject withoutScale(QJsonObject object) {
    auto base=object.value("Base object").toObject();base.remove("Complection");object.insert("Base object",base);return object;
}
QJsonObject withoutRotation(QJsonObject object) {
    auto base=object.value("Base object").toObject();base.remove("Rotation");object.insert("Base object",base);return object;
}
}

int runObjectRandomizationCheck()
{
    QTextStream out(stdout);int failures=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures;};
    // Authored synthetic FIG8: each of its eight shapes has distinct XYZ geometry.
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream<<quint32(0x38474946)<<quint32(1)<<quint32(1)<<quint32(3)<<quint32(3)<<quint32(3)
          <<quint32(0)<<quint32(0)<<quint32(0)<<quint32(0);
    for(int block=0;block<3;++block)for(int morph=0;morph<8;++morph)stream<<0.0f<<0.0f<<0.0f;
    for(int morph=0;morph<8;++morph)stream<<1.0f;
    const QVector<QVector3D> vertices{{-1,0,0},{1,0,0},{0,0,1},{0,0,0}};
    for(int axis=0;axis<3;++axis)for(int morph=0;morph<8;++morph)for(const auto& vertex:vertices) {
        const int bit=axis==0?2:axis==1?1:4;
        stream<<float(vertex[axis]+((morph & bit)?1.0:0.0));
    }
    for(int axis=0;axis<4;++axis)for(int lane=0;lane<4;++lane)stream<<(axis==2?1.0f:0.0f);
    for(int uv=0;uv<3;++uv)stream<<0.0f<<0.0f;
    stream<<quint16(0)<<quint16(1)<<quint16(2);
    for(int vertex=0;vertex<3;++vertex)stream<<quint16(vertex)<<quint16(0)<<quint16(vertex);
    QDataStream input(bytes);input.setByteOrder(QDataStream::LittleEndian);input.setFloatingPointPrecision(QDataStream::SinglePrecision);
    ei::CFigure figure;figure.setName("random_test_figure");
    if(!figure.readData(input))return 2;
    QTemporaryDir scratch;CSettings settings;QUndoStack history;CTreeView tree;
    QTableWidget properties;properties.setColumnCount(2);CProgressView progress;QLineEdit coords;CTileForm tiles;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coords,&tree,&tiles);view.makeCurrent();
    auto* mob=new CMob;mob->setFileName(QFileInfo(scratch.filePath("random.mob")));
    auto* first=new MorphObject(&figure);auto* second=new MorphObject(&figure);auto* untouched=new MorphObject(&figure);auto* light=new CLight;
    first->setMapId(40);second->setMapId(41);untouched->setMapId(42);light->setMapId(43);
    mob->nodes()<<first<<second<<untouched<<light;view.m_aMob<<mob;view.m_activeMob=mob;
    first->applyParam(QSharedPointer<IPropertyBase>(new prop3D(eObjParam_COMPLECTION,{1,2,3})));
    second->applyParam(QSharedPointer<IPropertyBase>(new prop3D(eObjParam_COMPLECTION,{2,3,4})));
    QByteArray baseline;mob->serializeMob(baseline,false);mob->adoptCommittedBytes(baseline);
    {QSignalBlocker block(CScene::getInstance());CScene::getInstance()->changeMode(eEditModeObjects);}
    first->setState(eSelect);second->setState(eSelect);light->setState(eSelect);
    const auto originalFirst=first->toJson(),originalSecond=second->toJson(),originalUntouched=untouched->toJson(),originalLight=light->toJson();
    const auto geometry=first->connectionPreviewVertices();
    for(int mask:{1,2,4,3,5,6,7}) {
        view.randomizeSelectedObjects(mask,25);
        bool bounds=history.count()==1;
        for(auto* object:{first,second}) {
            const QVector3D initial=object==first?QVector3D(1,2,3):QVector3D(2,3,4);
            for(int axis=0;axis<3;++axis) {
                const float value=object->constitution()[axis];
                bounds=bounds && (mask & (1<<axis) ? value>=initial[axis]*.75f && value<=initial[axis]*1.25f : value==initial[axis]);
            }
        }
        check(bounds,QString("axis mask %1: relative25percent bounds and disabled axes exact").arg(mask));
        const auto afterFirst=first->toJson(),afterSecond=second->toJson();
        const auto afterGeometry=first->connectionPreviewVertices();
        check(withoutScale(afterFirst)==withoutScale(originalFirst) && withoutScale(afterSecond)==withoutScale(originalSecond)
              && untouched->toJson()==originalUntouched && light->toJson()==originalLight,"only selected scalable object scale fields change");
        check(afterGeometry!=geometry && first->modelName()=="random_test_figure","FIG8 authored geometry changes through scale without changing identity");
        const QVector3D ratioFirst(first->constitution().x(),first->constitution().y()/2,first->constitution().z()/3);
        const QVector3D ratioSecond(second->constitution().x()/2,second->constitution().y()/3,second->constitution().z()/4);
        check(ratioFirst!=ratioSecond,"objects receive independent random multipliers");
        history.undo();check(first->toJson()==originalFirst && second->toJson()==originalSecond
            && first->connectionPreviewVertices()==geometry && !mob->refreshDirtyFromProjection(),"one Undo restores exact records geometry and clean state");
        history.redo();check(first->toJson()==afterFirst && second->toJson()==afterSecond
            && first->connectionPreviewVertices()==afterGeometry,"one Redo replays exact values and geometry");
        history.undo();history.clear();
    }
    for(const auto& invalid:QVector<QPair<int,double>>{{0,25},{8,25},{7,-1},{7,101},{7,NAN},{7,0}})
        view.randomizeSelectedObjects(invalid.first,invalid.second);
    check(history.count()==0 && first->toJson()==originalFirst,"invalid options and zero strength do not create history");
    first->setState(eDraw);second->setState(eDraw);view.randomizeSelectedObjects();
    check(history.count()==0 && light->toJson()==originalLight,"unselected objects and selected light are untouched");
    auto* missing=new CWorldObj;missing->setMapId(44);missing->setModelName("missing_random_figure");mob->nodes().append(missing);
    missing->setState(eSelect);const auto missingBefore=missing->toJson();
    missing->applyParam(QSharedPointer<IPropertyBase>(new prop3D(eObjParam_COMPLECTION,{-2,0,4})));
    const auto edgeBefore=missing->toJson();view.randomizeSelectedObjects(7,100);
    check(history.count()==1 && missing->constitution().x()>=-4 && missing->constitution().x()<=0
        && missing->constitution().y()==0 && missing->constitution().z()>=0 && missing->constitution().z()<=8,
        "100percent handles signed zero and missing-figure scales without crashing");
    history.undo();check(missing->toJson()==edgeBefore,"missing-figure scale exact Undo");
    history.clear();missing->setState(eDraw);first->setState(eSelect);second->setState(eSelect);
    // Start away from identity, including a near-gimbal orientation. A pure
    // world-axis delta must remain pure even if its Euler representation folds.
    first->setRot(QQuaternion::fromEulerAngles(89.9f,34.0f,-61.0f));
    second->setRot(QQuaternion::fromEulerAngles(-37.0f,73.0f,125.0f));
    mob->serializeMob(baseline,false);mob->adoptCommittedBytes(baseline);
    const auto rotationFirst=first->toJson(),rotationSecond=second->toJson();
    for(int axis:{0,1}) for(double degrees:{25.0,180.0}) {
        const QQuaternion beforeFirst=first->rotation(),beforeSecond=second->rotation();
        view.randomizeSelectedObjectRotation(axis,degrees);
        bool bounded=history.count()==1;
        QVector<QQuaternion> deltas;
        for(auto* object:{first,second}) {
            const auto before=object==first?beforeFirst:beforeSecond;
            const auto after=object->rotation();
            const QQuaternion delta=(after*before.conjugated()).normalized();deltas.append(delta);
            const double angle=2.0*std::acos(qBound(-1.0,double(std::abs(delta.scalar())),1.0))*180.0/3.141592653589793;
            bounded=bounded && std::abs(after.lengthSquared()-1.0f)<.00001f && angle<=degrees+.001
                && std::abs(delta.z())<.00001f
                && std::abs(axis==0?delta.y():delta.x())<.00001f;
        }
        check(bounded,QString("rotation %1 +/- %2 degrees: pure world axis, unit orientation, one history entry").arg(axis==0?"X":"Y").arg(degrees));
        const auto afterFirst=first->toJson(),afterSecond=second->toJson();
        check(withoutRotation(afterFirst)==withoutRotation(rotationFirst)
            && withoutRotation(afterSecond)==withoutRotation(rotationSecond)
            && untouched->toJson()==originalUntouched && light->toJson()==originalLight
            && missing->toJson()==edgeBefore,"rotation preserves scale position IDs and non-target records");
        check(deltas[0]!=deltas[1],"rotation samples independent angles per object");
        history.undo();check(first->rotation()==beforeFirst && second->rotation()==beforeSecond
            && first->toJson()==rotationFirst && second->toJson()==rotationSecond
            && !mob->refreshDirtyFromProjection(),"rotation Undo restores exact authored quaternions and clean state");
        history.redo();check(first->toJson()==afterFirst && second->toJson()==afterSecond,"rotation Redo replays sampled orientations exactly");
        history.undo();history.clear();
    }
    for(const auto& invalid:QVector<QPair<int,double>>{{-1,25},{2,25},{0,-1},{1,181},{0,NAN},{0,0}})
        view.randomizeSelectedObjectRotation(invalid.first,invalid.second);
    check(history.count()==0 && first->toJson()==rotationFirst,"invalid rotation axis/range and zero degrees are no-op");
    view.m_operationBackup.insert(first,QVector3D());view.randomizeSelectedObjectRotation(0,25);
    check(history.count()==0 && first->toJson()==rotationFirst,"unfinished transform blocks random rotation");view.m_operationBackup.clear();
    const auto validSecond=second->rotation();second->setRot(QQuaternion(0,0,0,0));
    view.randomizeSelectedObjectRotation(1,25);
    check(history.count()==0 && first->toJson()==rotationFirst,"invalid later quaternion rejects whole batch without changing earlier object");
    second->setRot(validSecond);first->setState(eDraw);second->setState(eDraw);view.randomizeSelectedObjectRotation(0,25);
    check(history.count()==0 && light->toJson()==originalLight,"rotation ignores unselected objects and selected light");
    history.clear();out<<(failures?"FAIL":"PASS")<<": object randomization checks\n";return failures?1:0;
}
