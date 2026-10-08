#include "diplomacy_dialog.h"
#include "window_geometry.h"
#include "ui_theme.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QTreeWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTabWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QHideEvent>
#include <algorithm>

namespace {
void setupTree(QTreeWidget* tree,const QStringList& columns) {
    tree->setHeaderLabels(columns); tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true); tree->setAlternatingRowColors(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->header()->setSectionResizeMode(QHeaderView::Interactive);
}
QString groupText(int group) { return group<0?QObject::tr("Unknown"):QString::number(group); }
QStringList possibleGroups(const diplomacy::Npc& npc) {
    QList<int> sorted=npc.possibleGroups.values(); std::sort(sorted.begin(),sorted.end());
    QStringList values; for(int group:sorted) values.append(QString::number(group)); return values;
}
void addRow(QTreeWidget* tree,const QStringList& text,const QString& source) {
    auto* row=new QTreeWidgetItem(tree,text);
    for(int c=0;c<text.size();++c) row->setToolTip(c,text[c]);
    row->setToolTip(text.size()-1,text.last()+"\n"+source);
}
}
CDiplomacyDialog::CDiplomacyDialog(Snapshot snapshot,QWidget* parent)
    : QDialog(parent), m_snapshot(std::move(snapshot))
{
    setObjectName("diplomacyInspector"); setWindowTitle(tr("Diplomacy"));
    setAttribute(Qt::WA_QuitOnClose,false); resize(1280,720);
    ei::rememberWindowGeometry(this,QStringLiteral("diplomacyInspector"));
    auto* layout=new QVBoxLayout(this);
    auto* controls=new QHBoxLayout;
    controls->addWidget(new QLabel(tr("MOB:"),this));
    m_sources=new QComboBox(this); m_sources->setObjectName("diplomacySourceFilter");
    m_sources->setMinimumContentsLength(24); controls->addWidget(m_sources,1);
    auto* refreshButton=new QPushButton(tr("Refresh"),this); controls->addWidget(refreshButton);
    layout->addLayout(controls);
    m_summary=new QLabel(this); m_summary->setObjectName("diplomacySummary");
    m_summary->setTextFormat(Qt::PlainText); m_summary->setWordWrap(true); layout->addWidget(m_summary);
    auto* notice=new QLabel(tr("MOB counts show initial membership. Script spawns and SetPlayer calls are possible changes; loops and conditions can change runtime counts. Script groups (GroupAdd / GroupSub) are separate from diplomacy groups."),this);
    notice->setWordWrap(true); layout->addWidget(notice);
    m_filter=new QLineEdit(this); m_filter->setObjectName("diplomacyNpcFilter");
    m_filter->setPlaceholderText(tr("Search NPC, ID, prototype or script")); layout->addWidget(m_filter);
    auto* split=new QSplitter(this);
    m_groups=new QTreeWidget(split); m_groups->setObjectName("diplomacyGroups");
    setupTree(m_groups,{tr("Group"),tr("NPCs in\nMOB"),tr("SetPlayer\ncalls")});
    m_groups->setMinimumWidth(330);
    m_groups->header()->setStretchLastSection(false);
    m_groups->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    m_groups->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    m_groups->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    m_tabs=new QTabWidget(split);
    m_npcs=new QTreeWidget(m_tabs); m_npcs->setObjectName("diplomacyNpcs");
    setupTree(m_npcs,{tr("NPC / prototype"),tr("ID"),tr("Initial\ngroup"),tr("Script\ngroups"),tr("Source")});
    m_npcs->setColumnWidth(0,225); m_npcs->setColumnWidth(1,65);
    m_npcs->setColumnWidth(2,125); m_npcs->setColumnWidth(3,150);
    m_tabs->addTab(m_npcs,tr("NPCs"));
    m_scripts=new QTreeWidget(m_tabs); m_scripts->setObjectName("diplomacyScripts");
    setupTree(m_scripts,{tr("Command / participant"),tr("Group"),tr("Source / line"),tr("Conditions / status")});
    m_scripts->setColumnWidth(0,285); m_scripts->setColumnWidth(1,70); m_scripts->setColumnWidth(2,160);
    m_tabs->addTab(m_scripts,tr("Scripts"));
    auto* relationPage=new QWidget(m_tabs); auto* relationLayout=new QVBoxLayout(relationPage);
    m_relationNotice=new QLabel(relationPage); m_relationNotice->setTextFormat(Qt::PlainText);
    m_relationNotice->setWordWrap(true); relationLayout->addWidget(m_relationNotice);
    m_relations=new QTableWidget(relationPage); m_relations->setObjectName("diplomacyRelations");
    m_relations->setEditTriggers(QAbstractItemView::NoEditTriggers);
    relationLayout->addWidget(m_relations,1);
    m_edit=new QPushButton(tr("Edit relations…"),relationPage); m_edit->setObjectName("diplomacyEditRelations");
    relationLayout->addWidget(m_edit); m_tabs->addTab(relationPage,tr("Relations"));
    split->addWidget(m_groups); split->addWidget(m_tabs); split->setStretchFactor(1,1);
    split->setSizes({400,840}); layout->addWidget(split,1);
    auto* closeButton=new QPushButton(tr("Close"),this);
    closeButton->setAutoDefault(false); layout->addWidget(closeButton,0,Qt::AlignRight);
    m_timer=new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(250);
    m_worker=new QFutureWatcher<diplomacy::Result>(this);
    connect(m_worker,&QFutureWatcher<diplomacy::Result>::finished,this,[this] {
        m_workerPending=false;
        if(m_workerGeneration==m_generation && isVisible()) {
            m_result=m_worker->result(); populateSources(); populateGroups();
        } else if(isVisible() && !m_timer->isActive()) m_timer->start();
    });
    connect(m_timer,&QTimer::timeout,this,&CDiplomacyDialog::refresh);
    connect(refreshButton,&QPushButton::clicked,this,&CDiplomacyDialog::invalidate);
    connect(closeButton,&QPushButton::clicked,this,&QDialog::close);
    connect(m_sources,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&CDiplomacyDialog::populateGroups);
    connect(m_groups,&QTreeWidget::itemSelectionChanged,this,&CDiplomacyDialog::populateDetails);
    connect(m_filter,&QLineEdit::textChanged,this,&CDiplomacyDialog::populateDetails);
    connect(m_tabs,&QTabWidget::currentChanged,this,&CDiplomacyDialog::populateRelations);
    connect(m_edit,&QPushButton::clicked,this,[this] {
        if(const auto* src=relationSource()) if(src->loaded) emit editRelationsRequested(src->key);
    });
}
bool CDiplomacyDialog::busy() const { return m_workerPending || m_timer->isActive(); }
void CDiplomacyDialog::showEvent(QShowEvent* event) { QDialog::showEvent(event); invalidate(); }
void CDiplomacyDialog::hideEvent(QHideEvent* event) {
    ++m_generation; m_timer->stop(); QDialog::hideEvent(event);
}
void CDiplomacyDialog::invalidate() {
    ++m_generation;
    // Clear stale source identities immediately; a removed MOB must not remain
    // navigable or countable while its replacement snapshot is being analyzed.
    m_result={}; m_groups->clear(); m_npcs->clear(); m_scripts->clear(); m_edit->setEnabled(false);
    m_relations->clear(); m_relations->setRowCount(0); m_relations->setColumnCount(0);
    if(isVisible()) { m_summary->setText(tr("Updating diplomacy…")); m_timer->start(); }
}
void CDiplomacyDialog::refresh() {
    if(!isVisible() || m_workerPending) return;
    m_request=m_snapshot(); m_workerGeneration=m_generation;
    const auto request=m_request;
    // The worker may finish before its GUI-thread completion is delivered.
    // Remain busy until that completion has installed (or rejected) the result.
    m_workerPending=true;
    m_worker->setFuture(QtConcurrent::run([request] { return diplomacy::inspectConnected(request); }));
}
bool CDiplomacyDialog::includesSource(const QString& key) const {
    return m_sources->currentData().toString().isEmpty() || m_sources->currentData().toString()==key;
}
void CDiplomacyDialog::populateSources() {
    const auto previous=m_sources->currentData().toString(); const QSignalBlocker blocker(m_sources);
    m_sources->clear(); m_sources->addItem(tr("All connected MOBs"),QString());
    for(const auto& src:m_result.sources) {
        const QString label=QFileInfo(src.key).fileName()+(src.loaded?tr(" [loaded]"):tr(" [AddMob / profile]"))
            +(src.draft?tr(" [unapplied script]"):QString());
        m_sources->addItem(label,src.key); m_sources->setItemData(m_sources->count()-1,src.key+"\n"+src.reason,Qt::ToolTipRole);
    }
    m_sources->setCurrentIndex(qMax(0,m_sources->findData(previous)));
}
void CDiplomacyDialog::populateGroups() {
    const int previous=m_groups->currentItem()?m_groups->currentItem()->data(0,Qt::UserRole).toInt():-2;
    const QSignalBlocker blocker(m_groups); m_groups->clear();
    QMap<int,int> counts,calls; QMap<int,QStringList> names;
    int placed=0,spawns=0,mobs=0;
    for(const auto& src:m_result.sources) if(includesSource(src.key)) {
        ++mobs;
        for(const auto& npc:src.npcs) if(npc.scripted) ++spawns;
            else { ++placed; ++counts[npc.group]; }
        for(int g=0;g<src.groupNames.size() && g<32;++g) {
            const QString name=src.groupNames[g];
            if(!name.isEmpty() && !names[g].contains(name)) names[g].append(name);
        }
    }
    for(const auto& event:m_result.events) if(includesSource(event.reference.sourceKey)
        && event.reference.command=="SetPlayer") ++calls[event.group];
    auto* all=new QTreeWidgetItem(m_groups,{tr("All groups"),QString::number(placed),QString::number([&]{int n=0;for(int v:calls)n+=v;return n;}())});
    all->setData(0,Qt::UserRole,-2);
    for(int g=0;g<32;++g) {
        const QString title=QString::number(g)+(names[g].isEmpty()?QString():" — "+names[g].join(" / "));
        auto* row=new QTreeWidgetItem(m_groups,{title,QString::number(counts[g]),QString::number(calls[g])});
        row->setData(0,Qt::UserRole,g); row->setToolTip(0,title);
    }
    for(int g:counts.keys()+calls.keys()) if((g<0 || g>=32) && g!=-2) {
        bool exists=false; for(int r=0;r<m_groups->topLevelItemCount();++r)
            if(m_groups->topLevelItem(r)->data(0,Qt::UserRole).toInt()==g) exists=true;
        if(!exists) {
            auto* row=new QTreeWidgetItem(m_groups,{g<0?tr("Unknown group"):tr("Invalid group %1").arg(g),QString::number(counts[g]),QString::number(calls[g])});
            row->setData(0,Qt::UserRole,g);
        }
    }
    m_groups->setCurrentItem(all);
    for(int r=0;r<m_groups->topLevelItemCount();++r)
        if(m_groups->topLevelItem(r)->data(0,Qt::UserRole).toInt()==previous) m_groups->setCurrentItem(m_groups->topLevelItem(r));
    QString summary=tr("%1 NPCs in %2 MOBs · %3 script spawn sites").arg(placed).arg(mobs).arg(spawns);
    if(!m_result.diagnostics.isEmpty()) summary+="\n"+tr("Incomplete inspection: %1").arg(m_result.diagnostics.join("; "));
    m_summary->setText(summary); populateDetails(); populateRelations();
}
void CDiplomacyDialog::populateDetails() {
    const int group=m_groups->currentItem()?m_groups->currentItem()->data(0,Qt::UserRole).toInt():-2;
    const QString filter=m_filter->text().trimmed();
    m_npcs->setUpdatesEnabled(false); m_scripts->setUpdatesEnabled(false);
    m_npcs->clear(); m_scripts->clear();
    for(const auto& src:m_result.sources) if(includesSource(src.key)) for(const auto& npc:src.npcs) {
        if(group!=-2 && npc.group!=group && !npc.possibleGroups.contains(group)) continue;
        QString name=npc.name; if(!npc.prototype.isEmpty()) name+=" · "+npc.prototype;
        QString source=QFileInfo(src.key).fileName();
        if(npc.scripted) source+=tr(":%1 [spawn site]").arg(npc.line);
        QStringList values={name,npc.hasId?QString::number(npc.id):QString::fromUtf8("—"),groupText(npc.group),possibleGroups(npc).join(", "),source};
        if(!filter.isEmpty() && !values.join(" ").contains(filter,Qt::CaseInsensitive)) continue;
        addRow(m_npcs,values,src.key);
    }
    for(const auto& event:m_result.events) if(includesSource(event.reference.sourceKey)) {
        const auto& ref=event.reference;
        bool related=group==-2 || event.group==group;
        if(ref.command=="SetDiplomacy" || ref.command=="GetDiplomacy") for(int i=0;i<qMin(2,ref.arguments.size());++i)
            if(ref.arguments[i].hasNumber && ref.arguments[i].number==group) related=true;
        if(!related) continue;
        QString status=ref.conditions.join("; ");
        if(!ref.status.isEmpty()) status+=(status.isEmpty()?QString():"; ")+ref.status;
        if(ref.command=="SetPlayer" && event.npcKey.isEmpty()) status+=(status.isEmpty()?QString():"; ")+tr("NPC identity unresolved");
        if(ref.command=="SetPlayer" && event.group<0) status+=(status.isEmpty()?QString():"; ")+tr("Group is computed, invalid or unknown");
        if(status.isEmpty()) status=tr("Possible execution");
        QStringList values={ref.context,event.group>=0?groupText(event.group):QString::fromUtf8("—"),QString("%1:%2").arg(QFileInfo(ref.sourceKey).fileName()).arg(ref.line),status};
        if(!filter.isEmpty() && !values.join(" ").contains(filter,Qt::CaseInsensitive)) continue;
        addRow(m_scripts,values,ref.sourceKey);
    }
    m_npcs->setUpdatesEnabled(true); m_scripts->setUpdatesEnabled(true);
}
const diplomacy::Source* CDiplomacyDialog::relationSource() const {
    const auto selected=m_sources->currentData().toString();
    const QString key=selected.isEmpty()?m_request.preferredSource:selected;
    for(const auto& src:m_result.sources) if(src.key==key) return &src;
    return m_result.sources.isEmpty()?nullptr:&m_result.sources[0];
}
void CDiplomacyDialog::populateRelations() {
    if(m_tabs->currentIndex()!=2) return;
    const auto* src=relationSource();
    m_edit->setEnabled(src && src->loaded);
    m_relations->clear(); m_relations->setRowCount(0); m_relations->setColumnCount(0);
    if(!src) { m_relationNotice->setText(tr("Open a MOB to inspect diplomacy.")); return; }
    bool complete=src->relations.size()==32;
    for(const auto& row:src->relations) if(row.size()!=32) complete=false;
    m_relationNotice->setText(complete?tr("%1 · Rows act on columns. 0: ally, 1: neutral, 2: war.").arg(QFileInfo(src->key).fileName()):tr("%1 has no complete diplomacy table.").arg(QFileInfo(src->key).fileName()));
    if(!complete) return;
    m_relations->setRowCount(32); m_relations->setColumnCount(32);
    QStringList names;
    for(int i=0;i<32;++i) names.append(QString::number(i)+" "+src->groupNames.value(i));
    m_relations->setHorizontalHeaderLabels(names); m_relations->setVerticalHeaderLabels(names);
    m_relations->horizontalHeader()->setDefaultSectionSize(95);
    for(int r=0;r<32;++r) for(int c=0;c<32;++c) {
        auto* item=new QTableWidgetItem(QString::number(src->relations[r][c]));
        ei::applyRelationCellColors(item, int(src->relations[r][c]));
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(names[r]+" → "+names[c]); m_relations->setItem(r,c,item);
    }
}
