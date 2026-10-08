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
#include <QToolButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTimer>
#include <QTabWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QHideEvent>
#include <QSettings>
#include <QPainter>
#include <QStyledItemDelegate>
#include <algorithm>

namespace {
enum class SectionIcon { Npc, Groups, Script, Relations, Refresh, Info };
constexpr int DetailsRole = Qt::UserRole+1, KeyRole = Qt::UserRole+2;
QIcon sectionIcon(SectionIcon section) {
    QPixmap image(48,48); image.setDevicePixelRatio(2); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#c5deed"),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.setBrush(QColor("#477f98"));
    switch(section) {
    case SectionIcon::Npc:
        p.drawEllipse(QRectF(8,3,8,8)); p.drawRoundedRect(QRectF(4,13,16,8),3,3); break;
    case SectionIcon::Groups:
        p.drawEllipse(QRectF(2,4,6,6)); p.drawEllipse(QRectF(16,4,6,6));
        p.drawRoundedRect(QRectF(1,12,8,8),2,2); p.drawRoundedRect(QRectF(15,12,8,8),2,2);
        p.setPen(QPen(QColor("#edc36c"),1.8)); p.drawLine(9,16,15,16); break;
    case SectionIcon::Script:
        p.drawRoundedRect(QRectF(4,2,16,20),2,2); p.setPen(QPen(QColor("#edc36c"),1.6));
        p.drawPolyline(QPolygonF()<<QPointF(9,8)<<QPointF(6,12)<<QPointF(9,16));
        p.drawPolyline(QPolygonF()<<QPointF(15,8)<<QPointF(18,12)<<QPointF(15,16)); break;
    case SectionIcon::Relations:
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) {
            p.setBrush(r==c?QColor("#84d691"):(r+c)%2?QColor("#edc36c"):QColor("#ff8c8c"));
            p.drawRect(QRectF(3+c*6,3+r*6,5,5));
        } break;
    case SectionIcon::Refresh:
        p.setBrush(Qt::NoBrush); p.drawArc(QRectF(4,4,16,16),35*16,285*16);
        p.setPen(QPen(QColor("#edc36c"),1.8));
        p.drawPolyline(QPolygonF()<<QPointF(14,3)<<QPointF(20,5)<<QPointF(18,11)); break;
    case SectionIcon::Info:
        p.drawEllipse(QRectF(3,3,18,18)); p.setPen(QPen(QColor("#edc36c"),2));
        p.drawPoint(12,7); p.drawLine(12,11,12,17); break;
    }
    return QIcon(image);
}
class InspectorRow : public QTreeWidgetItem {
public:
    explicit InspectorRow(const QStringList& values):QTreeWidgetItem(values) {}
    bool operator<(const QTreeWidgetItem& other) const override {
        const int c=treeWidget()->sortColumn();
        const auto a=data(c,Qt::UserRole), b=other.data(c,Qt::UserRole);
        if(a.isValid() && b.isValid()) return a.toULongLong()<b.toULongLong();
        if(a.isValid()!=b.isValid()) return a.isValid();
        return QTreeWidgetItem::operator<(other);
    }
};
class ReadableTree : public QTreeWidget {
    int m_nameWidth;
public:
    ReadableTree(int nameWidth,QWidget* parent):QTreeWidget(parent),m_nameWidth(nameWidth) { setColumnWidth(0,nameWidth); }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QTreeWidget::resizeEvent(event);
        int available=viewport()->width();
        for(int c=1;c<columnCount();++c) available-=columnWidth(c);
        // Keep the primary text readable; use horizontal scrolling in a narrow window.
        setColumnWidth(0,qMax(m_nameWidth,available));
    }
};
class RelationDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        auto colored=option;
        // Keep the relation color visible on selection; mark the cell with a border.
        colored.state&=~(QStyle::State_Selected|QStyle::State_HasFocus);
        QStyledItemDelegate::paint(painter,colored,index);
        if(option.state&QStyle::State_Selected) {
            painter->save(); painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(QColor("#c5deed"),2)); painter->drawRect(option.rect.adjusted(1,1,-2,-2)); painter->restore();
        }
    }
};
void setupTree(QTreeWidget* tree,const QStringList& columns) {
    tree->setHeaderLabels(columns); tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true); tree->setAlternatingRowColors(true);
    tree->setWordWrap(false); tree->setTextElideMode(Qt::ElideRight); tree->setIconSize(QSize(20,20));
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(QHeaderView::Interactive);
}
QString groupText(int group) { return group<0?QObject::tr("Unknown"):QString::number(group); }
QStringList possibleGroups(const diplomacy::Npc& npc) {
    QList<int> sorted=npc.possibleGroups.values(); std::sort(sorted.begin(),sorted.end());
    QStringList values; for(int group:sorted) values.append(QString::number(group)); return values;
}
InspectorRow* addRow(QTreeWidget* tree,const QStringList& text,const QString& details,const QString& key,const QIcon& icon) {
    auto* row=new InspectorRow(text);
    row->setIcon(0,icon); row->setData(0,DetailsRole,details); row->setData(0,KeyRole,key);
    for(int c=0;c<text.size();++c) row->setToolTip(c,text[c]);
    tree->addTopLevelItem(row); return row;
}
}
CDiplomacyDialog::CDiplomacyDialog(Snapshot snapshot,QWidget* parent)
    : QDialog(parent), m_snapshot(std::move(snapshot))
{
    setObjectName("diplomacyInspector"); setWindowTitle(tr("Diplomacy"));
    setWindowIcon(sectionIcon(SectionIcon::Groups));
    setAttribute(Qt::WA_QuitOnClose,false); resize(1560,940); setMinimumSize(900,560);
    ei::rememberWindowGeometry(this,QStringLiteral("diplomacyInspector"));
    auto* layout=new QVBoxLayout(this); layout->setSpacing(8);
    auto* controls=new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Sources:"),this));
    m_sources=new QComboBox(this); m_sources->setObjectName("diplomacySourceFilter");
    m_sources->setMinimumContentsLength(20); controls->addWidget(m_sources,1);
    auto* refreshButton=new QPushButton(sectionIcon(SectionIcon::Refresh),tr("Refresh"),this);
    refreshButton->setAutoDefault(false); controls->addWidget(refreshButton);
    auto* helpButton=new QToolButton(this); helpButton->setObjectName("diplomacyHelpButton");
    helpButton->setIcon(sectionIcon(SectionIcon::Info)); helpButton->setText(tr("How to read the data"));
    helpButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); helpButton->setCheckable(true);
    controls->addWidget(helpButton); layout->addLayout(controls);
    m_summary=new QLabel(this); m_summary->setObjectName("diplomacySummary");
    m_summary->setTextFormat(Qt::PlainText); QFont summaryFont=m_summary->font(); summaryFont.setBold(true); m_summary->setFont(summaryFont);
    m_summary->setWordWrap(true); layout->addWidget(m_summary);
    m_help=new QLabel(this); m_help->setObjectName("diplomacyHelp"); m_help->setTextFormat(Qt::PlainText);
    m_help->setWordWrap(true); m_help->hide(); layout->addWidget(m_help);
    connect(helpButton,&QToolButton::toggled,m_help,&QLabel::setVisible);
    m_filter=new QLineEdit(this); m_filter->setObjectName("diplomacyNpcFilter"); m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Search NPC, ID, prototype or script")); layout->addWidget(m_filter);
    m_split=new QSplitter(this); m_split->setObjectName("diplomacySplitter"); m_split->setChildrenCollapsible(false);
    auto* groupPanel=new QWidget(m_split); groupPanel->setMinimumWidth(285);
    auto* groupLayout=new QVBoxLayout(groupPanel); groupLayout->setContentsMargins(0,0,0,0);
    auto* groupTitle=new QLabel(tr("Diplomacy groups"),groupPanel); groupTitle->setFont(summaryFont); groupLayout->addWidget(groupTitle);
    m_hideEmpty=new QCheckBox(tr("Hide empty groups"),groupPanel); m_hideEmpty->setObjectName("diplomacyHideEmptyGroups");
    m_hideEmpty->setToolTip(tr("Show groups with placed NPCs or SetPlayer calls. The relation matrix always contains all 32 groups."));
    m_hideEmpty->setChecked(QSettings().value("ui/diplomacy/hideEmpty",true).toBool()); groupLayout->addWidget(m_hideEmpty);
    m_groups=new QTreeWidget(groupPanel); m_groups->setObjectName("diplomacyGroups");
    setupTree(m_groups,{tr("Group"),tr("NPCs"),tr("Assignments")}); groupLayout->addWidget(m_groups,1);
    m_groups->headerItem()->setToolTip(1,tr("Initial number of NPCs stored in the selected MOBs."));
    m_groups->headerItem()->setToolTip(2,tr("Number of SetPlayer calls; these are possible assignments, not additional NPCs."));
    m_groups->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    m_groups->setColumnWidth(1,m_groups->fontMetrics().horizontalAdvance(tr("NPCs"))+16);
    m_groups->setColumnWidth(2,m_groups->fontMetrics().horizontalAdvance(tr("Assignments"))+16);
    auto* rightPanel=new QWidget(m_split); auto* rightLayout=new QVBoxLayout(rightPanel); rightLayout->setContentsMargins(0,0,0,0);
    m_detailsSplit=new QSplitter(Qt::Vertical,rightPanel); m_detailsSplit->setObjectName("diplomacyDetailsSplitter");
    m_detailsSplit->setChildrenCollapsible(false); rightLayout->addWidget(m_detailsSplit,1);
    m_tabs=new QTabWidget(m_detailsSplit); m_tabs->setIconSize(QSize(20,20));
    m_npcs=new ReadableTree(240,m_tabs); m_npcs->setObjectName("diplomacyNpcs");
    setupTree(m_npcs,{tr("NPC"),tr("ID"),tr("MOB group"),QStringLiteral("SetPlayer"),tr("Prototype"),tr("MOB")});
    m_npcs->setColumnWidth(1,75); m_npcs->setColumnWidth(2,m_npcs->fontMetrics().horizontalAdvance(tr("MOB group"))+16);
    m_npcs->setColumnWidth(3,115); m_npcs->setColumnWidth(4,175); m_npcs->setColumnWidth(5,160);
    m_npcs->headerItem()->setToolTip(2,tr("Initial diplomacy group recorded in the MOB."));
    m_npcs->headerItem()->setToolTip(3,tr("Possible diplomacy groups assigned by SetPlayer. GroupAdd / GroupSub script groups are separate."));
    m_npcs->setSortingEnabled(true); m_npcs->sortByColumn(0,Qt::AscendingOrder);
    m_tabs->addTab(m_npcs,sectionIcon(SectionIcon::Npc),tr("NPCs"));
    m_scripts=new ReadableTree(300,m_tabs); m_scripts->setObjectName("diplomacyScripts");
    setupTree(m_scripts,{tr("Command / participant"),tr("Group"),tr("MOB"),tr("Line"),tr("Execution")});
    m_scripts->setColumnWidth(1,m_scripts->fontMetrics().horizontalAdvance(tr("Group"))+16);
    m_scripts->setColumnWidth(2,160); m_scripts->setColumnWidth(3,m_scripts->fontMetrics().horizontalAdvance(tr("Line"))+28);
    m_scripts->setColumnWidth(4,165);
    m_scripts->setSortingEnabled(true); m_scripts->sortByColumn(3,Qt::AscendingOrder);
    m_tabs->addTab(m_scripts,tr("Scripts")); m_tabs->setTabIcon(1,sectionIcon(SectionIcon::Script));
    auto* relationPage=new QWidget(m_tabs); auto* relationLayout=new QVBoxLayout(relationPage);
    auto* relationControls=new QHBoxLayout;
    relationControls->addWidget(new QLabel(tr("Relation table:"),relationPage));
    m_relationSources=new QComboBox(relationPage); m_relationSources->setObjectName("diplomacyRelationSource");
    relationControls->addWidget(m_relationSources,1);
    m_edit=new QPushButton(sectionIcon(SectionIcon::Relations),tr("Edit relations…"),relationPage);
    m_edit->setObjectName("diplomacyEditRelations"); m_edit->setAutoDefault(false); relationControls->addWidget(m_edit);
    relationLayout->addLayout(relationControls);
    auto* legend=new QHBoxLayout;
    for(int value=0;value<3;++value) {
        const QStringList labels={tr("0 · Ally"),tr("1 · Neutral"),tr("2 · War")};
        auto* label=new QLabel(labels[value],relationPage);
        QTableWidgetItem style; ei::applyRelationCellColors(&style,value);
        label->setStyleSheet(QString("background-color: %1; color: %2; padding: 3px 9px; border-radius: 3px;")
            .arg(style.background().color().name(),style.foreground().color().name())); legend->addWidget(label);
    }
    legend->addStretch(); relationLayout->addLayout(legend);
    m_relationNotice=new QLabel(relationPage); m_relationNotice->setTextFormat(Qt::PlainText);
    m_relationNotice->setWordWrap(true); relationLayout->addWidget(m_relationNotice);
    m_relations=new QTableWidget(relationPage); m_relations->setObjectName("diplomacyRelations");
    m_relations->setEditTriggers(QAbstractItemView::NoEditTriggers); m_relations->setSelectionMode(QAbstractItemView::SingleSelection);
    m_relations->setItemDelegate(new RelationDelegate(m_relations)); m_relations->setWordWrap(false);
    m_relations->verticalHeader()->setTextElideMode(Qt::ElideRight); m_relations->verticalHeader()->setFixedWidth(160);
    relationLayout->addWidget(m_relations,1);
    m_relationSelection=new QLabel(relationPage); m_relationSelection->setObjectName("diplomacyRelationSelection");
    m_relationSelection->setTextFormat(Qt::PlainText); m_relationSelection->setWordWrap(true); relationLayout->addWidget(m_relationSelection);
    m_tabs->addTab(relationPage,sectionIcon(SectionIcon::Relations),tr("Relations"));
    m_detailPanel=new QWidget(m_detailsSplit); auto* detailLayout=new QVBoxLayout(m_detailPanel); detailLayout->setContentsMargins(0,0,0,0);
    m_emptyNotice=new QLabel(m_detailPanel); m_emptyNotice->setObjectName("diplomacyEmptyNotice");
    m_emptyNotice->setStyleSheet(ei::uiTextStyle(ei::UiTextTone::Muted)); detailLayout->addWidget(m_emptyNotice);
    m_detailsTitle=new QLabel(m_detailPanel); m_detailsTitle->setFont(summaryFont); detailLayout->addWidget(m_detailsTitle);
    m_details=new QPlainTextEdit(m_detailPanel); m_details->setObjectName("diplomacyDetails");
    m_details->setReadOnly(true); m_details->setMinimumHeight(90); detailLayout->addWidget(m_details,1);
    m_detailsSplit->setStretchFactor(0,3); m_detailsSplit->setStretchFactor(1,1); m_detailsSplit->setSizes({480,260});
    m_split->setStretchFactor(1,1); m_split->setSizes({340,1220}); layout->addWidget(m_split,1);
    QSettings settings;
    const auto sizes=settings.value("ui/diplomacy/splitter").toByteArray(); if(!sizes.isEmpty()) m_split->restoreState(sizes);
    const auto detailSizes=settings.value("ui/diplomacy/detailsSplitter").toByteArray();
    if(!detailSizes.isEmpty()) m_detailsSplit->restoreState(detailSizes);
    auto* closeButton=new QPushButton(tr("Close"),this);
    closeButton->setAutoDefault(false); layout->addWidget(closeButton,0,Qt::AlignRight);
    m_timer=new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(250);
    m_worker=new QFutureWatcher<diplomacy::Result>(this);
    connect(m_worker,&QFutureWatcher<diplomacy::Result>::finished,this,[this] {
        m_workerPending=false;
        if(m_workerGeneration==m_generation && isVisible()) { m_result=m_worker->result(); populateSources(); populateGroups(); }
        else if(isVisible() && !m_timer->isActive()) m_timer->start();
    });
    connect(m_timer,&QTimer::timeout,this,&CDiplomacyDialog::refresh);
    connect(refreshButton,&QPushButton::clicked,this,&CDiplomacyDialog::invalidate);
    connect(closeButton,&QPushButton::clicked,this,&QDialog::close);
    connect(m_sources,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&CDiplomacyDialog::populateGroups);
    connect(m_relationSources,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this] {
        m_selectedRelationSource=m_relationSources->currentData().toString(); populateRelations();
    });
    connect(m_hideEmpty,&QCheckBox::toggled,this,&CDiplomacyDialog::filterGroups);
    connect(m_groups,&QTreeWidget::itemSelectionChanged,this,[this] {
        if(m_groups->currentItem()) m_selectedGroup=m_groups->currentItem()->data(0,Qt::UserRole).toInt();
        populateDetails();
        if(m_tabs->currentIndex()==2 && m_selectedGroup>=0 && m_selectedGroup<32 && m_relations->rowCount()==32)
            m_relations->setCurrentCell(m_selectedGroup,m_selectedGroup);
    });
    connect(m_filter,&QLineEdit::textChanged,this,&CDiplomacyDialog::populateDetails);
    connect(m_npcs,&QTreeWidget::itemSelectionChanged,this,&CDiplomacyDialog::showDetails);
    connect(m_scripts,&QTreeWidget::itemSelectionChanged,this,&CDiplomacyDialog::showDetails);
    connect(m_tabs,&QTabWidget::currentChanged,this,[this] { populateRelations(); showDetails(); });
    connect(m_relations,&QTableWidget::currentCellChanged,this,&CDiplomacyDialog::showRelationDetails);
    connect(m_edit,&QPushButton::clicked,this,[this] {
        if(const auto* src=relationSource()) if(src->loaded) emit editRelationsRequested(src->key);
    });
    showDetails();
}
bool CDiplomacyDialog::busy() const { return m_workerPending || m_timer->isActive(); }
void CDiplomacyDialog::showEvent(QShowEvent* event) { QDialog::showEvent(event); invalidate(); }
void CDiplomacyDialog::hideEvent(QHideEvent* event) {
    ++m_generation; m_timer->stop();
    QSettings settings; settings.setValue("ui/diplomacy/hideEmpty",m_hideEmpty->isChecked());
    settings.setValue("ui/diplomacy/splitter",m_split->saveState());
    if(m_tabs->currentIndex()!=2) settings.setValue("ui/diplomacy/detailsSplitter",m_detailsSplit->saveState());
    QDialog::hideEvent(event);
}
void CDiplomacyDialog::invalidate() {
    ++m_generation;
    // Remove stale identities while preserving the user's group choice for refresh.
    const QSignalBlocker groups(m_groups), npcs(m_npcs), scripts(m_scripts), relations(m_relations), sources(m_relationSources);
    if(m_npcs->currentItem()) m_selectedNpcKey=m_npcs->currentItem()->data(0,KeyRole).toString();
    if(m_scripts->currentItem()) m_selectedScriptKey=m_scripts->currentItem()->data(0,KeyRole).toString();
    m_result={}; m_groups->clear(); m_npcs->clear(); m_scripts->clear(); m_edit->setEnabled(false);
    m_relationSources->clear(); m_relations->clear(); m_relations->setRowCount(0); m_relations->setColumnCount(0);
    m_relationSelection->clear(); m_relationNotice->clear(); m_help->clear(); m_summary->setToolTip(QString());
    m_tabs->setTabText(0,tr("NPCs")); m_tabs->setTabText(1,tr("Scripts")); showDetails();
    if(isVisible()) { m_summary->setText(tr("Updating diplomacy…")); m_timer->start(); }
}
void CDiplomacyDialog::refresh() {
    if(!isVisible() || m_workerPending) return;
    m_request=m_snapshot(); m_workerGeneration=m_generation;
    const auto request=m_request;
    // Wait until GUI-thread completion has installed or rejected the result.
    m_workerPending=true;
    m_worker->setFuture(QtConcurrent::run([request] { return diplomacy::inspectConnected(request); }));
}
bool CDiplomacyDialog::includesSource(const QString& key) const {
    return m_sources->currentData().toString().isEmpty() || m_sources->currentData().toString()==key;
}
void CDiplomacyDialog::populateSources() {
    const auto previous=m_sources->currentData().toString(); const QSignalBlocker blocker(m_sources);
    m_sources->clear(); m_sources->addItem(tr("All connected MOBs"),QString());
    const QIcon icon=sectionIcon(SectionIcon::Groups);
    for(const auto& src:m_result.sources) {
        const QString label=QFileInfo(src.key).fileName()+(src.loaded?tr(" [loaded]"):tr(" [AddMob / profile]"))
            +(src.draft?tr(" [unapplied script]"):QString());
        m_sources->addItem(icon,label,src.key); m_sources->setItemData(m_sources->count()-1,src.key+"\n"+src.reason,Qt::ToolTipRole);
    }
    m_sources->setCurrentIndex(qMax(0,m_sources->findData(previous)));
}
void CDiplomacyDialog::populateGroups() {
    const QSignalBlocker blocker(m_groups); m_groups->clear();
    QMap<int,int> counts,calls; QMap<int,QStringList> names;
    int placed=0,spawns=0,mobs=0;
    for(const auto& src:m_result.sources) if(includesSource(src.key)) {
        ++mobs;
        for(const auto& npc:src.npcs) if(npc.scripted) ++spawns; else { ++placed; ++counts[npc.group]; }
        for(int g=0;g<src.groupNames.size() && g<32;++g) {
            const QString name=src.groupNames[g]; if(!name.isEmpty() && !names[g].contains(name)) names[g].append(name);
        }
    }
    for(const auto& event:m_result.events) if(includesSource(event.reference.sourceKey)
        && event.reference.command=="SetPlayer") ++calls[event.group];
    auto* all=new QTreeWidgetItem(m_groups,{tr("All groups"),QString::number(placed),QString::number([&]{int n=0;for(int v:calls)n+=v;return n;}())});
    all->setData(0,Qt::UserRole,-2); all->setIcon(0,sectionIcon(SectionIcon::Groups));
    for(int g=0;g<32;++g) {
        const QString title=QString::number(g)+(names[g].isEmpty()?QString():" — "+names[g].join(" / "));
        auto* row=new QTreeWidgetItem(m_groups,{title,QString::number(counts[g]),QString::number(calls[g])});
        row->setData(0,Qt::UserRole,g); row->setToolTip(0,title);
        row->setTextAlignment(1,Qt::AlignCenter); row->setTextAlignment(2,Qt::AlignCenter);
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
        if(m_groups->topLevelItem(r)->data(0,Qt::UserRole).toInt()==m_selectedGroup) m_groups->setCurrentItem(m_groups->topLevelItem(r));
    filterGroups();
    QString summary=tr("%1 NPCs · %2 MOBs · %3 script spawn sites").arg(placed).arg(mobs).arg(spawns);
    if(!m_result.diagnostics.isEmpty()) summary+=" · "+tr("Incomplete inspection: %1 warnings").arg(m_result.diagnostics.size());
    m_summary->setText(summary); m_summary->setToolTip(m_result.diagnostics.join("\n"));
    QString help=tr("MOB counts show initial membership. Script spawns and SetPlayer calls are possible changes; loops and conditions can change runtime counts. Script groups (GroupAdd / GroupSub) are separate from diplomacy groups.");
    if(!m_result.diagnostics.isEmpty()) help+="\n\n"+m_result.diagnostics.join("\n");
    m_help->setText(help); populateDetails(); populateRelationSources(); populateRelations();
}
void CDiplomacyDialog::filterGroups() {
    for(int r=1;r<m_groups->topLevelItemCount();++r) {
        auto* item=m_groups->topLevelItem(r);
        item->setHidden(m_hideEmpty->isChecked() && item->text(1)=="0" && item->text(2)=="0");
    }
    if(m_groups->currentItem() && m_groups->currentItem()->isHidden()) {
        m_selectedGroup=-2; m_groups->setCurrentItem(m_groups->topLevelItem(0));
    }
}
void CDiplomacyDialog::populateDetails() {
    const int group=m_groups->currentItem()?m_groups->currentItem()->data(0,Qt::UserRole).toInt():-2;
    const QString filter=m_filter->text().trimmed();
    if(m_npcs->currentItem()) m_selectedNpcKey=m_npcs->currentItem()->data(0,KeyRole).toString();
    if(m_scripts->currentItem()) m_selectedScriptKey=m_scripts->currentItem()->data(0,KeyRole).toString();
    const QString npcKey=m_selectedNpcKey, scriptKey=m_selectedScriptKey;
    const QSignalBlocker npcs(m_npcs), scripts(m_scripts);
    m_npcs->setUpdatesEnabled(false); m_scripts->setUpdatesEnabled(false);
    m_npcs->setSortingEnabled(false); m_scripts->setSortingEnabled(false); m_npcs->clear(); m_scripts->clear();
    for(const auto& src:m_result.sources) if(includesSource(src.key)) for(const auto& npc:src.npcs) {
        if(group!=-2 && npc.group!=group && !npc.possibleGroups.contains(group)) continue;
        const QString possible=possibleGroups(npc).join(", ");
        QString source=QFileInfo(src.key).fileName(); if(npc.scripted) source+=tr(":%1 [spawn site]").arg(npc.line);
        const QString details=tr("NPC: %1\nID: %2\nPrototype: %3\nInitial MOB group: %4\nPossible SetPlayer groups: %5\nSource: %6")
            .arg(npc.name,npc.hasId?QString::number(npc.id):tr("Unknown"),npc.prototype,groupText(npc.group),possible.isEmpty()?QString::fromUtf8("—"):possible,src.key)
            +(npc.scripted?"\n"+tr("Script spawn site at line %1; this does not determine the runtime number of NPCs.").arg(npc.line):QString());
        QStringList values={npc.name.isEmpty()?tr("Unnamed NPC"):npc.name,npc.hasId?QString::number(npc.id):QString::fromUtf8("—"),groupText(npc.group),possible,npc.prototype,source};
        if(!filter.isEmpty() && !(values.join(" ")+" "+details).contains(filter,Qt::CaseInsensitive)) continue;
        auto* row=addRow(m_npcs,values,details,src.key+"\n"+npc.key,npc.scripted?m_tabs->tabIcon(1):m_tabs->tabIcon(0));
        if(npc.hasId) row->setData(1,Qt::UserRole,QVariant::fromValue<qulonglong>(npc.id));
        if(npc.group>=0) row->setData(2,Qt::UserRole,npc.group);
        row->setTextAlignment(1,Qt::AlignCenter); row->setTextAlignment(2,Qt::AlignCenter);
        row->setToolTip(5,src.key); row->setToolTip(3,details);
    }
    for(const auto& event:m_result.events) if(includesSource(event.reference.sourceKey)) {
        const auto& ref=event.reference;
        bool related=group==-2 || event.group==group;
        if(ref.command=="SetDiplomacy" || ref.command=="GetDiplomacy") for(int i=0;i<qMin(2,ref.arguments.size());++i)
            if(ref.arguments[i].hasNumber && ref.arguments[i].number==group) related=true;
        if(!related) continue;
        QString status=ref.conditions.join("\n");
        if(!ref.status.isEmpty()) status+=(status.isEmpty()?QString():"\n")+ref.status;
        if(ref.command=="SetPlayer" && event.npcKey.isEmpty()) status+=(status.isEmpty()?QString():"\n")+tr("NPC identity unresolved");
        if(ref.command=="SetPlayer" && event.group<0) status+=(status.isEmpty()?QString():"\n")+tr("Group is computed, invalid or unknown");
        if(status.isEmpty()) status=tr("Possible execution");
        const QString details=tr("Call:\n%5\n\nConditions and status:\n%6\n\nCommand: %1\nGroup: %2\nSource: %3\nLine: %4")
            .arg(ref.command,event.group>=0?groupText(event.group):QString::fromUtf8("—"),ref.sourceKey,QString::number(ref.line),ref.context,status);
        QString execution=ref.conditions.isEmpty()?tr("Possible execution"):tr("Conditional");
        if(!ref.status.isEmpty() || (ref.command=="SetPlayer" && (event.group<0 || event.npcKey.isEmpty()))) execution=tr("Unresolved");
        QStringList values={ref.context.simplified(),event.group>=0?groupText(event.group):QString::fromUtf8("—"),QFileInfo(ref.sourceKey).fileName(),QString::number(ref.line),execution};
        if(!filter.isEmpty() && !(values.join(" ")+" "+details).contains(filter,Qt::CaseInsensitive)) continue;
        auto* row=addRow(m_scripts,values,details,ref.sourceKey+"\n"+QString::number(ref.line)+"\n"+ref.context,m_tabs->tabIcon(1));
        if(event.group>=0) row->setData(1,Qt::UserRole,event.group);
        row->setData(3,Qt::UserRole,ref.line); row->setTextAlignment(1,Qt::AlignCenter); row->setTextAlignment(3,Qt::AlignCenter);
        row->setToolTip(4,status); row->setToolTip(2,ref.sourceKey);
    }
    m_npcs->setSortingEnabled(true); m_scripts->setSortingEnabled(true);
    for(auto* tree:{m_npcs,m_scripts}) {
        const QString key=tree==m_npcs?npcKey:scriptKey;
        if(tree->topLevelItemCount()) tree->setCurrentItem(tree->topLevelItem(0));
        for(int r=0;r<tree->topLevelItemCount();++r) if(tree->topLevelItem(r)->data(0,KeyRole).toString()==key) tree->setCurrentItem(tree->topLevelItem(r));
        tree->setUpdatesEnabled(true);
    }
    m_tabs->setTabText(0,tr("NPCs (%1)").arg(m_npcs->topLevelItemCount()));
    m_tabs->setTabText(1,tr("Scripts (%1)").arg(m_scripts->topLevelItemCount())); showDetails();
}
void CDiplomacyDialog::showDetails() {
    const bool matrix=m_tabs->currentIndex()==2;
    m_filter->setEnabled(!matrix); m_detailPanel->setVisible(!matrix); m_emptyNotice->setVisible(!matrix);
    if(matrix) return;
    auto* tree=m_tabs->currentIndex()==0?m_npcs:m_scripts;
    m_detailsTitle->setText(tree==m_npcs?tr("NPC details"):tr("Script details"));
    m_emptyNotice->setText(tree->topLevelItemCount()?tr("%1 rows · Click a column heading to sort").arg(tree->topLevelItemCount()):tr("No matches. Change the group, source or search."));
    m_details->setPlainText(tree->currentItem()?tree->currentItem()->data(0,DetailsRole).toString():tr("Select a row to see full details."));
}
void CDiplomacyDialog::populateRelationSources() {
    const QSignalBlocker blocker(m_relationSources); m_relationSources->clear();
    for(const auto& src:m_result.sources) if(includesSource(src.key)) {
        m_relationSources->addItem(QFileInfo(src.key).fileName()+(src.loaded?QString():tr(" [read-only]")),src.key);
        m_relationSources->setItemData(m_relationSources->count()-1,src.key,Qt::ToolTipRole);
    }
    int selected=m_relationSources->findData(m_selectedRelationSource);
    if(selected<0) selected=m_relationSources->findData(m_request.preferredSource);
    m_relationSources->setCurrentIndex(qMax(0,selected)); m_selectedRelationSource=m_relationSources->currentData().toString();
}
const diplomacy::Source* CDiplomacyDialog::relationSource() const {
    const QString key=m_relationSources->currentData().toString();
    for(const auto& src:m_result.sources) if(src.key==key) return &src;
    return nullptr;
}
void CDiplomacyDialog::sizeRelationCells() {
    const int width=qMax(28,m_relations->fontMetrics().horizontalAdvance("31")+4);
    m_relations->horizontalHeader()->setMinimumSectionSize(width);
    m_relations->horizontalHeader()->setDefaultSectionSize(width);
    m_relations->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_relations->verticalHeader()->setMinimumSectionSize(24);
    m_relations->verticalHeader()->setDefaultSectionSize(qMax(26,m_relations->fontMetrics().height()+2));
}
void CDiplomacyDialog::populateRelations() {
    if(m_tabs->currentIndex()!=2) return;
    const auto* src=relationSource(); m_edit->setEnabled(src && src->loaded);
    const QSignalBlocker blocker(m_relations);
    m_relations->clear(); m_relations->setRowCount(0); m_relations->setColumnCount(0); m_relationSelection->clear();
    if(!src) { m_relationNotice->setText(tr("Open a MOB to inspect diplomacy.")); return; }
    bool complete=src->relations.size()==32;
    for(const auto& row:src->relations) if(row.size()!=32) complete=false;
    m_relationNotice->setText(complete?tr("The row group acts on the column group. Column numbers match the groups on the left."):tr("%1 has no complete diplomacy table.").arg(QFileInfo(src->key).fileName()));
    if(!complete) return;
    m_relations->setRowCount(32); m_relations->setColumnCount(32); sizeRelationCells();
    QStringList names; for(int i=0;i<32;++i) names.append(QString::number(i)+(src->groupNames.value(i).isEmpty()?QString():" · "+src->groupNames.value(i)));
    for(int i=0;i<32;++i) {
        auto* column=new QTableWidgetItem(QString::number(i)); column->setToolTip(names[i]); m_relations->setHorizontalHeaderItem(i,column);
        auto* row=new QTableWidgetItem(names[i]); row->setToolTip(names[i]); m_relations->setVerticalHeaderItem(i,row);
    }
    for(int r=0;r<32;++r) for(int c=0;c<32;++c) {
        const auto value=src->relations[r][c]; auto* item=new QTableWidgetItem(QString::number(value));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable); ei::applyRelationCellColors(item,int(value)); item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(names[r]+" → "+names[c]+"\n"+tr("Stored value: %1").arg(value)); m_relations->setItem(r,c,item);
    }
    const int group=m_selectedGroup>=0 && m_selectedGroup<32?m_selectedGroup:0;
    m_relations->setCurrentCell(group,group); showRelationDetails();
}
void CDiplomacyDialog::showRelationDetails() {
    const int r=m_relations->currentRow(), c=m_relations->currentColumn();
    if(r<0 || c<0 || !m_relations->item(r,c)) { m_relationSelection->clear(); return; }
    const uint value=m_relations->item(r,c)->text().toUInt();
    const QStringList labels={tr("Ally"),tr("Neutral"),tr("War")};
    m_relationSelection->setText(tr("%1 → %2: %3 (%4)").arg(m_relations->verticalHeaderItem(r)->text(),m_relations->verticalHeaderItem(c)->text(),value<3?labels[int(value)]:tr("Unknown relation"),QString::number(value)));
    QFont normal=m_relations->font(), active=normal; active.setBold(true);
    for(int i=0;i<32;++i) {
        m_relations->horizontalHeaderItem(i)->setFont(i==c?active:normal);
        m_relations->verticalHeaderItem(i)->setFont(i==r?active:normal);
    }
}
