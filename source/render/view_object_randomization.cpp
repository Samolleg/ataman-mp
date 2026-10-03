#include "view.h"
#include "worldobj.h"
#include "mob.h"
#include "property.h"
#include "undo.h"
#include <QPointer>
#include <QRandomGenerator>
#include <cmath>

namespace {
// Store the sampled values once: Redo replays precisely the same result.
class RandomizeObjectsCommand : public QUndoCommand,
                                public IAiGraphPreviewInvalidatingCommand
{
    QPointer<CView> m_view;
    QList<QPointer<CNode>> m_nodes;
    QVector<QVector3D> m_before, m_after;
    void apply(const QVector<QVector3D>& values)
    {
        if(!m_view) return;
        m_view->makeCurrent();
        QSet<CMob*> owners;
        for(int i=0;i<m_nodes.size();++i) {
            auto* node=m_nodes[i].data();
            auto* owner=node ? m_view->ownerMob(node) : nullptr;
            if(!owner) continue;
            node->applyParam(QSharedPointer<IPropertyBase>(
                new prop3D(eObjParam_COMPLECTION,values[i])));
            owners.insert(owner);
        }
        for(auto* owner:owners) m_view->setDurty(owner);
        m_view->viewParameters();
        m_view->update();
    }
public:
    RandomizeObjectsCommand(CView* view,const QList<CNode*>& nodes,
                            const QVector<QVector3D>& before,const QVector<QVector3D>& after)
        : m_view(view),m_before(before),m_after(after)
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
    if(!m_activeMob || !m_pUndoStack) return;
    if(axes<1 || axes>7 || !std::isfinite(strengthPercent)
       || strengthPercent<0 || strengthPercent>100) return;
    if(!m_pendingPasteNodes.isEmpty() || !m_operationBackup.isEmpty()) {
        emit updateMsg(QString::fromUtf8("Завершите текущую операцию перед рандомизацией."));
        return;
    }
    QList<CNode*> nodes;
    QVector<QVector3D> before,after;
    const double strength=strengthPercent/100.0;
    for(auto* node:m_activeMob->nodes()) {
        auto* object=dynamic_cast<CWorldObj*>(node);
        if(!object || object->nodeState()!=eSelect || object->isMarkDeleted()) continue;
        const QVector3D original=object->constitution();
        QVector3D result=original;
        for(int axis=0;axis<3;++axis) {
            if(!std::isfinite(original[axis])) {
                emit updateMsg(QString::fromUtf8("У выделенного объекта некорректный масштаб. Рандомизация отменена."));
                return;
            }
            if(axes & (1<<axis)) {
                const double factor=1.0+(QRandomGenerator::global()->generateDouble()*2.0-1.0)*strength;
                const double value=original[axis]*factor;
                result[axis]=float(value);
                if(!std::isfinite(result[axis])) {
                    emit updateMsg(QString::fromUtf8("Результат масштаба выходит за допустимый диапазон. Рандомизация отменена."));
                    return;
                }
            }
        }
        if(result==original) continue;
        nodes.append(node);before.append(original);after.append(result);
    }
    if(nodes.isEmpty()) return;
    // COMPLECTION recalculates FIG geometry, including its authored forms.
    m_pUndoStack->push(new RandomizeObjectsCommand(this,nodes,before,after));
}
