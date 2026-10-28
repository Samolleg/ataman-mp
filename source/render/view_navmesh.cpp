#include "view.h"
#include "navmesh_panel.h"
#include "navigation_save_events.h"
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
#include <QComboBox>
#include <QPainter>
#include <QTextDocument>
#include <QtMath>
#include <algorithm>

void CView::createNavmeshMenu(QMenu* menu) {
    // MainWindow creates menus before attach() creates the viewport controls.
    // Resolve the panel when invoked; never conditionally append a late button.
    auto* action=menu->addAction(QStringLiteral("Навигация ИИ — Navmesh"));
    connect(action,&QAction::triggered,this,[this](){
        if(!m_navigationSource)return;
        m_navigationSource->setCurrentIndex(1);
        if(m_navigationScroll->isHidden())m_aiGraphPreviewCollapse->click();
        m_aiGraphPreviewWidget->show();m_aiGraphPreviewWidget->raise();
    });
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
void CView::navigationMapSaved(const QString& path,bool navigationChanged){
    if(!m_navmeshPanel||!m_pLand||!m_pLand->isMprLoad())return;
    const auto same=[](const QString& a,const QString& b){return QDir::cleanPath(a).compare(QDir::cleanPath(b),Qt::CaseInsensitive)==0;};
    const auto mpr=m_pLand->filePath().absoluteFilePath();
    const auto mob=m_environmentMob?m_environmentMob->filePath().absoluteFilePath():QString();
    const auto navsrc=QFileInfo(mpr).absolutePath()+"/"+QFileInfo(mpr).completeBaseName()+".navsrc";
    if(!same(path,mpr)&&!same(path,mob)&&!same(path,navsrc))return;
    syncNavmeshContext();
    m_navmeshPanel->acknowledgeSave(path);
    if(navigationChanged)m_navmeshPanel->requestBakeOnSave();
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
    if(m_navmeshPanel->lossesVisible()){
        const auto& comparison=m_navmeshPanel->comparison();
        for(const auto& tile:comparison.renderTiles){if(tile.areas.isEmpty()||!visible(tile))continue;++m_navmeshDrawnTiles;for(const auto& batch:tile.areas)draw(batch.vertices,batch.color,GL_TRIANGLES);}
        const int selected=m_navmeshPanel->selectedRegion();
        if(selected>=0&&selected<comparison.areas.size()){
            QVector<QVector3D> edges;
            const QRect b=comparison.areas[selected].bounds;
            for(int y=b.top();y<=b.bottom();++y)for(int x=b.left();x<=b.right();++x){const int i=y*comparison.width+x;if(comparison.regions[i]!=selected)continue;const float a=x*.5f,c=y*.5f,z=comparison.heights[i]+.24f;const QVector3D p(a,c,z),q(a+.5f,c,z),r(a+.5f,c+.5f,z),s(a,c+.5f,z);edges<<p<<q<<q<<r<<r<<s<<s<<p;}
            ei::gl::LineWidth(2);draw(edges,{1,1,1,1},GL_LINES);ei::gl::LineWidth(1);
        }
        ei::gl::Enable(GL_DEPTH_TEST);return;
    }
    const bool filled=m_navmeshPanel->fillVisible();
    for(const auto& tile:doc.renderTiles){if(!visible(tile))continue;++m_navmeshDrawnTiles;if(filled)for(const auto& area:tile.areas)draw(area.vertices,area.color,GL_TRIANGLES);draw(tile.edges,filled?QVector4D(.12f,.16f,.12f,.65f):QVector4D(.22f,.95f,.45f,.8f),GL_LINES);if(m_navmeshPanel->narrowVisible())draw(tile.narrow,{1.f,.45f,.05f,1.f},GL_LINES);}
    ei::gl::LineWidth(2);draw(doc.tiles,{.15f,.9f,1.f,.9f},GL_LINES);
    glPointSize(4);if(m_navmeshPanel->islandsVisible())draw(doc.islands,{1.f,.1f,.15f,1.f},GL_POINTS);glPointSize(1);ei::gl::LineWidth(1);ei::gl::Enable(GL_DEPTH_TEST);
}

void CView::drawNavmeshLegend(){
    if(!m_navmeshPanel||!m_navmeshPanel->legendVisible())return;
    const auto html=m_navmeshPanel->legendHtml();const int wanted=qMin(325,qMax(120,width()-40));
    if(html!=m_navmeshLegendHtml||m_navmeshLegendSize.width()!=wanted+12||!m_navmeshLegendTexture){
        // Paint only an owned CPU image. QPainter must never touch QGLWidget's
        // double-buffered framebuffer (see existing entrance-label renderer).
        QTextDocument doc;doc.setDefaultFont(QFont(QStringLiteral("Segoe UI"),9));doc.setDefaultStyleSheet("body {color: white;} p {margin: 0;}");doc.setHtml("<body>"+html+"</body>");doc.setTextWidth(wanted);
        m_navmeshLegendSize=QSize(wanted+12,qCeil(doc.size().height())+10);
        QImage image(m_navmeshLegendSize,QImage::Format_ARGB32);image.fill(Qt::transparent);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QColor(180,190,200,180));painter.setBrush(QColor(18,22,26,200));painter.drawRoundedRect(QRectF(image.rect()).adjusted(.5,.5,-.5,-.5),5,5);painter.translate(6,5);doc.drawContents(&painter);painter.end();
        if(m_navmeshLegendTexture)deleteTexture(m_navmeshLegendTexture);
        m_navmeshLegendTexture=bindTexture(image,GL_TEXTURE_2D,GL_RGBA);m_navmeshLegendHtml=html;
    }
    const int position=ei::attributeLocation(&m_program,"a_position"),texture=ei::attributeLocation(&m_program,"a_texture");if(position<0||texture<0)return;
    // Place beside (not under) native overlay widgets such as the minimap.
    QList<QRect> occupied;QList<int> candidates{width()-m_navmeshLegendSize.width()-12};
    for(auto* widget:findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(widget->isVisible()){
        occupied<<widget->geometry();candidates<<widget->geometry().left()-m_navmeshLegendSize.width()-12;
    }
    std::sort(candidates.begin(),candidates.end(),std::greater<int>());
    m_navmeshLegendRect=QRect(QPoint(12,12),m_navmeshLegendSize);
    for(int x:candidates){QRect box(QPoint(x,12),m_navmeshLegendSize);if(!rect().contains(box))continue;bool blocked=false;for(const auto& other:occupied)blocked|=box.intersects(other);if(!blocked){m_navmeshLegendRect=box;break;}}
    const float right=2.f*(m_navmeshLegendRect.right()+1)/width()-1,left=2.f*m_navmeshLegendRect.left()/width()-1,top=1-2.f*m_navmeshLegendRect.top()/height(),bottom=1-2.f*(m_navmeshLegendRect.bottom()+1)/height();
    const QVector<QVector3D> vertices={{left,bottom,0},{right,bottom,0},{right,top,0},{left,top,0}};
    const QVector<QVector2D> uv={{0,0},{1,0},{1,1},{0,1}};
    QMatrix4x4 identity;m_program.setUniformValue(ei::uniformLocation(&m_program,"u_viewMmatrix"),identity);m_program.setUniformValue(ei::uniformLocation(&m_program,"u_projMmatrix"),identity);m_program.setUniformValue(ei::uniformLocation(&m_program,"customColor"),QVector4D(0,0,0,0));m_program.setUniformValue(ei::uniformLocation(&m_program,"qt_Texture0"),0);
    ei::gl::Disable(GL_DEPTH_TEST);m_program.enableAttributeArray(texture);ei::gl::BindTexture(GL_TEXTURE_2D,m_navmeshLegendTexture);m_program.setAttributeArray(position,vertices.constData());m_program.setAttributeArray(texture,uv.constData());ei::gl::DrawArrays(GL_TRIANGLE_FAN,0,4);ei::gl::BindTexture(GL_TEXTURE_2D,0);m_program.disableAttributeArray(texture);
}
bool CView::navmeshComparisonClick(const QPoint& point){
    if(!m_navmeshPanel||!m_navmeshPanel->layerVisible()||!m_navmeshPanel->lossesVisible())return false;
    const auto& c=m_navmeshPanel->comparison();if(!c.valid)return false;
    bool invertible=false;const auto inverse=(m_projection*m_cam->viewMatrix()).inverted(&invertible);if(!invertible)return false;
    const float x=2.f*point.x()/qMax(1,width())-1,y=1-2.f*point.y()/qMax(1,height());
    auto near4=inverse*QVector4D(x,y,-1,1),far4=inverse*QVector4D(x,y,1,1);const auto a=near4.toVector3DAffine(),b=far4.toVector3DAffine();const auto ray=b-a;if(qAbs(ray.z())<1e-7)return false;
    // Intersect only difference cells, choose the nearest visible surface.
    float best=1e30f;int region=-1;
    for(int i=0;i<c.cells.size();++i)if(c.cells[i]){const float t=(c.heights[i]+.2f-a.z())/ray.z();if(t<0||t>best)continue;const auto p=a+t*ray;const int cx=i%c.width,cy=i/c.width;if(p.x()>=cx*.5f&&p.x()<(cx+1)*.5f&&p.y()>=cy*.5f&&p.y()<(cy+1)*.5f){best=t;region=c.regions[i];}}
    if(region<0)return false;m_navmeshPanel->selectRegion(region);return true;
}
