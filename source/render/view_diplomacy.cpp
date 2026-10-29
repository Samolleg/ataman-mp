#include "view.h"
#include "unit.h"
#include "mob.h"
#include "camera.h"
#include "scene.h"
#include "undo.h"
#include "render_queue.h"
#include "diplomacy_colors.h"
#include "viewport_widget.h"
#include "tree_view.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUndoStack>
#include <algorithm>

namespace {
CMob* sourceMob(const QVector<CMob*>& mobs, const QString& source)
{
    for(CMob* mob : mobs)
        if(mob && mob->filePath().absoluteFilePath() == source) return mob;
    return nullptr;
}
bool completeTable(const QVector<QVector<uint>>& table)
{
    if(table.size() != 32) return false;
    for(const auto& row : table) if(row.size() != 32) return false;
    return true;
}
}

void CView::setDiplomacyMapMode(bool enabled)
{
    if(m_diplomacyMapMode == enabled) return;
    cancelPendingOperation();
    m_diplomacyMapMode = enabled;
    m_diplomacyAnchor.clear();
    if(enabled) {
        setLandscapeEditingEnabled(false);
        setTravelEditing(false);
        CScene::getInstance()->changeMode(eEditModeObjects);
    }
    if(enabled && !m_diplomacyOverlay) {
        auto* panel = new CViewportWidget(this);
        m_diplomacyOverlay = panel;
        panel->setObjectName("diplomacyMapOverlay");
        panel->setViewportSurfaceColor(QColor("#202a35"));
        panel->setStyleSheet(
            "QFrame#diplomacyMapOverlay { border: 1px solid #526d87; border-radius: 6px; }"
            "QLabel { color: #dfe9f4; background: transparent; border: none; }"
            "QPushButton { padding: 4px 9px; border: 1px solid #536779; border-radius: 4px; background: #2e4052; color: #e8f1fa; }"
            "QPushButton:hover { background: #3d5871; }");
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(12,9,12,9); layout->setSpacing(6);
        auto* top = new QHBoxLayout;
        auto* title = new QLabel(tr("DIPLOMACY"),panel);
        QFont font = title->font(); font.setBold(true); title->setFont(font);
        top->addWidget(title); top->addStretch();
        auto* groups = new QPushButton(tr("Group colors"),panel);
        groups->setObjectName("diplomacyGroupColors");
        auto* editor = new QPushButton(tr("Relations…"),panel);
        auto* close = new QPushButton(QString::fromUtf8("×"),panel);
        close->setToolTip(tr("Turn off diplomacy map mode"));
        top->addWidget(groups); top->addWidget(editor); top->addWidget(close); layout->addLayout(top);
        auto* selection = new QLabel(panel); selection->setObjectName("diplomacyMapSelection");
        selection->setTextFormat(Qt::PlainText); selection->setWordWrap(true); layout->addWidget(selection);
        auto* legend = new QLabel(panel); legend->setObjectName("diplomacyMapLegend");
        legend->setWordWrap(true); legend->setTextFormat(Qt::RichText);
        layout->addWidget(legend);
        connect(groups,&QPushButton::clicked,this,&CView::clearDiplomacyAnchor);
        connect(editor,&QPushButton::clicked,this,&CView::diplomacyWindowRequested);
        connect(close,&QPushButton::clicked,this,[this] { setDiplomacyMapMode(false); });
        connect(this,&CView::selectionChanged,this,[this] {
            if(!m_diplomacyMapMode) return;
            CUnit* unit = nullptr;
            for(CNode* node : selectionSnapshot())
                if(auto* candidate = dynamic_cast<CUnit*>(node)) unit = candidate;
            setDiplomacyAnchor(unit);
        });
        connect(this,&CView::mobStateChanged,this,&CView::updateDiplomacyOverlay);
        connect(this,&CView::loadedMapStateChanged,this,&CView::updateDiplomacyOverlay);
        if(m_pUndoStack) connect(m_pUndoStack,&QUndoStack::indexChanged,this,&CView::updateDiplomacyOverlay);
    }
    updateDiplomacyOverlay();
    emit diplomacyMapModeChanged(enabled);
    refreshSelectContextHelp();
    update();
}

void CView::setDiplomacyAnchor(CUnit* unit)
{
    m_diplomacyAnchor = unit && ownerMob(unit) && !unit->isMarkDeleted() ? unit : nullptr;
    updateDiplomacyOverlay(); update();
}
void CView::clearDiplomacyAnchor() { setDiplomacyAnchor(nullptr); }

void CView::updateDiplomacyOverlay()
{
    if(m_diplomacyAnchor && (!ownerMob(m_diplomacyAnchor) || m_diplomacyAnchor->isMarkDeleted()))
        m_diplomacyAnchor.clear();
    if(!m_diplomacyOverlay) return;
    m_diplomacyOverlay->setVisible(m_diplomacyMapMode);
    if(!m_diplomacyMapMode) return;
    auto* label = m_diplomacyOverlay->findChild<QLabel*>("diplomacyMapSelection");
    QString text = tr("Group colors · Click an NPC to see its relations to the others.");
    if(m_diplomacyAnchor) {
        auto* unit = m_diplomacyAnchor.data(); CMob* mob = ownerMob(unit);
        const int group = static_cast<unsigned char>(unit->dipGroup());
        text = tr("%1 · Group %2 → other NPCs · %3")
            .arg(unit->mapName().isEmpty() ? QString::number(unit->mapId()) : unit->mapName(),
                 QString::number(group),mob->filePath().fileName());
        if(group >= 32 || !completeTable(mob->diplomacyField()))
            text += "\n" + tr("No valid diplomacy table: relations are shown in gray.");
    }
    label->setText(text);
    auto* legend = m_diplomacyOverlay->findChild<QLabel*>("diplomacyMapLegend");
    if(m_diplomacyAnchor) {
        legend->setText(QString("<span style='color:%1'>● %2</span> &nbsp; <span style='color:%3'>● %4</span> &nbsp; <span style='color:%5'>● %6</span> &nbsp; <span style='color:#9aa7b5'>● %7</span>")
            .arg(diplomacy::relationColor(0).name(),tr("Ally"),diplomacy::relationColor(1).name(),tr("Neutral"),
                 diplomacy::relationColor(2).name(),tr("Enemy"),tr("Unknown")));
    } else {
        QSet<int> groups;
        for(CMob* mob:m_aMob) if(isMobVisible(mob)) for(CNode* node:mob->nodes())
            if(auto* unit=dynamic_cast<CUnit*>(node))
                if(!unit->isMarkDeleted() && unit->nodeState()!=ENodeState::eHidden)
                    groups.insert(static_cast<unsigned char>(unit->dipGroup()));
        QList<int> sorted=groups.values(); std::sort(sorted.begin(),sorted.end());
        QStringList swatches;
        for(int group:sorted) swatches.append(QString("<span style='color:%1'>● %2</span>")
            .arg(diplomacy::groupColor(group).name(),QString::number(group)));
        legend->setText(swatches.isEmpty()?tr("No NPCs in visible MOBs."):swatches.join(" &nbsp; "));
    }
    m_diplomacyOverlay->setFixedWidth(qMin(610,qMax(200,width()-24)));
    m_diplomacyOverlay->adjustSize();
    m_diplomacyOverlay->move(qMax(12,width()-m_diplomacyOverlay->width()-12),12);
    m_diplomacyOverlay->raise();
}

QColor CView::diplomacyNpcColor(CUnit* unit) const
{
    if(!unit) return diplomacy::groupColor(-1);
    const int target = static_cast<unsigned char>(unit->dipGroup());
    CMob* mob = m_diplomacyAnchor ? ownerMob(m_diplomacyAnchor) : nullptr;
    if(!mob || m_diplomacyAnchor->isMarkDeleted())
        return diplomacy::groupColor(target);
    const int group = static_cast<unsigned char>(m_diplomacyAnchor->dipGroup());
    const auto& table = mob->diplomacyField();
    if(group >= 32 || target >= 32 || !completeTable(table)) return diplomacy::groupColor(-1);
    return diplomacy::relationColor(table[group][target]);
}

void CView::drawDiplomacyObjectVisual(CNode* node, bool active)
{
    auto& queue = RenderQueueContext::instance();
    auto* unit = dynamic_cast<CUnit*>(node);
    if(m_diplomacyMapMode && !m_minimapCaptureActive && m_topdownPass == ETopdownPass::None && unit)
        queue.setDiplomacyTint(diplomacy::mapTint(diplomacyNpcColor(unit)));
    node->drawObjectVisual(active,&m_program);
    queue.setDiplomacyTint(QVector4D());
}

bool CView::focusDiplomacyNpc(const QString& source, const QString& key)
{
    CMob* mob = sourceMob(m_aMob,source);
    if(!mob) return false;
    for(CNode* node : mob->nodes()) {
        auto* unit = dynamic_cast<CUnit*>(node);
        if(!unit || unit->isMarkDeleted() || key != QString("node:%1").arg(unit->innerId())) continue;
        setMobVisible(mob,true);
        if(unit->nodeState() == ENodeState::eHidden) unit->setState(ENodeState::eDraw);
        const auto before = selectionSnapshot();
        restoreSelection({unit}); recordSelectionChange(before,tr("Select diplomacy NPC"));
        if(m_diplomacyMapMode) setDiplomacyAnchor(unit);
        focusScenarioPosition(unit->drawPosition());
        return true;
    }
    return false;
}

bool CView::editDiplomacyRelation(const QString& source, int row, int column,
                                 uint value, bool symmetric, uint expected, uint expectedReverse)
{
    CMob* mob = sourceMob(m_aMob,source);
    if(!mob || !m_pUndoStack || row < 0 || column < 0 || row >= 32 || column >= 32 || value > 2)
        return false;
    auto table = mob->diplomacyField();
    if(!completeTable(table) || table[row][column] != expected
       || (symmetric && table[column][row] != expectedReverse)) return false;
    if(table[row][column] == value && (!symmetric || table[column][row] == value)) return true;
    table[row][column] = value;
    if(symmetric) table[column][row] = value;
    m_pUndoStack->push(new CReplaceDiplomacyTableCommand(mob,mob->diplomacyNames(),table,
        tr("Change diplomacy %1 → %2").arg(row).arg(column)));
    updateDiplomacyOverlay(); update();
    return true;
}

bool CView::createDiplomacyTable(const QString& source)
{
    CMob* mob = sourceMob(m_aMob,source);
    if(!mob || !m_pUndoStack || !mob->diplomacyField().isEmpty()) return false;
    QVector<QVector<uint>> table(32,QVector<uint>(32,1));
    for(int g=0;g<32;++g) table[g][g] = 0;
    m_pUndoStack->push(new CReplaceDiplomacyTableCommand(mob,mob->diplomacyNames(),table,tr("Create neutral diplomacy table")));
    return true;
}
