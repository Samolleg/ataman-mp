#include "scenario_panel.h"
#include "view.h"
#include "mob.h"
#include "node.h"
#include "unit.h"
#include "profile_database.h"
#include <QTimer>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QSignalBlocker>
#include <QFontDatabase>
#include <cmath>
#include <limits>

namespace {
QString identity(const QString& source, const QString& object) {
    return scenario::normalizedSource(source) + "|" + object;
}
QString nodeKey(CNode* node) { return QString("node:%1").arg(node->innerId()); }
bool finite(const QVector3D& p) { return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z()); }
}
CScenarioPanel::CScenarioPanel(CView* view, QWidget* parent) : QWidget(parent), m_view(view)
{
    setObjectName("mapScenarioPanel");
    auto* layout=new QVBoxLayout(this);
    auto* explanation=new QLabel(tr("Static scenario links do not prove execution. No references found does not mean unused."),this);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto* controls=new QHBoxLayout;
    m_overlay=new QCheckBox(tr("Show scenario layer"),this); m_overlay->setChecked(true);
    m_selectedOnly=new QCheckBox(tr("Selected participant only"),this); m_selectedOnly->setChecked(true);
    auto* refreshButton=new QPushButton(tr("Refresh"),this);
    controls->addWidget(m_overlay); controls->addWidget(m_selectedOnly); controls->addWidget(refreshButton);
    layout->addLayout(controls);
    auto* legend=new QLabel(tr("Cyan: placed participant · Purple: virtual spawn · Amber: destination / selected links"),this);
    legend->setWordWrap(true); layout->addWidget(legend);
    m_summary=new QLabel(this); m_summary->setWordWrap(true); layout->addWidget(m_summary);
    m_filter=new QLineEdit(this); m_filter->setPlaceholderText(tr("Filter by participant, command, source or status")); layout->addWidget(m_filter);
    auto* splitter=new QSplitter(Qt::Vertical,this);
    m_references=new QTreeWidget(splitter); m_references->setObjectName("scenarioReferences");
    m_references->setColumnCount(6);
    m_references->setHeaderLabels({tr("Command"),tr("Participant / expression"),tr("Source"),tr("Function"),tr("Line"),tr("Status")});
    m_references->setRootIsDecorated(false); m_references->setUniformRowHeights(true);
    m_references->setSelectionMode(QAbstractItemView::SingleSelection);
    m_references->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_references->header()->setSectionResizeMode(QHeaderView::Interactive);
    const int columnWidths[] = {180,230,140,130,65,340};
    for(int c=0;c<6;++c) m_references->setColumnWidth(c,columnWidths[c]);
    m_code=new QPlainTextEdit(splitter); m_code->setObjectName("scenarioSourcePreview"); m_code->setReadOnly(true);
    m_code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    splitter->addWidget(m_references); splitter->addWidget(m_code); splitter->setStretchFactor(0,3); splitter->setStretchFactor(1,2);
    layout->addWidget(splitter,1);
    m_details=new QLabel(this); m_details->setTextFormat(Qt::PlainText); m_details->setWordWrap(true); m_details->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_details);
    auto* navigation=new QHBoxLayout;
    m_toCode=new QPushButton(tr("Go to code"),this); m_toCode->setObjectName("scenarioGoToCode");
    m_targets=new QComboBox(this);
    m_toTarget=new QPushButton(tr("Show participant / marker"),this); m_toTarget->setObjectName("scenarioGoToTarget");
    navigation->addWidget(m_toCode); navigation->addWidget(m_targets,1); navigation->addWidget(m_toTarget); layout->addLayout(navigation);
    m_timer=new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(180);
    connect(m_timer,&QTimer::timeout,this,&CScenarioPanel::refresh);
    connect(refreshButton,&QPushButton::clicked,this,&CScenarioPanel::refresh);
    connect(m_filter,&QLineEdit::textChanged,this,&CScenarioPanel::populate);
    connect(m_selectedOnly,&QCheckBox::toggled,this,&CScenarioPanel::populate);
    connect(m_overlay,&QCheckBox::toggled,this,&CScenarioPanel::updateOverlay);
    connect(m_references,&QTreeWidget::itemSelectionChanged,this,&CScenarioPanel::showReference);
    connect(m_references,&QTreeWidget::itemDoubleClicked,this,[this](){navigateCode();});
    connect(m_toCode,&QPushButton::clicked,this,&CScenarioPanel::navigateCode);
    connect(m_toTarget,&QPushButton::clicked,this,&CScenarioPanel::navigateTarget);
    connect(m_targets,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){m_toTarget->setEnabled(i>=0);});
    if(view) {
        connect(view,&CView::mobStateChanged,this,&CScenarioPanel::scheduleRefresh);
        connect(view,&CView::loadedMapStateChanged,this,&CScenarioPanel::scheduleRefresh);
        connect(view,&CView::mobScriptChanged,this,&CScenarioPanel::scheduleRefresh);
        connect(view,&CView::scenarioBufferChanged,this,&CScenarioPanel::scheduleRefresh);
        connect(view,&CView::selectionChanged,this,[this](){ if(!m_refreshing) { populate(); updateOverlay(); } });
        connect(view,&CView::scenarioSourceLocationRequested,this,&CScenarioPanel::showSourceLocation);
        connect(view,QOverload<CMob*>::of(&CView::unloadMob),this,[this](CMob*){scheduleRefresh();});
    }
    populate();
}
void CScenarioPanel::setFamilyReport(const CMobFamilyValidationReport& report) { m_family=report; scheduleRefresh(); }
void CScenarioPanel::setLayerEnabled(bool enabled) {
    m_enabled=enabled;
    if(enabled) refresh();
    else { m_timer->stop(); if(m_view) m_view->setScenarioOverlay({}, {}, {}, {}); }
}
void CScenarioPanel::scheduleRefresh() {
    if(m_view) m_view->setScenarioOverlay({}, {}, {}, {});
    m_result={}; m_request={}; m_navigationReference=-1;
    populate();
    if(m_enabled) { m_summary->setText(tr("Updating scenario analysis…")); m_timer->start(); }
}
const scenario::Source* CScenarioPanel::source(const QString& key) const {
    for(const auto& s:m_request.sources) if(scenario::normalizedSource(s.key)==scenario::normalizedSource(key)) return &s;
    return nullptr;
}
void CScenarioPanel::refresh() {
    m_timer->stop();
    if(m_refreshing || !m_enabled || !m_view) return;
    if(m_view->hasPendingOperation()) { scheduleRefresh(); return; }
    m_refreshing=true; m_request={}; m_result={};
    CMob* active=m_view->currentMob();
    if(active) {
        m_request.activeSource=active->filePath().absoluteFilePath();
        QSet<QString> loaded;
        for(CMob* mob:m_view->mobs()) {
            if(!mob) continue;
            scenario::Source s; s.key=mob->filePath().absoluteFilePath();
            loaded.insert(scenario::normalizedSource(s.key));
            s.text=m_view->scenarioScriptText(mob,&s.draft);
            for(CNode* node:mob->nodes()) {
                if(!node || node->isMarkDeleted() || (node->nodeType() & eWorldObject)!=eWorldObject) continue;
                scenario::Object o; o.key=nodeKey(node); o.id=node->mapId(); o.name=node->mapName();
                o.category=node->nodeType()==eUnit?tr("NPC"):node->nodeType()==eLever?tr("Lever"):tr("Object");
                o.position=node->drawPosition(); o.hasPosition=finite(o.position); s.objects.append(o);
            }
            m_request.sources.append(s);
        }
        bool activeInReportedFamily=false;
        for(const auto& member:m_family.members) if(scenario::normalizedSource(member.path)==scenario::normalizedSource(m_request.activeSource)) activeInReportedFamily=member.activeRuntimeFamily;
        for(const auto& member:m_family.members) {
            if(member.missing || !member.error.isEmpty()) continue;
            // Only use a reported primary as a root when it is still loaded.
            // A previous map's family cannot survive a close/switch by cache.
            if(activeInReportedFamily && member.activeRuntimeFamily && member.primaryMob && loaded.contains(scenario::normalizedSource(member.path)))
                m_request.familyRoots.append(member.path);
            if(activeInReportedFamily && member.activeRuntimeFamily && member.inclusionReason=="profile-wide autorun")
                m_request.familyRoots.append(member.path);
            if(loaded.contains(scenario::normalizedSource(member.path))) continue;
            scenario::Source s; s.key=member.path; s.text=scenario::normalizedText(member.script);
            for(const auto& obj:member.objects) if(obj.hasNid) {
                scenario::Object o; o.key="raw:"+obj.sectionPath; o.id=obj.nid; o.name=obj.name;
                // Unloaded members participate in identity analysis only. Their
                // coordinates are not silently projected into the open viewport.
                s.objects.append(o);
            }
            m_request.sources.append(s);
        }
        if(m_request.familyRoots.isEmpty()) m_request.familyRoots.append(m_request.activeSource);
        auto database=ei::ProfileDatabaseContext::instance().current();
        if(database) for(auto it=database->monsters.cbegin();it!=database->monsters.cend();++it) m_request.prototypes.insert(it.key());
        m_result=scenario::analyze(m_request);
    }
    m_refreshing=false;
    populate(); updateOverlay();
}
QString CScenarioPanel::selectedKey() const {
    if(!m_view) return QString();
    const auto nodes=m_view->selectedNodes();
    if(nodes.size()!=1) return QString();
    CMob* mob=m_view->ownerMob(nodes[0]);
    return mob?identity(mob->filePath().absoluteFilePath(),nodeKey(nodes[0])):QString();
}
QStringList CScenarioPanel::participantKeys(const scenario::Reference& ref) const {
    QStringList keys;
    if(ref.kind==scenario::Kind::Spawn) keys.append(identity(ref.sourceKey,QString("spawn:%1").arg(ref.offset)));
    for(const auto& target:ref.targets) if(!target.objectKey.isEmpty()) keys.append(identity(target.sourceKey,target.objectKey));
    // A selected object should still show calls for which it is a possible
    // duplicate/incomplete-family candidate. This is filtering, not resolution:
    // those rows retain their warning and never acquire an automatic target.
    for(const auto& target:ref.targets) if(target.hasId && (target.resolution==scenario::Resolution::Ambiguous || target.resolution==scenario::Resolution::Incomplete))
        for(const auto& src:m_request.sources) if(m_result.family.contains(scenario::normalizedSource(src.key)))
            for(const auto& obj:src.objects) if(obj.id==target.id) keys.append(identity(src.key,obj.key));
    return keys;
}
int CScenarioPanel::currentReference() const {
    auto* row=m_references->currentItem();
    return row?row->data(0,Qt::UserRole).toInt():-1;
}
void CScenarioPanel::populate() {
    const int previous=currentReference();
    const QSignalBlocker blocked(m_references);
    m_references->clear();
    const QString selected=selectedKey();
    const QString filter=m_filter->text().trimmed();
    int drafts=0; for(const auto& s:m_request.sources) if(s.draft) ++drafts;
    for(int i=0;i<m_result.references.size();++i) {
        const auto& ref=m_result.references[i];
        // Unrelated sources are inspected for external identities, not presented
        // as scripts belonging to this map.
        if(!m_result.family.contains(scenario::normalizedSource(ref.sourceKey))) continue;
        if(m_selectedOnly->isChecked() && !selected.isEmpty() && !participantKeys(ref).contains(selected)) continue;
        QStringList targets,status;
        if(!ref.spawnName.isEmpty()) targets.append(ref.spawnName+" / "+ref.prototype);
        for(const auto& target:ref.targets) {
            targets.append(target.name.isEmpty()?target.expression:target.name+" · "+target.expression);
            status.append(scenario::resolutionLabel(target.resolution));
        }
        if(!ref.status.isEmpty()) status.append(ref.status);
        if(status.isEmpty()) status.append(tr("Static call; execution not proven"));
        const auto* src=source(ref.sourceKey);
        QString sourceName=QFileInfo(ref.sourceKey).fileName();
        if(src && src->draft) sourceName+=tr(" [unapplied buffer]");
        QStringList columns={ref.command,targets.join("; "),sourceName,ref.function,QString::number(ref.line),status.join("; ")};
        if(!filter.isEmpty() && !columns.join(" ").contains(filter,Qt::CaseInsensitive)) continue;
        auto* row=new QTreeWidgetItem(m_references,columns);
        row->setData(0,Qt::UserRole,i);
        for(int c=0;c<6;++c) row->setToolTip(c,columns[c]);
        if(i==previous) m_references->setCurrentItem(row);
    }
    m_summary->setText(m_request.activeSource.isEmpty()?tr("Open a MOB to inspect its scenario."):
        tr("%1 references shown · %2 virtual markers · %3 unapplied buffers")
            .arg(m_references->topLevelItemCount()).arg(m_result.markers.size()).arg(drafts));
    if(m_references->topLevelItemCount()==0 && !m_request.activeSource.isEmpty())
        m_summary->setText(m_summary->text()+"\n"+tr("No references found in this view. This does not mean unused."));
    if(!m_result.diagnostics.isEmpty()) m_summary->setText(m_summary->text()+"\n"+m_result.diagnostics.mid(0,4).join("; "));
    showReference();
}
void CScenarioPanel::showReference() {
    m_navigationReference=currentReference();
    m_targets->clear(); m_code->clear(); m_details->clear(); m_toCode->setEnabled(false); m_toTarget->setEnabled(false);
    if(m_navigationReference<0 || m_navigationReference>=m_result.references.size()) { updateOverlay(); return; }
    const auto& ref=m_result.references[m_navigationReference];
    const auto* src=source(ref.sourceKey);
    if(!src) return;
    m_code->setPlainText(src->text);
    QTextCursor cursor(m_code->document()); cursor.setPosition(qBound(0,ref.offset,src->text.size())); cursor.select(QTextCursor::LineUnderCursor);
    m_code->setTextCursor(cursor); m_code->centerCursor();
    m_toCode->setEnabled(true);
    QString conditions=ref.conditions.isEmpty()?tr("No enclosing condition identified; execution still unproven"):ref.conditions.join("\nAND\n");
    QString details=tr("%1 · %2:%3 · %4\nConditions: %5").arg(scenario::kindLabel(ref.kind),QFileInfo(ref.sourceKey).fileName()).arg(ref.line).arg(ref.function,conditions);
    if(ref.hasDestination) details+="\n"+(ref.destinationHasZ?tr("Authored XYZ: %1, %2, %3").arg(ref.destination.x()).arg(ref.destination.y()).arg(ref.destination.z()):tr("Authored XY: %1, %2; viewport height comes from the loaded surface").arg(ref.destination.x()).arg(ref.destination.y()));
    if(!ref.status.isEmpty()) details+="\n"+ref.status;
    m_details->setText(details);
    for(int t=0;t<ref.targets.size();++t) if(scenario::navigable(ref.targets[t].resolution)) {
        const auto& target=ref.targets[t];
        if(!target.objectKey.startsWith("raw:")) m_targets->addItem(target.name.isEmpty()?target.expression:target.name,t);
    }
    for(int i=0;i<m_result.markers.size();++i) if(m_result.markers[i].reference==m_navigationReference)
        m_targets->addItem(m_result.markers[i].spawn?tr("Virtual spawn"):tr("Virtual destination"),-i-1);
    m_toTarget->setEnabled(m_targets->count()>0);
    updateOverlay();
}
void CScenarioPanel::navigateCode() {
    const int i=currentReference();
    if(!m_view || i<0 || i>=m_result.references.size()) return;
    const auto ref=m_result.references[i]; const auto* src=source(ref.sourceKey);
    if(!src) return;
    for(CMob* mob:m_view->mobs()) if(scenario::normalizedSource(mob->filePath().absoluteFilePath())==scenario::normalizedSource(ref.sourceKey)) {
        if(!m_view->openScenarioSource(mob,src->text,ref.offset)) { refresh(); m_details->setText(tr("Source changed; references refreshed. Select the call again.")); }
        return;
    }
    m_code->setFocus(); m_details->setText(m_details->text()+"\n"+tr("Unloaded source: read-only preview. Open its MOB to edit."));
}
void CScenarioPanel::navigateTarget() {
    const int i=currentReference();
    if(!m_view || m_view->hasPendingOperation() || i<0 || i>=m_result.references.size() || m_targets->currentIndex()<0) return;
    const auto ref=m_result.references[i]; const int value=m_targets->currentData().toInt();
    if(value<0) {
        const int marker=-value-1; if(marker>=m_result.markers.size()) return;
        auto position=m_result.markers[marker].position;
        if(m_view->scenarioMarkerPosition(&position,m_result.markers[marker].hasZ)) m_view->focusScenarioPosition(position);
        else m_details->setText(m_details->text()+"\n"+tr("No loaded surface for XY marker; no point was invented."));
        return;
    }
    if(value>=ref.targets.size()) return;
    const auto& target=ref.targets[value];
    for(CMob* mob:m_view->mobs()) if(scenario::normalizedSource(mob->filePath().absoluteFilePath())==scenario::normalizedSource(target.sourceKey)) {
        for(CNode* node:mob->nodes()) if(node && nodeKey(node)==target.objectKey && node->mapId()==target.id) {
            if(!m_view->isMobVisible(mob)) { m_details->setText(tr("Participant MOB is hidden. Enable its visibility to navigate.")); return; }
            // Navigation is not an edit or a new history command.
            m_view->restoreSelection({node}); m_view->moveCamToSelectedObjects(); return;
        }
    }
    refresh();
}
void CScenarioPanel::showSourceLocation(CMob* mob, int offset) {
    if(!m_view || !mob || !m_view->mobs().contains(mob)) return;
    emit showRequested(); m_enabled=true; refresh();
    m_selectedOnly->setChecked(false); m_filter->clear();
    const QString key=scenario::normalizedSource(mob->filePath().absoluteFilePath());
    int best=-1, distance=std::numeric_limits<int>::max();
    for(int row=0;row<m_references->topLevelItemCount();++row) {
        auto* item=m_references->topLevelItem(row); const auto& ref=m_result.references[item->data(0,Qt::UserRole).toInt()];
        if(scenario::normalizedSource(ref.sourceKey)!=key) continue;
        const int d=offset>=ref.offset && offset<ref.offset+ref.length?0:qAbs(ref.offset-offset);
        if(d<distance) { best=row; distance=d; }
    }
    if(best>=0) { auto* item=m_references->topLevelItem(best); m_references->setCurrentItem(item); m_references->scrollToItem(item); }
}
void CScenarioPanel::updateOverlay() {
    if(!m_view) return;
    if(!m_enabled || !m_overlay->isChecked()) { m_view->setScenarioOverlay({}, {}, {}, {}); return; }
    QMap<QString,QVector3D> positions;
    for(CMob* mob:m_view->mobs()) if(mob && m_view->isMobVisible(mob)) for(CNode* node:mob->nodes())
        if(node && node->nodeState()!=eHidden && finite(node->drawPosition())) positions.insert(identity(mob->filePath().absoluteFilePath(),nodeKey(node)),node->drawPosition());
    QVector<QVector3D> participants,spawns,destinations,links;
    QSet<QString> seen;
    for(const auto& ref:m_result.references) if(m_result.family.contains(scenario::normalizedSource(ref.sourceKey)))
        for(const auto& target:ref.targets) if(scenario::navigable(target.resolution)) {
            const auto key=identity(target.sourceKey,target.objectKey);
            if(positions.contains(key) && !seen.contains(key)) { seen.insert(key); participants.append(positions[key]); }
        }
    QString focus=selectedKey();
    if(focus.isEmpty() && m_navigationReference>=0 && m_navigationReference<m_result.references.size()) {
        const auto keys=participantKeys(m_result.references[m_navigationReference]); if(!keys.isEmpty()) focus=keys[0];
    }
    for(const auto& marker:m_result.markers) {
        auto position=marker.position;
        if(!m_view->scenarioMarkerPosition(&position,marker.hasZ)) continue;
        const QString key=identity(marker.sourceKey,marker.participantKey);
        if(marker.spawn) { spawns.append(position); positions.insert(key,position); }
    }
    for(const auto& marker:m_result.markers) {
        auto position=marker.position;
        if(!m_view->scenarioMarkerPosition(&position,marker.hasZ)) continue;
        const QString key=identity(marker.sourceKey,marker.participantKey);
        if(!marker.spawn) {
            // Show destination/link geometry only for the selected participant.
            if(key!=focus) continue;
            destinations.append(position);
            if(positions.contains(key)) links << positions[key]+QVector3D(0,0,0.3f) << position+QVector3D(0,0,0.3f);
        }
    }
    if(!focus.isEmpty() && positions.contains(focus)) for(const auto& ref:m_result.references) {
        const auto keys=participantKeys(ref); if(!keys.contains(focus)) continue;
        for(const auto& key:keys) if(key!=focus && positions.contains(key)) links << positions[focus]+QVector3D(0,0,0.3f) << positions[key]+QVector3D(0,0,0.3f);
    }
    m_view->setScenarioOverlay(participants,spawns,destinations,links);
}
