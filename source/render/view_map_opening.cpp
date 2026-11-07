#include "view.h"
#include "camera.h"
#include "main_window.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"
#include "camera_document.h"
#include "endurance_document.h"
#include "opening_trace.h"
#include "opening_cpu_value.h"
#include <tuple>
#include <cmath>
namespace {
bool intersects(const QMatrix4x4& clip,float width,float height,float low,float high) {
    QVector4D corners[8];
    for(int i=0;i<8;++i) {
        corners[i]=clip*QVector4D((i&1)?width:0,(i&2)?height:0,(i&4)?high:low,1);
        for(int axis=0;axis<4;++axis)if(!std::isfinite(corners[i][axis]))return false;
    }
    // Clip each map-bounds face against the actual shared lens. This includes
    // a flat new map; maxZ is a height scale, not its occupied height range.
    const int faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for(const auto& face:faces) {
        QVector<QVector4D> polygon;for(int i:face)polygon.append(corners[i]);
        for(int plane=0;plane<6&&!polygon.isEmpty();++plane) {
            const int axis=plane/2;const float sign=(plane%2)?1:-1;
            QVector<QVector4D> clipped;auto previous=polygon.last();float d0=previous.w()+sign*previous[axis];
            for(const auto& current:polygon) {
                const float d1=current.w()+sign*current[axis];
                if((d0>=0)!=(d1>=0))clipped.append(previous+(current-previous)*(d0/(d0-d1)));
                if(d1>=0)clipped.append(current);previous=current;d0=d1;
            }
            polygon=std::move(clipped);
        }
        if(!polygon.isEmpty())return true;
    }
    // The complete frustum can fit inside a large/high map bounds box.
    const auto inverse=clip.inverted();
    for(int i=0;i<8;++i){auto p=inverse*QVector4D((i&1)?1:-1,(i&2)?1:-1,(i&4)?1:-1,1);if(qAbs(p.w())<1e-8)continue;p/=p.w();
        if(p.x()>=0&&p.x()<=width&&p.y()>=0&&p.y()<=height&&p.z()>=low&&p.z()<=high)return true;}
    return false;
}
}
void CView::ensureMapOpeningCamera(const QFileInfo& file) {
    opening::Scope timing("camera.validate",file.fileName());
    const auto serial=property("openingSerial").toULongLong();
    const auto data=ei::openingCpuValue(property("rebuild64Selected").toBool(),[file]{
        opening::Scope timing("cpu.camera.map_bounds");CMprDocument doc;CValidationReport report;
        const bool valid=doc.load(file,&report)&&!report.hasErrors()&&doc.mapInfo().valid;
        CTerrainEditModel terrain;STerrainHeightRange range;float centreZ=0;
        if(valid&&terrain.load(doc,&report)){range=terrain.heightRange();centreZ=float(terrain.height(terrain.vertexCountX()/2,terrain.vertexCountY()/2))*range.worldUnitsPerRawStep;}
        return std::make_tuple(valid,doc.mapInfo(),range,centreZ);
    });
    if(!std::get<0>(data)||property("openingSerial").toULongLong()!=serial||m_applicationShutdown)return;
    const auto info=std::get<1>(data);const auto range=std::get<2>(data);const float w=info.sectorCountX*32.0f,h=info.sectorCountY*32.0f;
    QMatrix4x4 lens=m_projection;
    if(m_width<=0||m_height<=0){lens.setToIdentity();lens.perspective(45,float(qMax(1,width()))/qMax(1,height()),1,1000);}
    if(intersects(lens*m_cam->viewMatrix(),w,h,range.minimumWorld,range.maximumWorld))return;
    if(auto* main=qobject_cast<MainWindow*>(window())) {
        const auto path=main->openingCameraPath(file);CCameraDocument camera;QString error;
        if(!path.isEmpty()&&camera.open(path,path,&error)&&camera.count()>0) {
            const auto view=camera.pose(0).view();
            if(intersects(lens*view,w,h,range.minimumWorld,range.maximumWorld)){m_cam->adoptView(view);opening::span("camera.start_cam",opening::clock(),path);return;}
        }
    }
    // New maps have no map.txt / CAM. Use the regular editor orientation at
    // the new map's centre instead of carrying coordinates of the old world.
    m_cam->clearPreviewView();m_cam->reset();QVector3D p,pivot,rotation;m_cam->getLocation(p,pivot,rotation);
    if(qAbs(rotation.x())>=89.0f){rotation.setX(-70.0f);m_cam->setLocation(p,pivot,rotation);}
    const QVector3D centre(w*.5f,h*.5f,std::get<3>(data));
    const auto view=m_cam->viewMatrix();const auto local=view.inverted().map(QVector3D(0,0,-40));
    p+=centre-local;pivot+=centre-local;m_cam->setLocation(p,pivot,rotation);
    opening::span("camera.new_map_default",opening::clock(),file.fileName());
}
