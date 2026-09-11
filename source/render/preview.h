#ifndef PREVIEW_H
#define PREVIEW_H

#include <QGLWidget>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include "types.h"

class CNode;
class CView;
class CCamera;
class CSettings;
class QMouseEvent;

///
/// \brief The CPreview class provides a display of the currently created 3D scene node (new object creation form).
///
class CPreview : public QGLWidget
{
    Q_OBJECT

public:
    explicit CPreview(QWidget* parent=nullptr, const QGLWidget* shareWidget=nullptr);
    //explicit CPreview(QWidget* parent=nullptr, const QGLWidget* pShareWidget=nullptr);
    void attachView(CView* pView) {m_pView = pView;}
    void attachNode(CNode* pNode, bool resetView = true);
    void resetView(const QVector3D& cameraRotation = QVector3D(-70.0f, 0.0f, -45.0f),
                   float modelPitch = 0.0f, float modelYaw = 0.0f);
    void attachSettings(CSettings* pSet);
    ~CPreview();

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
//    void keyPressEvent(QKeyEvent* event) override;
//    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void draw();

private slots:
    void refreshCam(CBox nodePos);

private:
    CNode* m_pNode;
    int m_height;
    int m_width;
    QMatrix4x4 m_projection;
    QOpenGLShaderProgram m_program;
    CView* m_pView;
    QSharedPointer<CCamera> m_cam;
    QPoint m_lastMouse;
    Qt::MouseButton m_dragButton{Qt::NoButton};
    QVector3D m_pan;
    float m_modelYaw{0.0f};
    float m_modelPitch{0.0f};
    QVector3D m_resetCameraRotation{-70.0f,0.0f,-45.0f};
};


#endif // PREVIEW_H
