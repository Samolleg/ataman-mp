#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "operationmanager.h"
#include "profile_database.h"
#include "endurance_document.h"
#include "tree_view.h"
#include "assembly_gallery.h"
#include "resourcemanager.h"
#include "scene.h"
#include "property.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QUndoCommand>
#include <QUuid>
#include <QJsonArray>
#include <QPointer>
#include "model_gallery.h"
#include "resource_catalog.h"
#include "settings.h"
#include <QtMath>

namespace {
class AssemblyTagCommand : public QUndoCommand {
    QPointer<CView> view;QList<QPointer<CNode>> nodes;QVector<QJsonObject> before,after;
    void apply(const QVector<QJsonObject>& state) {
        if(!view)return;CMob* owner=nullptr;
        for(int i=0;i<nodes.size();++i)if(nodes[i] && view->ownerMob(nodes[i])){owner=view->ownerMob(nodes[i]);nodes[i]->setAssemblyTag(state[i]);}
        if(owner)view->setDurty(owner);view->viewParameters();view->update();
    }
public:
    AssemblyTagCommand(CView* v,const QList<CNode*>& members,const QJsonObject& tag,const QString& text):view(v) {
        setText(text);for(auto* n:members){nodes.append(n);before.append(n->assemblyTag());after.append(tag);}
    }
    void undo() override {apply(before);}void redo() override {apply(after);}
};
QString profileCatalogPath() {auto db=ei::ProfileDatabaseContext::instance().current();return ei::TechnicalModelCatalog::pathForDatabase(db?db->sourceArchivePath:QString());}
}
void CView::setAssemblyWorldPosition(CNode* node,const QVector3D& world) {
    auto base=world;base.setZ(0);node->updatePos(base);
    base.setZ(world.z()-node->drawPosition().z());node->updatePos(base);
}
QList<CNode*> CView::assemblySelection(const QList<CNode*>& requested) {
    QList<CNode*> result=requested;
    QHash<CMob*,QSet<QString>> groups;
    for(auto* node:requested)if(node && !node->assemblyTag().isEmpty())groups[ownerMob(node)].insert(node->assemblyTag().value("instanceId").toString());
    for(auto it=groups.begin();it!=groups.end();++it)if(it.key())for(auto* node:it.key()->nodes())
        if(!node->isMarkDeleted() && it.value().contains(node->assemblyTag().value("instanceId").toString()) && !result.contains(node))result.append(node);
    return result;
}
void CView::normalizeAssemblySelection(const QList<CNode*>& before) {
    if(CScene::getInstance()->getMode()!=eEditModeObjects)return;
    QSet<CNode*> old=QSet<CNode*>::fromList(before);
    for(auto* mob:m_aMob)if(mob) {
        QHash<QString,QList<CNode*>> groups;
        for(auto* node:mob->nodes())if(!node->isMarkDeleted() && !node->assemblyTag().isEmpty())groups[node->assemblyTag().value("instanceId").toString()].append(node);
        for(const auto& members:groups) {
            int selected=0,previous=0;for(auto* node:members){selected+=node->nodeState()==eSelect;previous+=old.contains(node);}
            const auto state=(selected>0 && selected>=previous)?eSelect:eDraw;
            for(auto* node:members)if(node->nodeState()!=eHidden)node->setState(state);
        }
    }
}
void CView::groupSelectedAssembly(const QString& name,const QString& catalogId) {
    auto members=selectionSnapshot();if(!m_activeMob || members.isEmpty() || !m_pUndoStack)return;
    for(auto* node:members)if(ownerMob(node)!=m_activeMob)return;
    const QJsonObject tag{{"instanceId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"name",name},{"catalogId",catalogId}};
    m_pUndoStack->push(new AssemblyTagCommand(this,members,tag,QString::fromUtf8("Собрать: ")+name));
}
void CView::disassembleSelection() {
    if(hasPendingOperation())cancelPendingOperation();
    QList<CNode*> members;for(auto* node:assemblySelection(selectionSnapshot()))if(ownerMob(node)==m_activeMob && !node->assemblyTag().isEmpty())members.append(node);
    if(!members.isEmpty())m_pUndoStack->push(new AssemblyTagCommand(this,members,{},QString::fromUtf8("Разобрать сборку")));
}
bool CView::captureSelectedAssembly(const QString& name,ei::ObjectAssembly* output,QString* error) {
    auto fail=[&](const QString& text){if(error)*error=text;return false;};
    if(!m_activeMob || hasPendingOperation() || CScene::getInstance()->getMode()!=eEditModeObjects)
        return fail(QString::fromUtf8("Завершите текущую операцию и выберите детали активного MOB."));
    const auto nodes=selectionSnapshot();if(nodes.isEmpty() || nodes.size()>512)return fail(QString::fromUtf8("Выберите от 1 до 512 деталей."));
    ei::ObjectAssembly a;a.id=QUuid::createUuid().toString(QUuid::WithoutBraces);a.name=name.trimmed();
    QVector3D minimum=nodes.first()->drawPosition(),maximum=minimum;
    QHash<uint,int> ids;
    for(int i=0;i<nodes.size();++i) {
        auto* node=nodes[i];if(ownerMob(node)!=m_activeMob)return fail(QString::fromUtf8("Все детали должны принадлежать активному MOB."));
        if(ids.contains(node->mapId()))return fail(QString::fromUtf8("У выбранных деталей повторяются ID. Сборка не создана."));
        ids.insert(node->mapId(),i);
        for(int j=0;j<3;++j){minimum[j]=qMin(minimum[j],node->drawPosition()[j]);maximum[j]=qMax(maximum[j],node->drawPosition()[j]);}
    }
    QVector3D anchor=(minimum+maximum)*0.5f;anchor.setZ(minimum.z());
    QHash<CNode*,QPair<QByteArray,QByteArray>> templates;
    if(!m_activeMob->rawObjectTemplatesBytes(nodes,&templates))return fail(QString::fromUtf8("Сначала сохраните MOB: нужна полная исходная запись каждой детали."));
    for(auto* node:nodes) {
        ei::AssemblyPart part;part.object=node->toJson();part.offset=node->drawPosition()-anchor;
        QByteArray source,clean;
        if(!m_activeMob->rawObjectTemplateBytes(node,&source,&clean))return fail(QString::fromUtf8("Сначала сохраните MOB: нужна полная исходная запись каждой детали."));
        part.object.insert("_Endurance raw provenance",QJsonObject{
            {"sourceDocumentSha256",QString::fromLatin1(node->rawSourceDocumentSha256().toHex())},
            {"sourceObjectPath",node->rawSourceObjectPath()},{"sourceSubtreeSha256",QString::fromLatin1(node->rawSourceSubtreeSha256().toHex())},
            {"sourceSubtreeBase64",QString::fromLatin1(source.toBase64())},{"cleanSubtreeBase64",QString::fromLatin1(clean.toBase64())}});
        const auto wo=ei::assemblyWorldObject(part.object);
        const uint parent=wo.value("Parent Id").toVariant().toUInt();
        if(wo.contains("Parent Id") && parent!=uint(-1) && parent!=0) {
            if(!ids.contains(parent))return fail(QString::fromUtf8("Родитель одной из деталей вне выделения. Добавьте его в сборку или сначала уберите эту игровую связь."));
            part.parentPart=ids.value(parent);
        }
        a.parts.append(part);
    }
    return ei::readObjectAssembly(ei::writeObjectAssembly(a),output,error);
}
bool CView::beginAssemblyPlacement(const ei::ObjectAssembly& supplied,QString* error,bool groupMembers) {
    ei::ObjectAssembly a;if(!ei::readObjectAssembly(ei::writeObjectAssembly(supplied),&a,error))return false;
    if(!m_activeMob || !m_pUndoStack || !m_pTree || !m_pOp) {if(error)*error=QString::fromUtf8("Откройте активный MOB для размещения сборки.");return false;}
    cancelPendingOperation();QPoint cursor=mapFromGlobal(QCursor::pos());if(!rect().contains(cursor))cursor=rect().center();
    QVector3D anchor;if(!objectPlacementPositionAt(cursor,&anchor)) {if(error)*error=QString::fromUtf8("Наведите мышь на рельеф для размещения сборки.");return false;}
    return beginPreparedAssemblyPlacement(a,cursor,anchor,error,groupMembers);
}
bool CView::beginPreparedAssemblyPlacement(const ei::ObjectAssembly& a,const QPoint& cursor,const QVector3D& anchor,QString* error,bool groupMembers) {
    ei::ObjectAssembly checked;
    if(!ei::readObjectAssembly(ei::writeObjectAssembly(a),&checked,error) || !m_activeMob)return false;
    const auto selection=selectionSnapshot();QList<CNode*> nodes;makeCurrent();
    const QJsonObject tag{{"instanceId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"name",a.name},{"catalogId",a.id}};
    for(const auto& part:a.parts) {
        auto* node=m_activeMob->createNode(part.object);if(!node)break;
        if(groupMembers)node->setAssemblyTag(tag);setAssemblyWorldPosition(node,anchor+part.offset);nodes.append(node);
    }
    for(int i=0;nodes.size()==a.parts.size() && i<nodes.size();++i) {
        auto descriptor=nodes[i]->toJson();auto wo=ei::assemblyWorldObject(descriptor);
        // Parent ID is a gameplay field. Patch it through the ordinary property
        // interface so lossless transactions see exactly the intended change.
        if(!wo.contains("Parent Id"))continue;
        const uint parent=a.parts[i].parentPart>=0?nodes[a.parts[i].parentPart]->mapId():uint(-1);
        QSharedPointer<IPropertyBase> value(new propInt(eObjParam_PARENT_ID,int(parent)));
        nodes[i]->applyParam(value);
    }
    QByteArray bytes;CValidationReport report;
    if(nodes.size()!=a.parts.size() || !m_activeMob->buildValidatedCandidate(&bytes,&report)) {
        for(auto* node:nodes)m_activeMob->discardUncommittedNode(node);
        restoreSelection(selection);
        if(error)*error = nodes.size()!=a.parts.size()
            ? QString::fromUtf8("Объекты не добавлены: не удалось выделить свободные ID в диапазонах семейства MOB. Проверьте диапазоны и доступность MOB. Весь пакет отменён.")
            : QString::fromUtf8("Сборка не размещена:\n")+report.toText();
        return false;
    }
    for(auto* mob:m_aMob)if(mob)mob->clearSelect(true);
    for(auto* node:nodes)node->setState(eSelect);
    collectObjectTreeData();
    m_pendingOperationOwnsPaste=true;m_pendingDatabasePlacement=true;m_pendingAssemblyPlacement=true;
    m_pendingPasteMob=m_activeMob;m_pendingPasteNodes=nodes;m_pendingPasteOriginalSelection=selection;
    m_pendingAssemblyOffsets.clear();for(int i=0;i<nodes.size();++i)m_pendingAssemblyOffsets.insert(nodes[i],a.parts[i].offset);
    m_lastViewportMousePos=cursor;m_hasViewportMousePos=true;m_pOp->changeState(new CMoveAxis(this,EOperateAxisXY));
    viewParameters();emit loadedMapStateChanged();update();return true;
}
void CView::saveSelectionAsAssembly() {
    bool ok=false;const auto name=QInputDialog::getText(this,QString::fromUtf8("Сохранить сборку"),QString::fromUtf8("Название сборки"),QLineEdit::Normal,{},&ok);
    if(!ok)return;ei::ObjectAssembly a;QString error;
    if(!captureSelectedAssembly(name,&a,&error)){QMessageBox::warning(this,QString::fromUtf8("Сборка не создана"),error);return;}
    ei::TechnicalModelCatalog catalog;const auto path=profileCatalogPath();
    if(path.isEmpty() || !catalog.load(path,&error) || !catalog.setAssembly(a,&error) || !catalog.save(&error)) {
        QMessageBox::warning(this,QString::fromUtf8("Сборка не сохранена"),error.isEmpty()?QString::fromUtf8("Настройте database.res профиля."):error);return;
    }
    groupSelectedAssembly(a.name,a.id);openAssemblyCatalog();
}

bool CView::beginGalleryPlacement(const QVector<QPair<QString,QString>>& entries,QString* error)
{
    if(entries.isEmpty() || entries.size()>512)return false;
    ei::ObjectAssembly batch;batch.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    batch.name=QString::fromUtf8("Объекты из витрины");
    const int columns=qCeil(qSqrt(entries.size()));
    const QStringList models=CObjectList::getInstance()->figureList();
    const QStringList textures=CTextureList::getInstance()->textureList();
    for(int i=0;i<entries.size();++i) {
        const auto& entry=entries[i];
        if(!models.contains(entry.first,Qt::CaseInsensitive) || !textures.contains(entry.second,Qt::CaseInsensitive)) {
            if(error)*error=QString::fromUtf8("Модель или текстура недоступна: %1 / %2").arg(entry.first,entry.second);return false;
        }
        CWorldObj defaults;auto data=defaults.toJson();
        data.insert("Model name",entry.first);data.insert("Primary texture",entry.second);
        CWorldObj node(data);ei::AssemblyPart part;CValidationReport report;
        if(!CMob::prepareFreshTechnicalNode(&node,&part.object,&report)) {if(error)*error=report.toText();return false;}
        part.offset=QVector3D((i%columns)*4.0f,(i/columns)*4.0f,0);
        batch.parts.append(part);
    }
    return beginAssemblyPlacement(batch,error,false);
}

void CView::openObjectGallery()
{
    if(!m_activeMob) {emit updateMsg(QString::fromUtf8("Откройте карту с MOB для добавления объектов."));return;}
    ei::TechnicalModelCatalog catalog;QString error;
    const auto generation=ei::ProfileDatabaseContext::instance().generation();
    catalog.load(profileCatalogPath(),&error);
    ei::TechnicalTextureCatalog scan;
    QStringList roots;
    if(auto* option=dynamic_cast<COptStringList*>(settings()->opt(eOptSetResource,"mapRoots")))roots=option->value().toList();
    const auto sources=configuredResourceSourcePaths(settings());
    CModelGallery gallery(CObjectList::getInstance()->figureList(),&catalog,ei::TechnicalModelCatalog::profileKey(roots),
        CTextureList::getInstance()->textureList(),&scan,CModelGallery::resourceThumbnails(sources),this,{}, {},
        CModelGallery::resourceThumbnailNamespace(sources),CModelConnectionEditor::resourceScenes(sources));
    gallery.enableMultiSelection();
    if(!error.isEmpty())gallery.showCatalogError(error);
    if(gallery.exec()!=QDialog::Accepted)return;
    if(generation!=ei::ProfileDatabaseContext::instance().generation()) {emit updateMsg(QString::fromUtf8("Профиль изменился. Откройте витрину заново."));return;}
    if(!beginGalleryPlacement(gallery.selectedEntries(),&error))
        QMessageBox::warning(this,QString::fromUtf8("Не удалось добавить объекты"),error);
}
void CView::openAssemblyCatalog() {
    ei::TechnicalModelCatalog catalog;QString error;const auto path=profileCatalogPath();
    if(path.isEmpty() || !catalog.load(path,&error)){QMessageBox::warning(this,QString::fromUtf8("Каталог сборок"),error.isEmpty()?QString::fromUtf8("Настройте database.res профиля."):error);return;}
    CAssemblyGallery gallery(&catalog,CTextureList::getInstance()->configuredResourceArchivePaths(),this,
        [this](const QString& name,ei::ObjectAssembly* a,QString* error){return captureSelectedAssembly(name,a,error);});
    gallery.setPlacementAvailable(m_activeMob!=nullptr);
    if(gallery.exec()!=QDialog::Accepted)return;
    if(!beginAssemblyPlacement(gallery.selectedAssembly(),&error))QMessageBox::warning(this,QString::fromUtf8("Сборка не размещена"),error);
    setFocus();
}
