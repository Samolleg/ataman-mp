#include "view.h"
#include "camera.h"
#include "utils.h"
#include "shader_locations.h"
#include "gl_dispatch.h"

bool CView::captureCamPose(SCameraPose* pose) const
{
    return m_cam && SCameraPose::fromView(m_cam->viewMatrix(), pose);
}

void CView::setCameraTrack(const QVector<SCameraPose>& poses, int selected, bool preview)
{
    m_cameraTrackLines.clear();
    m_camPreviewActive = preview && selected >= 0 && selected < poses.size();
    if(m_camPreviewActive) m_cam->setPreviewView(poses[selected].view());
    else m_cam->clearPreviewView();
    updateCameraProjection();
    for(int index = 0; index < poses.size(); ++index) {
        const auto& pose = poses[index];
        if(index > 0) m_cameraTrackLines << poses[index-1].position << pose.position;
        const float scale = index == selected ? 1.0f : 0.25f;
        const QVector3D forward = pose.forward();
        const QVector3D up = pose.up();
        const QVector3D right = QVector3D::crossProduct(forward, up);
        const QVector3D center = pose.position + forward * scale;
        const QVector3D corners[] = {center + (right+up*0.65f)*scale,
            center + (-right+up*0.65f)*scale, center + (-right-up*0.65f)*scale,
            center + (right-up*0.65f)*scale};
        for(int side = 0; side < 4; ++side)
            m_cameraTrackLines << pose.position << corners[side]
                               << corners[side] << corners[(side+1)%4];
        if(index == selected) m_cameraTrackLines << pose.position << pose.position+forward*3.0f;
    }
    update();
}

void CView::drawCameraTrack(int positionAttribute)
{
    if(m_cameraTrackLines.isEmpty() || m_camPreviewActive || m_minimapCaptureActive) return;
    m_program.setUniformValue(ei::uniformLocation(&m_program, "customColor"), QVector4D(1.0f,0.7f,0.1f,1.0f));
    m_program.setAttributeArray(positionAttribute, m_cameraTrackLines.constData());
    ei::gl::DrawArrays(GL_LINES, 0, m_cameraTrackLines.size());
}
