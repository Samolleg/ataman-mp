#include "view.h"
#include "navmesh_panel.h"
#include "landscape.h"
#include "mob.h"
#include "camera.h"
#include "settings.h"
#include "shader_locations.h"
#include "gl_dispatch.h"
#include <QMainWindow>
#include <QDockWidget>
#include <QMenu>
#include <QToolButton>
#include <QTimer>
#include <QScrollArea>
#include <QLayout>
#include <QDir>

void CView::createNavmeshDock(QMainWindow* window,QMenu* menu) {
    auto* dock=new QDockWidget(QStringLiteral("Navmesh — EIRebuild64"),window);dock->setObjectName("navmeshDock");
    auto* scroll=new QScrollArea(dock);scroll->setWidgetResizable(true);
    m_navmeshPanel=new CNavmeshPanel(scroll);scroll->setWidget(m_navmeshPanel);dock->setWidget(scroll);dock->setMinimumWidth(290);window->addDockWidget(Qt::RightDockWidgetArea,dock);dock->hide();
    menu->addAction(dock->toggleViewAction());
    if(m_aiGraphPreviewWidget){auto* button=new QToolButton(m_aiGraphPreviewWidget);button->setText(QStringLiteral("Navmesh…"));m_aiGraphPreviewWidget->layout()->addWidget(button);connect(button,&QToolButton::clicked,dock,[dock](){dock->show();dock->raise();});}
    connect(m_navmeshPanel,&CNavmeshPanel::geometryChanged,this,[this](){update();});
    connect(m_navmeshPanel,&CNavmeshPanel::oldGraphRequested,this,[this](){if(m_aiGraphPreviewToggle)m_aiGraphPreviewToggle->setChecked(true);});
    auto* timer=new QTimer(m_navmeshPanel);timer->setInterval(1000);connect(timer,&QTimer::timeout,this,[this](){syncNavmeshContext();});timer->start();syncNavmeshContext();
}
void CView::syncNavmeshContext(){
    if(!m_navmeshPanel)return;const bool loaded=m_pLand&&m_pLand->isMprLoad();
    QString maps=loaded?m_pLand->filePath().absolutePath():QString();
    if(loaded&&m_pSettings)if(auto* roots=dynamic_cast<COptStringList*>(m_pSettings->opt(eOptSetResource,"mapRoots"))){
        // A Base fallback MPR still uses the active mod's authored Nav64.
        for(const auto& root:roots->value())if(QDir(root).canonicalPath().compare(QDir(maps).canonicalPath(),Qt::CaseInsensitive)==0){if(!roots->value().isEmpty())maps=roots->value().first();break;}
    }
    m_navmeshPanel->setScene(loaded?m_pLand->filePath().absoluteFilePath():QString(),loaded&&m_environmentMob?m_environmentMob->filePath().absoluteFilePath():QString(),m_pSettings?m_pSettings->configuredGameRoot():QString(),maps);
    m_navmeshPanel->setUnsavedChanges(loaded&&(m_pLand->isDirty()||(m_environmentMob&&m_environmentMob->isDirty())));
}
void CView::drawNavmeshOverlay(int position){
    m_navmeshDrawnTiles=0;m_navmeshDrawnVertices=0;
    if(!m_navmeshPanel||!m_navmeshPanel->layerVisible())return;const auto& doc=m_navmeshPanel->document();
    const QMatrix4x4 clip=m_projection*m_cam->viewMatrix();
    auto visible=[&](const SNav64RenderTile& tile){
        int outside[6]={};for(int c=0;c<8;++c){const QVector3D v((c&1)?tile.maximum.x():tile.minimum.x(),(c&2)?tile.maximum.y():tile.minimum.y(),(c&4)?tile.maximum.z():tile.minimum.z());const auto p=clip*QVector4D(v,1);outside[0]+=p.x()<-p.w();outside[1]+=p.x()>p.w();outside[2]+=p.y()<-p.w();outside[3]+=p.y()>p.w();outside[4]+=p.z()<-p.w();outside[5]+=p.z()>p.w();}for(int n:outside)if(n==8)return false;return true;
    };
    ei::gl::Disable(GL_DEPTH_TEST);
    auto draw=[&](const QVector<QVector3D>& v,const QVector4D& color,GLenum primitive){if(v.isEmpty())return;m_navmeshDrawnVertices+=quint64(v.size());m_program.setUniformValue(ei::uniformLocation(&m_program,"customColor"),color);m_program.setAttributeArray(position,v.constData());ei::gl::DrawArrays(primitive,0,v.size());};
    for(const auto& tile:doc.renderTiles){if(!visible(tile))continue;++m_navmeshDrawnTiles;for(const auto& area:tile.areas)draw(area.vertices,area.color,GL_TRIANGLES);draw(tile.edges,{.12f,.16f,.12f,.55f},GL_LINES);draw(tile.narrow,{1.f,.45f,.05f,1.f},GL_LINES);}
    ei::gl::LineWidth(2);draw(doc.tiles,{.15f,.9f,1.f,.9f},GL_LINES);
    glPointSize(4);draw(doc.islands,{1.f,.1f,.15f,1.f},GL_POINTS);glPointSize(1);ei::gl::LineWidth(1);ei::gl::Enable(GL_DEPTH_TEST);
}
