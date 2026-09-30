#include "group_rotation_check.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "unit.h"
#include "particle.h"
#include "tree_view.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "scene.h"
#include "operationmanager.h"
#include "endurance_document.h"
#include <QTemporaryDir>
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextStream>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QFile>

namespace {
class FixtureWorldObject : public CWorldObj {
public:
    // Populate serialized scale without loading an installed model resource.
    void setFixtureScale(const QVector3D& scale) { m_complection=scale; }
};
bool positionNear(const QVector3D& a, const QVector3D& b) { return (a-b).length()<0.002f; }
bool sameRotation(CNode* node, const QQuaternion& rotation) {
    const auto actual=QQuaternion::fromEulerAngles(node->getEulerRotation());
    return qAbs(QQuaternion::dotProduct(actual.normalized(),rotation.normalized()))>0.99999f;
}
}
int runGroupRotationCheck()
{
    QTextStream out(stdout); int failures=0;
    auto check=[&](bool ok,const char* name) { out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures; };
    QTemporaryDir scratch;
    if(!scratch.isValid()) return 1;
    CSettings settings; QUndoStack history; CTreeView tree; QTableWidget properties;
    properties.setColumnCount(2); CProgressView progress; QLineEdit coordinates; CTileForm tiles;
    CView view(nullptr,nullptr); view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    auto* mob=new CMob; mob->setFileName(QFileInfo(scratch.filePath("active.mob")));
    auto* a=new CWorldObj; auto* b=new FixtureWorldObject; auto* c=new CUnit;
    a->setMapId(81); b->setMapId(81); c->setMapId(83); // Identity must not depend on map ID.
    c->logicAt(0)->createPatrolByIndex(-1);
    c->logicAt(0)->createPatrolByIndex(0);
    auto* unsupported=new CParticle;
    mob->nodes()<<a<<b<<c<<unsupported;
    view.m_aMob.append(mob); view.m_activeMob=mob;
    auto* lockedMob=new CMob; lockedMob->setFileName(QFileInfo(scratch.filePath("locked.mob")));
    auto* locked=new CWorldObj; lockedMob->nodes()<<locked; view.m_aMob.append(lockedMob);
    const QVector3D initial[]={{8,20,3},{12,20,5},{9,22,4}};
    const QVector3D center(10,21,4); // Midpoint of selection origin bounds, independent of order.
    const QQuaternion orientation=QQuaternion::fromEulerAngles(25,35,15);
    QVector3D scale(2,3,0.5f); b->setFixtureScale(scale);
    auto reset=[&]() {
        history.clear();
        { const QSignalBlocker guard(CScene::getInstance()); CScene::getInstance()->changeMode(eEditModeObjects); }
        int i=0; for(CNode* node : {static_cast<CNode*>(a),static_cast<CNode*>(b),static_cast<CNode*>(c)}) {
            auto position=initial[i++]; node->updatePos(position); node->setRot(orientation); node->setState(eSelect);
        }
        QVector3D farPosition(1000,2000,3000); unsupported->updatePos(farPosition); unsupported->setState(eSelect);
        locked->updatePos(farPosition); locked->setState(eSelect);
    };
    reset();
    const auto unitBefore=c->movementState();
    view.m_pOp->keyPress(eKey_R); view.m_pOp->keyPress(eKey_9); view.m_pOp->keyPress(eKey_0);
    const auto z=QQuaternion::fromAxisAndAngle(0,0,1,90);
    check(positionNear(a->position(),center+z.rotatedVector(initial[0]-center)) &&
          positionNear(b->position(),center+z.rotatedVector(initial[1]-center)) &&
          positionNear(c->position(),center+z.rotatedVector(initial[2]-center)),"R numeric 90 rotates all positions around one fixed center");
    check(sameRotation(a,z*orientation) && sameRotation(b,z*orientation),"world axis composes orientations even for tilted objects");
    check(qAbs((a->position()-b->position()).length()-(initial[0]-initial[1]).length())<0.002f,"group distances preserved");
    view.m_pOp->keyPress(eKey_Esc);
    check(positionNear(a->position(),initial[0]) && positionNear(b->position(),initial[1]) && sameRotation(a,orientation) && c->movementState()==unitBefore && history.count()==0,"Esc restores positions orientations and unit logic without history");
    view.m_pOp->keyPress(eKey_R); view.m_pOp->keyPress(eKey_9); view.m_pOp->keyPress(eKey_0);
    view.m_pOp->keyPress(eKey_Enter);
    check(history.count()==1,"group commit is one undo step");
    check(positionNear(a->position(),center+z.rotatedVector(initial[0]-center)),"commit retains position preview");
    history.undo(); check(positionNear(a->position(),initial[0]) && positionNear(b->position(),initial[1]) && sameRotation(a,orientation) && c->movementState()==unitBefore,"one undo restores whole group including unit logic");
    history.redo(); check(positionNear(a->position(),center+z.rotatedVector(initial[0]-center)) && sameRotation(a,z*orientation),"one redo restores group transform");
    check(positionNear(unsupported->position(),{1000,2000,3000}) && positionNear(locked->position(),{1000,2000,3000}),"unsupported and locked nodes excluded from pivot and transformation");
    check(b->constitution()==scale,"rotation preserves nonuniform object scale");
    for(int axis=0;axis<3;++axis) {
        reset(); view.operationSetBackup(EOperationAxisType::eRotate);
        QVector3D delta; delta[axis]=15;
        for(int i=0;i<6;++i) view.rotateTo(delta);
        QVector3D unitAxis; unitAxis[axis]=1;
        auto rotation=QQuaternion::fromAxisAndAngle(unitAxis,90);
        check(positionNear(a->position(),center+rotation.rotatedVector(initial[0]-center)) && sameRotation(a,rotation*orientation),"incremental preview preserves fixed pivot and orientation on each axis");
        view.operationRevert(EOperationAxisType::eRotate);
        check(positionNear(a->position(),initial[0]) && c->movementState()==unitBefore,"revert restores every axis");
    }
    reset(); view.m_pOp->keyPress(eKey_R); view.m_pOp->keyPress(eKey_9); view.m_pOp->keyPress(eKey_0);
    view.m_pOp->keyPress(eKey_X);
    check(positionNear(a->position(),initial[0]) && sameRotation(a,orientation),"axis switch discards previous preview");
    view.m_pOp->keyPress(eKey_9); view.m_pOp->keyPress(eKey_0); view.m_pOp->keyPress(eKey_Backspace); view.m_pOp->keyPress(eKey_Backspace);
    check(positionNear(a->position(),initial[0]) && sameRotation(a,orientation),"clearing numeric input restores initial transform");
    view.m_pOp->keyPress(eKey_Esc);
    reset(); b->setState(eDraw); c->setState(eDraw); view.operationSetBackup(EOperationAxisType::eRotate);
    QVector3D delta(0,0,90); view.rotateTo(delta);
    check(positionNear(a->position(),initial[0]),"single object keeps its own pivot");
    view.operationRevert(EOperationAxisType::eRotate);
    reset();
    const QPoint anchor=view.operationAnchor();
    view.m_pOp->keyPress(eKey_R);
    QMouseEvent move1(QEvent::MouseMove,anchor+QPoint(450,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QMouseEvent move2(QEvent::MouseMove,anchor+QPoint(900,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    view.m_pOp->mouseMoveEvent(&move1); view.m_pOp->mouseMoveEvent(&move2);
    check(positionNear(a->position(),center+z.rotatedVector(initial[0]-center)),"R mouse movement uses same group pivot");
    QMouseEvent release(QEvent::MouseButtonRelease,anchor+QPoint(900,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    view.m_pOp->mouseReleaseEvent(&release);
    check(history.count()==1 && positionNear(a->position(),center+z.rotatedVector(initial[0]-center)),"left click commits mouse gesture as one history step");
    // Exercise actual binary serialization and reopen using only a temporary MOB.
    b->setMapId(82);
    QByteArray serialized; mob->serializeMob(serialized,false);
    QFile file(scratch.filePath("rotated.mob")); bool written=file.open(QIODevice::WriteOnly);
    if(written) { written=file.write(serialized)==serialized.size(); file.close(); }
    QFile input(file.fileName());
    const bool opened=input.open(QIODevice::ReadOnly);
    const auto saved=opened?input.readAll():QByteArray(); input.close();
    CMobDocument document; CValidationReport report; CMob reopened;
    bool roundtrip=written && opened && document.loadBytes(saved,"rotated.mob",&report)
        && reopened.deserialize(saved,&document);
    CNode* restored=reopened.nodeByMapId(a->mapId());
    check(roundtrip && restored && positionNear(restored->position(),a->position()) && sameRotation(restored,z*orientation),"temporary MOB serialization and fresh reopen preserve group position and orientation");
    history.undo(); check(positionNear(a->position(),initial[0]) && c->movementState()==unitBefore,"mouse gesture undo restores baseline");
    reset(); view.m_pOp->keyPress(eKey_R); view.m_pOp->keyPress(eKey_Minus); view.m_pOp->keyPress(eKey_9); view.m_pOp->keyPress(eKey_0);
    check(positionNear(a->position(),center+z.conjugated().rotatedVector(initial[0]-center)),"negative numeric angle rotates group in opposite direction");
    view.m_pOp->keyPress(eKey_Esc);
    reset(); a->setState(eDraw); b->setState(eDraw); c->setState(eDraw); unsupported->setState(eDraw);
    view.m_pOp->keyPress(eKey_R); view.m_pOp->keyPress(eKey_9); view.m_pOp->keyPress(eKey_0); view.m_pOp->keyPress(eKey_Enter);
    check(history.count()==0 && positionNear(a->position(),initial[0]),"empty editable selection is a no-op");
    out<<"group-rotation-check: "<<failures<<" failures\n"; return failures?1:0;
}
