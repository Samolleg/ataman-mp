#include "view.h"
#include "worldobj.h"
#include "mob.h"
#include "property.h"
#include "undo.h"
#include <QPointer>
#include <QRandomGenerator>
#include <cmath>

namespace {
struct RandomObjectState {
    QVector3D scale;
    QQuaternion rotation;
};
// One transaction owns sampled values, not the current selection or settings.
class RandomizeObjectsCommand : public QUndoCommand,
                                public IAiGraphPreviewInvalidatingCommand
{
    QPointer<CView> m_view;
    CMob* m_owner;
    QList<QPointer<CNode>> m_nodes;
    QVector<RandomObjectState> m_before, m_after;
    void apply(const QVector<RandomObjectState>& values)
    {
        if(!m_view || !m_view->mobs().contains(m_owner)) return;
        m_view->makeCurrent();
        // One membership index per batch instead of a full MOB search per node.
        const auto& ownerNodes=m_owner->nodes();
        const QSet<CNode*> available=QSet<CNode*>::fromList(ownerNodes);
        bool changed=false;
        for(int i=0;i<m_nodes.size();++i) {
            auto* node=m_nodes[i].data();
            if(!node || !available.contains(node)) continue;
            const bool scaleChanged=m_before[i].scale!=m_after[i].scale;
            const bool rotationChanged=m_before[i].rotation!=m_after[i].rotation;
            if(rotationChanged) {
                if(scaleChanged) {
                    // recalcFigure below rebuilds bounds/projection once with
                    // the final rotation and scale, avoiding old-mesh work.
                    node->CNode::setRot(values[i].rotation);
                } else node->setRot(values[i].rotation);
            }
            if(scaleChanged)
                node->applyParam(QSharedPointer<IPropertyBase>(
                    new prop3D(eObjParam_COMPLECTION,values[i].scale)));
            changed=true;
        }
        if(!changed) return;
        m_view->setDurty(m_owner);
        m_view->viewParameters();
        m_view->update();
    }
public:
    RandomizeObjectsCommand(CView* view,CMob* owner,const QList<CNode*>& nodes,
                            const QVector<RandomObjectState>& before,
                            const QVector<RandomObjectState>& after)
        :m_view(view),m_owner(owner),m_before(before),m_after(after)
    {
        setText(QString::fromUtf8("Рандом: %1 объектов").arg(nodes.size()));
        for(auto* node:nodes) m_nodes.append(node);
    }
    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }
    bool invalidatesAiGraphPreview() const override { return true; }
};
}

void CView::randomizeSelectedObjects(int axes,double strengthPercent)
{
    if(axes<1 || axes>7) return;
    randomizeSelectedObjectsCombined(axes,strengthPercent,false,false,0);
}

void CView::randomizeSelectedObjectRotation(int axis,double maximumDegrees)
{
    if(axis<0 || axis>1) return;
    randomizeSelectedObjectsCombined(0,0,axis==0,axis==1,maximumDegrees);
}

void CView::randomizeSelectedObjectsCombined(int axes,double strengthPercent,
                                            bool rotateX,bool rotateY,double maximumDegrees)
{
    if(!m_activeMob || !m_pUndoStack || axes<0 || axes>7
       || !std::isfinite(strengthPercent) || strengthPercent<0 || strengthPercent>100
       || !std::isfinite(maximumDegrees) || maximumDegrees<0 || maximumDegrees>180)
        return;
    const bool scale=axes!=0 && strengthPercent>0;
    const bool rotation=(rotateX || rotateY) && maximumDegrees>0;
    if(!scale && !rotation) return;
    if(!m_pendingPasteNodes.isEmpty() || !m_operationBackup.isEmpty()) {
        emit updateMsg(QString::fromUtf8("Завершите текущую операцию перед рандомизацией."));
        return;
    }
    QList<CNode*> nodes;
    QVector<RandomObjectState> before,after;
    const double strength=strengthPercent/100.0;
    for(auto* node:m_activeMob->nodes()) {
        auto* object=dynamic_cast<CWorldObj*>(node);
        if(!object || object->nodeState()!=eSelect || object->isMarkDeleted()) continue;
        const RandomObjectState original{object->constitution(),object->rotation()};
        RandomObjectState result=original;
        if(scale) for(int axis=0;axis<3;++axis) {
            if(!std::isfinite(original.scale[axis])) {
                emit updateMsg(QString::fromUtf8("У выделенного объекта некорректный масштаб. Рандомизация отменена."));
                return;
            }
            if(axes & (1<<axis)) {
                const double factor=1.0+(QRandomGenerator::global()->generateDouble()*2.0-1.0)*strength;
                result.scale[axis]=float(original.scale[axis]*factor);
                if(!std::isfinite(result.scale[axis])) {
                    emit updateMsg(QString::fromUtf8("Результат масштаба выходит за допустимый диапазон. Рандомизация отменена."));
                    return;
                }
            }
        }
        if(rotation) {
            const auto& q=original.rotation;
            if(!std::isfinite(q.scalar()) || !std::isfinite(q.x()) || !std::isfinite(q.y())
               || !std::isfinite(q.z()) || !std::isfinite(q.lengthSquared()) || q.lengthSquared()<=0) {
                emit updateMsg(QString::fromUtf8("У выделенного объекта некорректное вращение. Рандомизация отменена."));
                return;
            }
            result.rotation=q.normalized();
            // Fixed world-axis composition: X first, then Y, about own origin.
            for(int axis=0;axis<2;++axis) if(axis==0?rotateX:rotateY) {
                const float degrees=float((QRandomGenerator::global()->generateDouble()*2.0-1.0)*maximumDegrees);
                const QVector3D direction=axis==0?QVector3D(1,0,0):QVector3D(0,1,0);
                result.rotation=(QQuaternion::fromAxisAndAngle(direction,degrees)*result.rotation).normalized();
            }
        }
        if(result.scale==original.scale && result.rotation==original.rotation) continue;
        nodes.append(node);before.append(original);after.append(result);
    }
    if(nodes.isEmpty()) return;
    // All validation completes before any object is changed.
    m_pUndoStack->push(new RandomizeObjectsCommand(this,m_activeMob,nodes,before,after));
}
