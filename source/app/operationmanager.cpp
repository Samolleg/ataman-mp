#include <QLineEdit>
#include <QTimer>

#include <cmath>

#include "operationmanager.h"
#include "landscape_edit_policy.h"
#include "node.h"
#include "view.h"
#include "landscape.h"
#include "key_manager.h"
#include "settings.h"
#include "camera.h"
#include "layout_components/connectors_ui.h"
#include "log.h"
#include "scene.h"
#include "undo.h"
#include "round_mob_form.h"
#include "scene.h"

void strToOperValue(QVector3D& vec, const EOperateAxis axis, const QString& value)
{
    vec = QVector3D(0.0f, 0.0f, 0.0f);
    if(value.isEmpty())
    {
        ei::log(eLogDebug, "Operation has invalid value");
        return;
    }
    switch (axis)
    {
    case EOperateAxisX:
        vec.setX(value.toFloat());
        break;
    case EOperateAxisY:
        vec.setY(value.toFloat());
        break;
    case EOperateAxisZ:
        vec.setZ(value.toFloat());
        break;
    case EOperateAxisXY:
    {
        vec.setX(value.toFloat());
        vec.setY(value.toFloat());
        break;
    }
    case EOperateAxisXZ:
    {
        vec.setX(value.toFloat());
        vec.setY(value.toFloat());
        break;
    }
    case EOperateAxisYZ:
    {
        vec.setY(value.toFloat());
        vec.setZ(value.toFloat());
        break;
    }
    }
}

void CSelect::keyPress(COperation *pOp, EKeyCode key)
{
    switch (key) {
    case eKey_Esc:
    {
        m_pView->clearObjectSelection();
        break;
    }
    case eKey_G:
    {
        pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisXY));
        delete this;
        break;
    }
    case eKey_R:
    {
        if(!pOp->keyManager()->isPressed(eKey_Ctrl))
        {
            pOp->setCurrent(new CRotateAxis(m_pView, EOperateAxisZ));
            delete this;
        }
        break;
    }
    case eKey_T:
    {
        if (pOp->keyManager()->isPressed(eKey_Ctrl))
        {
            m_pView->iterateRoundMob();
        }
        break;
    }
    case eKey_Delete:
    {
        m_pView->deleteSelectedNodes();
        //pOp->setCurrent(new CScaleAxis(m_pView, EOperateAxisZ));
        break;
    }
    case eKey_C:
    {
        if (pOp->keyManager()->isPressed(eKey_Ctrl))
            m_pView->selectedObjectToClipboardBuffer();
        break;
    }
    case eKey_V:
    {
        if (pOp->keyManager()->isPressed(eKey_Ctrl))
            m_pView->clipboradObjectsToScene();
        break;
    }
    case eKey_H:
    {
        if (pOp->keyManager()->isPressed(eKey_Alt))
            m_pView->unHideAll();
        else if(!pOp->keyManager()->isPressed(eKey_Ctrl))
            m_pView->hideSelectedNodes();

        break;
    }
    case eKey_P:
    {
        if(!pOp->keyManager()->isPressed(eKey_Ctrl)
           && CScene::getInstance()->getMode() == eEditModeLogic)
            m_pView->addLogicPoint(false);
        break;
    }
    case eKey_L:
    {
        if(!pOp->keyManager()->isPressed(eKey_Ctrl)
           && !pOp->keyManager()->isPressed(eKey_Alt)
           && CScene::getInstance()->getMode() == eEditModeLogic)
            m_pView->addLogicPoint(true);
        break;
    }
    case eKey_U:
    {
        m_pView->execUnloadCommand();
        break;
    }
    case eKey_X:
    {
        if (pOp->keyManager()->isPressed(eKey_Ctrl))
        {
            m_pView->selectedObjectToClipboardBuffer();
            m_pView->deleteSelectedNodes();
        }
        break;
    }
    case eKey_NumDot:
    {
        m_pView->moveCamToSelectedObjects();
        break;
    }
    default:
        pOp->keyManager()->press(key);
        break;
    }
}

void CSelect::keyRelease(COperation *pOp, EKeyCode key)
{
    pOp->keyManager()->release(key);
    switch (key) {
    default:
        break;
    }
}

void CSelect::mousePressEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->buttons()) {
    case Qt::LeftButton:
    {
        pOp->keyManager()->press(eKey_special1);
//        m_pView->pickObject(event->pos(), pOp->keyManager()->isPressed(eKey_Shift));
//        m_pView->viewParameters();
        break;
    }
    case Qt::RightButton:
    {
        break;
    }
    case Qt::MiddleButton:
    {
        // Orbit the scene point currently under the viewport center. The
        // inherited camera otherwise retains an old world-space pivot until
        // another command explicitly recenters it.
        if(!m_pView->telephotoCameraEnabled())
            m_pView->anchorCameraRotationToViewportCenter();
        break;
    }
    }
    m_lastPos = pEvent->pos();
}

void CSelect::mouseReleaseEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->button()) {
    case Qt::LeftButton:
    {
        pOp->keyManager()->release(eKey_special1);
        const QPoint topLeft(m_lastPos.x() < pEvent->pos().x() ? m_lastPos.x() : pEvent->pos().x(), m_lastPos.y() > pEvent->pos().y() ? m_lastPos.y() : pEvent->pos().y());
        const QPoint bottomRight(m_lastPos.x() > pEvent->pos().x() ? m_lastPos.x() : pEvent->pos().x(), m_lastPos.y() < pEvent->pos().y() ? m_lastPos.y() : pEvent->pos().y());
        QRect rect(topLeft, bottomRight);
        m_pView->pickObject(rect, pOp->keyManager()->isPressed(eKey_Shift));
        m_pView->viewParameters();

        break;
    }
    default: //TODO
    {
        int d(777);
        Q_UNUSED(d);
    }
        break;
    }
    m_lastPos = pEvent->pos();
}

void rotateAroundPivot(CCamera* pCam, int dx, int dy, int senseX, int senseY)
{
    // rotate around pivot
;
    float coef = 12.0f-senseY/10.0f;
    float angle = float(dy)/coef;
    pCam->xRotate(angle);
    coef = 12.0f-senseX/10.0f;
    angle = float(dx)/coef;
    pCam->zRotate(angle);

}

void CSelect::mouseMoveEvent(COperation *pOp, QMouseEvent *pEvent)
{
    const int dx = pEvent->x() - m_lastPos.x();
    const int dy = pEvent->y() - m_lastPos.y();

    if (pEvent->buttons() & Qt::MiddleButton)
    {
        if(m_pView->telephotoCameraEnabled())
        {
            m_lastPos = pEvent->pos();
            return;
        }
        // rotate around pivot
        int senseX = dynamic_cast<COptInt*>(m_pView->settings()->opt(eOptSetGeneral, "mouseSenseX"))->value();
        int senseY = dynamic_cast<COptInt*>(m_pView->settings()->opt(eOptSetGeneral, "mouseSenseY"))->value();
        rotateAroundPivot(pOp->camera(), dx, dy, senseX, senseY);
        m_lastPos = pEvent->pos();
    }
    else if (pEvent->buttons() & Qt::LeftButton)
    {
        //todo. QRubberBand
        QPoint leftTop (m_lastPos.x() < pEvent->pos().x() ? m_lastPos.x() : pEvent->pos().x(), m_lastPos.y() > pEvent->pos().y() ? m_lastPos.y() : pEvent->pos().y());
        QPoint leftBottom(m_lastPos.x() < pEvent->pos().x() ? m_lastPos.x() : pEvent->pos().x(), m_lastPos.y() < pEvent->pos().y() ? m_lastPos.y() : pEvent->pos().y());
        QPoint rightButtom (m_lastPos.x() > pEvent->pos().x() ? m_lastPos.x() : pEvent->pos().x(), m_lastPos.y() < pEvent->pos().y() ? m_lastPos.y() : pEvent->pos().y());
        QPoint rightTop(m_lastPos.x() > pEvent->pos().x() ? m_lastPos.x() : pEvent->pos().x(), m_lastPos.y() > pEvent->pos().y() ? m_lastPos.y() : pEvent->pos().y());
        //QRect rect(leftTop, rightButtom);
        QRect rect(leftBottom, rightTop);
        m_pView->drawSelectFrame(rect);

    }
    QVector3D mouseCoord (m_pView->getTerrainPos(pEvent->x(), pEvent->y()));
    QString text = "not defined";
    if(mouseCoord.z() > -0.1f && mouseCoord.z() < 100.0f)
        text = util::makeString(mouseCoord, true);
    pOp->updateMouseCoords(text);
}

void CSelect::wheelEvent(COperation *pOp, QWheelEvent* pEvent)
{
    const int step = landscapeWheelStep(pEvent->angleDelta().y(),
                                        pEvent->delta(), pEvent->pixelDelta().y());
    if(step != 0)
        pOp->camera()->enlarge(step > 0);
}

CMoveAxis::CMoveAxis(CView *pView, EOperateAxis ax)
    :CState(pView)
{
    qDebug()<< "CMoveAxis init with: " << ax;
    axis = ax;
    m_lastPos = m_pView->operationAnchor();
    lastLandPos = m_pView->getTerrainPos(m_lastPos.x(), m_lastPos.y());
    pView->operationSetBackup(EOperationAxisType::eMove);
    CStatusConnector::getInstance()->updateStatus("move.ico", "shift+Z(x,y) = exclude Z(x,y,) axis, X(y,z) - move X(y,z), Esc - Cancel, LMB - apply");
    CButtonConnector::getInstance()->pressButton(EButtonOpMove);
}

void CMoveAxis::keyPress(COperation *pOp, EKeyCode key)
{
    if(key==eKey_Tab && !pOp->keyManager()->isPressed(eKey_Ctrl)) {m_pView->cycleModelConnectionTarget();return;}
    if(m_pView->isLogicPlacementPending()) {
        if(key == eKey_Esc) m_pView->cancelPendingOperation();
        // Placement is confirmed by a map click, not transform-axis/numeric keys.
        return;
    }
    switch (key) {
    case eKey_Esc:
    {
        qDebug() << "exit moveAxis operation";
        m_pView->cancelPendingOperation();
        break;
    }
    case eKey_X:
    {
        m_pView->operationRevert(EOperationAxisType::eMove);
        if(pOp->keyManager()->isPressed(eKey_Shift))
            pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisYZ));
        else
            pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisX));
        delete this;
        break;
    }
    case eKey_Y:
    {
        m_pView->operationRevert(EOperationAxisType::eMove);
        if(pOp->keyManager()->isPressed(eKey_Shift))
            pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisXZ));
        else
            pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisY));
        delete this;
        break;
    }
    case eKey_Z:
    {
        m_pView->operationRevert(EOperationAxisType::eMove);
        if(pOp->keyManager()->isPressed(eKey_Shift))
            pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisXY));
        else
            pOp->setCurrent(new CMoveAxis(m_pView, EOperateAxisZ));
        delete this;
        break;
    }
    case eKey_Minus:
    case eKey_NumMinus:
    {
        value += "-";
        break;
    }
    case eKey_Period:
    case eKey_NumDot:
    {
        value += ".";
        qDebug() << value << endl;
        break;
    }
    case eKey_0:
    case eKey_1:
    case eKey_2:
    case eKey_3:
    case eKey_4:
    case eKey_5:
    case eKey_6:
    case eKey_7:
    case eKey_8:
    case eKey_9:
    {
        //todo: show value in ui
        value += QString().number(key-eKey_0); //get key as digit
        qDebug() << value << endl;
        m_pView->operationRevert(EOperationAxisType::eMove);
        QVector3D dir(0.0f, 0.0f, 0.0f);
        strToOperValue(dir, axis, value);
        m_pView->moveTo(dir);
        break;
    }
    case eKey_Backspace:
    {
        if(!value.isEmpty())
            value.remove(value.length()-1, 1);
        qDebug() << value << endl;
        if(value.isEmpty())
            break;

        m_pView->operationRevert(EOperationAxisType::eMove);
        QVector3D dir;
        strToOperValue(dir, axis, value);
        m_pView->moveTo(dir);
        break;
    }
    case eKey_Enter:
    {
        if(!value.isEmpty() || m_pView->isDatabasePlacementPending() || m_pView->isLogicPlacementPending())
            m_pView->operationApply(EOperationAxisType::eMove);
        pOp->setCurrent(new CSelect(m_pView));
        delete this;
        break;
    }
    default:
    {
        pOp->keyManager()->press(key);
        break;
    }
    }
}

void CMoveAxis::keyRelease(COperation *pOp, EKeyCode key)
{
    switch (key) {
    default:
    {
        pOp->keyManager()->release(key);
        break;
    }
    }
}

void CMoveAxis::axisOperate(COperation *pOp, CNode *pNode)
{
    qDebug() << "running operation move axis " + QString::number(axis) + " to " + QString::number(pNode->mapId()) + " with value: " + value;
    if(value.isEmpty() || !pNode)
    {
        qDebug() << "operation is invalid (empty)";
    }
    else
    {
        QVector3D moveDir(pNode->position());
        switch (axis) {
        case EOperateAxisX:
            moveDir.setX(moveDir.x() + value.toFloat()); break;
        case EOperateAxisY:
            moveDir.setY(moveDir.y() + value.toFloat()); break;
        case EOperateAxisZ:
            moveDir.setZ(moveDir.z() + value.toFloat()); break;
        case EOperateAxisXY:
        {
            moveDir.setX(moveDir.x() + value.toFloat());
            moveDir.setY(moveDir.y() + value.toFloat());
            break;
        }
        case EOperateAxisXZ:
        {
            moveDir.setX(moveDir.x() + value.toFloat());
            moveDir.setZ(moveDir.z() + value.toFloat());
            break;
        }
        case EOperateAxisYZ:
        {
            moveDir.setY(moveDir.y() + value.toFloat());
            moveDir.setZ(moveDir.z() + value.toFloat());
            break;
        }
        }

        pNode->updatePos(moveDir);
    }
    m_pView->operationApply(EOperationAxisType::eMove);
    pOp->setCurrent(new CSelect(m_pView));
    delete this;
}

void CMoveAxis::mousePressEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->button()) {
    case Qt::LeftButton:
    {
        Q_UNUSED(pOp);
        m_placementPress = true;
        break;
    }
    default:
    {
        int d(777);
        Q_UNUSED(d);
        break;
    }
    }
}

void CMoveAxis::mouseReleaseEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->button()) {
    case Qt::LeftButton:
    {
        if(m_pView->isLogicPlacementPending()) {
            const bool pressed = m_placementPress;
            m_placementPress = false;
            if(!pressed || !m_pView->updateLogicPlacementPointer(pEvent->pos())) return;
        }
        if(m_pView->isDatabasePlacementPending()
           && !m_pView->updateDatabasePlacementPointer(pEvent->pos())) return;
        m_pView->operationApply(EOperationAxisType::eMove);
        pOp->setCurrent(new CSelect(m_pView));
        delete this;
        break;
    }
    default:
    {
        int d(777);
        Q_UNUSED(d);
        break;
    }
    }

}

void CMoveAxis::synchronizePointerAfterCameraNavigation(const QPoint& position)
{
    m_lastPos = position;
    lastLandPos = m_pView->getTerrainPos(position.x(), position.y());
}

void CMoveAxis::mouseMoveEvent(COperation *pOp, QMouseEvent *pEvent)
{
    Q_UNUSED(pOp);
    if(m_pView->isLogicPlacementPending()) {
        m_pView->updateLogicPlacementPointer(pEvent->pos());
        return;
    }
    if(m_pView->isDatabasePlacementPending())
    {
        m_pView->updateDatabasePlacementPointer(pEvent->pos());
        return;
    }
    QVector3D newPos = m_pView->getTerrainPos(pEvent->x(), pEvent->y());
    QVector3D moveDir = newPos - lastLandPos;
    switch (axis) {
    case EOperateAxisX:
    {
        moveDir.setY(0.0f);
        moveDir.setZ(0.0f);
        break;
    }
    case EOperateAxisY:
    {
        moveDir.setX(0.0f);
        moveDir.setZ(0.0f);
        break;
    }
    case EOperateAxisZ:
    {
        //double trueLength = sqrt(pow(x(), 2) + pow(y(), 2));
        //QLineF line(QLine (event->pos(), m_lastPos));
        float dif = (m_lastPos.y() - pEvent->y())/50.0f;
        //moveDir = QVector3D(0.0f, 0.0f, float(line.length())/50.0f);
        moveDir = QVector3D(0.0f, 0.0f, dif);
        m_lastPos = pEvent->pos();
        break;
    }
    case EOperateAxisXY:
        moveDir.setZ(0.0f); break;
    case EOperateAxisXZ:
        moveDir.setY(0.0f); break;
    case EOperateAxisYZ:
        moveDir.setX(0.0f); break;
    }
    m_pView->moveTo(moveDir);
    m_pView->updateModelMagnet(pEvent->pos());
    //m_pView->viewParameters();
    lastLandPos = newPos;
}

CRotateAxis::CRotateAxis(CView *pView, EOperateAxis ax)
    :CState(pView)
{
    qDebug()<< "CRotateAxis init with: " << ax;
    axis = ax;
    m_lastPos = m_pView->operationAnchor();
    pView->operationSetBackup(EOperationAxisType::eRotate);
    CStatusConnector::getInstance()->updateStatus("rotate.ico", "Z(x,y) - rotate Z(x,y). Esc - Cancel, LMB - apply");
    CButtonConnector::getInstance()->pressButton(EButtonOpRotate);
}


void CRotateAxis::keyPress(COperation *pOp, EKeyCode key)
{
    switch (key) {
    case eKey_Esc:
    {
        qDebug() << "exit CRotateAxis operation";
        m_pView->cancelPendingOperation();
        break;
    }
    case eKey_Minus:
    case eKey_NumMinus:
    {
        value += "-";
        break;
    }
    case eKey_Period:
    case eKey_NumDot:
    {
        value += ".";
        qDebug() << value << endl;
        break;
    }
    case eKey_0:
    case eKey_1:
    case eKey_2:
    case eKey_3:
    case eKey_4:
    case eKey_5:
    case eKey_6:
    case eKey_7:
    case eKey_8:
    case eKey_9:
    {
        value += QString().number(key-eKey_0); //get key as digit
        qDebug() << value << endl;
        m_pView->operationRevert(EOperationAxisType::eRotate);
        QVector3D rot(0.0f, 0.0f, 0.0f);
        strToOperValue(rot, axis, value);
        m_pView->rotateTo(rot);
        break;
    }
    case eKey_Backspace:
    {
        if(!value.isEmpty())
            value.remove(value.length()-1, 1);
        qDebug() << value << endl;
        m_pView->operationRevert(EOperationAxisType::eRotate);
        if(value.isEmpty())
            break;
        QVector3D rot(0.0f, 0.0f, 0.0f);
        strToOperValue(rot, axis, value);
        m_pView->rotateTo(rot);
        break;
    }
    case eKey_Enter:
    {
        m_pView->operationApply(EOperationAxisType::eRotate);
        pOp->setCurrent(new CSelect(m_pView));
        delete this;
        break;
    }
    case eKey_X:
    {
        m_pView->operationRevert(EOperationAxisType::eRotate);
        pOp->setCurrent(new CRotateAxis(m_pView, EOperateAxisX));
        delete this;
        break;
    }
    case eKey_Y:
    {
        m_pView->operationRevert(EOperationAxisType::eRotate);
        pOp->setCurrent(new CRotateAxis(m_pView, EOperateAxisY));
        delete this;
        break;
    }
    case eKey_Z:
    {
        m_pView->operationRevert(EOperationAxisType::eRotate);
        pOp->setCurrent(new CRotateAxis(m_pView, EOperateAxisZ));
        delete this;
        break;
    }
    default:
    {
        pOp->keyManager()->press(key);
        break;
    }
    }
}

void CRotateAxis::keyRelease(COperation *pOp, EKeyCode key)
{
    switch (key) {
    default:
    {
        pOp->keyManager()->release(key);
        break;
    }
    }
}

void CRotateAxis::mousePressEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->buttons()) {
    case Qt::LeftButton:
    {
        Q_UNUSED(pOp);
        break;
    }
    default:
    {
        int d(777);
        Q_UNUSED(d);
        break;
    }
    }
}

void CRotateAxis::mouseReleaseEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->button()) {
    case Qt::LeftButton:
    {
        m_pView->operationApply(EOperationAxisType::eRotate);
        pOp->setCurrent(new CSelect(m_pView));
        delete this;
        break;
    }
    default:
    {
        int d(777);
        Q_UNUSED(d);
        break;
    }
    }
}

void CRotateAxis::mouseMoveEvent(COperation *pOp, QMouseEvent *pEvent)
{
    Q_UNUSED(pOp);
    float dif = (pEvent->x() - m_lastPos.x())/10.0f;
    QVector3D rot(0.0f, 0.0f, 0.0f); //euler angles for add rotation;
    switch (axis) {
    case EOperateAxisX:
        rot.setX(dif); break;
    case EOperateAxisY:
        rot.setY(dif); break;
    case EOperateAxisZ:
        rot.setZ(dif); break;
    case EOperateAxisXY:
    case EOperateAxisXZ:
    case EOperateAxisYZ:
    {
        qDebug() << "not defined";
    }
    }
    m_pView->rotateTo(rot);
    //m_pView->viewParameters();
    m_lastPos = pEvent->pos();
}

void CRotateAxis::axisOperate(COperation *pOp, CNode *pNode)
{
    qDebug() << "running operation rotate axis " + QString::number(axis) + " to " + QString::number(pNode->mapId()) + " with value: " + value;
    if(value.isEmpty() || !pNode)
    {
        qDebug() << "operation is invalid (empty)";
    }
    else
    {
        const float gimbalAvoidStep = 0.001f;
        QVector3D rotation(pNode->getEulerRotation());
        QQuaternion quat;
        QVector3D eulerRot;
        const auto setIsNan = [&quat, &eulerRot](QVector3D& rot)
        {
            quat = QQuaternion::fromEulerAngles(rot);
            eulerRot = quat.toEulerAngles();
            if(eulerRot.x() == eulerRot.x() && eulerRot.y() == eulerRot.y() && eulerRot.z() == eulerRot.z())
                return false;
            return true;
        };

        switch (axis) {
        case EOperateAxisX:
        {
            rotation.setX(rotation.x() + value.toFloat());
            for(int i(0); i<100; ++i) //avoid gimbal lock
                if (setIsNan(rotation))
                    rotation.setX(rotation.x() + gimbalAvoidStep);
                else
                    break;
            break;
        }
        case EOperateAxisY:
        {
            rotation.setY(rotation.y() + value.toFloat());
            for(int i(0); i<100; ++i) //avoid gimbal lock
                if (setIsNan(rotation))
                    rotation.setY(rotation.y() + gimbalAvoidStep);
                else
                    break;
            break;
        }
        case EOperateAxisZ:
        {
            rotation.setZ(rotation.z() + value.toFloat());
            for(int i(0); i<100; ++i) //avoid gimbal lock
                if (setIsNan(rotation))
                    rotation.setZ(rotation.z() + gimbalAvoidStep);
                else
                    break;
            break;
        }
        case EOperateAxisXY:
        {
            rotation.setX(rotation.x() + value.toFloat());
            rotation.setY(rotation.y() + value.toFloat());
            for(int i(0); i<200; ++i) //avoid gimbal lock
            {
                if (!setIsNan(rotation))
                    break;

                if (i%2 == 0)
                    rotation.setX(rotation.x() + gimbalAvoidStep);
                else
                    rotation.setY(rotation.y() + gimbalAvoidStep);
            }
            break;
        }
        case EOperateAxisXZ:
        {
            rotation.setX(rotation.x() + value.toFloat());
            rotation.setZ(rotation.z() + value.toFloat());
            for(int i(0); i<200; ++i) //avoid gimbal lock
            {
                if (!setIsNan(rotation))
                    break;

                if (i%2 == 0)
                    rotation.setX(rotation.x() + gimbalAvoidStep);
                else
                    rotation.setZ(rotation.z() + gimbalAvoidStep);
            }
            break;
        }
        case EOperateAxisYZ:
        {
            rotation.setY(rotation.y() + value.toFloat());
            rotation.setZ(rotation.z() + value.toFloat());
            for(int i(0); i<200; ++i) //avoid gimbal lock
            {
                if (!setIsNan(rotation))
                    break;

                if (i%2 == 0)
                    rotation.setY(rotation.y() + gimbalAvoidStep);
                else
                    rotation.setZ(rotation.z() + gimbalAvoidStep);
            }
            break;
        }
        }
        pNode->setRot(quat);
        m_pView->viewParameters();
    }
    m_pView->operationApply(EOperationAxisType::eRotate);
    pOp->setCurrent(new CSelect(m_pView));
    delete this;
}

CScaleAxis::CScaleAxis(CView *pView, EOperateAxis ax)
    :CState(pView)
{
    qDebug()<< "CScaleAxis init with: " << ax;
    axis = ax;
    m_lastPos = m_pView->operationAnchor();
    pView->operationSetBackup(EOperationAxisType::eScale);
    CStatusConnector::getInstance()->updateStatus("scale.ico", "X(y,z) - scale X(y,z), shift+Z(x,y) = exclude Z(x,y,) axis. Esc - Cancel, LMB - apply");
    CButtonConnector::getInstance()->pressButton(EButtonOpScale);
}


void CScaleAxis::keyPress(COperation *pOp, EKeyCode key)
{
    switch (key) {
    case eKey_Esc:
    {
        qDebug() << "exit CScaleAxis operation";
        m_pView->cancelPendingOperation();
        break;
    }
    case eKey_Minus:
    case eKey_NumMinus:
    {
        value += "-";
        break;
    }
    case eKey_Period:
    case eKey_NumDot:
    {
        value += ".";
        qDebug() << value << endl;
        break;
    }
    case eKey_0:
    case eKey_1:
    case eKey_2:
    case eKey_3:
    case eKey_4:
    case eKey_5:
    case eKey_6:
    case eKey_7:
    case eKey_8:
    case eKey_9:
    {
        value += QString().number(key-eKey_0); //get key as digit
        qDebug() << value << endl;
        m_pView->operationRevert(EOperationAxisType::eScale);
        QVector3D scale(0.0f, 0.0f, 0.0f);
        strToOperValue(scale, axis, value);
        m_pView->scaleTo(scale);
        break;
    }
    case eKey_Backspace:
    {
        if(!value.isEmpty())
            value.remove(value.length()-1, 1);
        qDebug() << value << endl;
        if(value.isEmpty())
            break;

        m_pView->operationRevert(EOperationAxisType::eScale);
        QVector3D scale(0.0f, 0.0f, 0.0f);
        strToOperValue(scale, axis, value);
        m_pView->scaleTo(scale);
        break;
    }
    case eKey_Enter:
    {
        if(!value.isEmpty())
            m_pView->operationApply(EOperationAxisType::eScale);
        pOp->setCurrent(new CSelect(m_pView));
        delete this;
        break;
    }
    case eKey_X:
    {
        m_pView->operationRevert(EOperationAxisType::eScale);
        pOp->setCurrent(new CScaleAxis(m_pView, EOperateAxisX));
        delete this;
        break;
    }
    case eKey_Y:
    {
        m_pView->operationRevert(EOperationAxisType::eScale);
        pOp->setCurrent(new CScaleAxis(m_pView, EOperateAxisY));
        delete this;
        break;
    }
    case eKey_Z:
    {
        m_pView->operationRevert(EOperationAxisType::eScale);
        pOp->setCurrent(new CScaleAxis(m_pView, EOperateAxisZ));
        delete this;
        break;
    }
    default:
    {
        pOp->keyManager()->press(key);
        break;
    }
    }
}

void CScaleAxis::keyRelease(COperation *pOp, EKeyCode key)
{
    switch (key) {
    default:
    {
        pOp->keyManager()->release(key);
        break;
    }
    }
}

void CScaleAxis::axisOperate(COperation *pOp, CNode *pNode)
{
    qDebug() << "running operation scale axis " + QString::number(axis) + " to " + QString::number(pNode->mapId()) + " with value: " + value;
    if(value.isEmpty() || !pNode)
    {
        qDebug() << "operation is invalid (empty)";
    }
    else
    {

        QVector3D scale(pNode->complection());
        switch (axis) {
        case EOperateAxisX:
            scale.setX(scale.x() + value.toFloat()); break;
        case EOperateAxisY:
            scale.setY(scale.y() + value.toFloat()); break;
        case EOperateAxisZ:
            scale.setZ(scale.z() + value.toFloat()); break;
        case EOperateAxisXY:
        {
            scale.setX(scale.x() + value.toFloat());
            scale.setY(scale.y() + value.toFloat());
            break;
        }
        case EOperateAxisXZ:
        {
            scale.setX(scale.x() + value.toFloat());
            scale.setZ(scale.z() + value.toFloat());
            break;
        }
        case EOperateAxisYZ:
        {
            scale.setY(scale.y() + value.toFloat());
            scale.setZ(scale.z() + value.toFloat());
            break;
        }
        }

        pNode->setConstitution(scale);
        m_pView->viewParameters();
    }
    m_pView->operationApply(EOperationAxisType::eScale);
    pOp->setCurrent(new CSelect(m_pView));
    delete this;
}

void CScaleAxis::mousePressEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->buttons()) {
    case Qt::LeftButton:
    {
        Q_UNUSED(pOp);
        break;
    }
    default:
        break;
    }

}

void CScaleAxis::mouseReleaseEvent(COperation *pOp, QMouseEvent *pEvent)
{
    switch (pEvent->button()) {
    case Qt::LeftButton:
    {
        m_pView->operationApply(EOperationAxisType::eScale);
        pOp->setCurrent(new CSelect(m_pView));
        delete this;
        break;
    }
    default:
        break;
    }
}

void CScaleAxis::mouseMoveEvent(COperation *pOp, QMouseEvent *pEvent)
{
    Q_UNUSED(pOp);
    float dif = (pEvent->x() - m_lastPos.x())/200.0f;
    QVector3D scale(0.0f, 0.0f, 0.0f); //euler angles for add rotation;
    switch (axis) {
    case EOperateAxisX:
        scale.setX(dif); break;
    case EOperateAxisY:
        scale.setY(dif); break;
    case EOperateAxisZ:
        scale.setZ(dif); break;
    case EOperateAxisXY:
    case EOperateAxisXZ:
    case EOperateAxisYZ:
    {
        qDebug() << "not defined";
    }
    }
    m_pView->scaleTo(scale);
    m_lastPos = pEvent->pos();
}

COperation::COperation(CState *s)
    : current(s)
    ,m_cameraNavigationActive(false)
{
    //create key manager
    m_keyManager.reset(new CKeyManager());
}

void COperation::changeState(CState *state)
{
    m_cameraNavigationActive = false;
    if(nullptr!= current)
        delete current;
    current = state;
}

void COperation::mousePressEvent(QMouseEvent *pEvent)
{
    if(pEvent->button() == Qt::MiddleButton)
    {
        if(current && current->m_pView
           && current->m_pView->telephotoCameraEnabled())
        {
            pEvent->accept();
            return;
        }
        m_cameraNavigationActive = true;
        m_cameraNavigationLastPos = pEvent->pos();
        if(current && current->m_pView)
            current->m_pView->anchorCameraRotationToViewportCenter();
        pEvent->accept();
        return;
    }
    current->mousePressEvent(this, pEvent);
}

void COperation::mouseReleaseEvent(QMouseEvent *pEvent)
{
    if(pEvent->button() == Qt::MiddleButton && m_cameraNavigationActive)
    {
        m_cameraNavigationActive = false;
        if(current)
            current->synchronizePointerAfterCameraNavigation(pEvent->pos());
        pEvent->accept();
        return;
    }
    current->mouseReleaseEvent(this, pEvent);
}

void COperation::mouseMoveEvent(QMouseEvent *pEvent)
{
    if(m_cameraNavigationActive)
    {
        if(!(pEvent->buttons() & Qt::MiddleButton))
        {
            m_cameraNavigationActive = false;
            if(current)
                current->synchronizePointerAfterCameraNavigation(pEvent->pos());
        }
        else
        {
            const int dx = pEvent->x() - m_cameraNavigationLastPos.x();
            const int dy = pEvent->y() - m_cameraNavigationLastPos.y();
            if(current && current->m_pView && m_pCam)
            {
                const COptInt* senseX = dynamic_cast<const COptInt*>(
                    current->m_pView->settings()->opt(eOptSetGeneral, "mouseSenseX"));
                const COptInt* senseY = dynamic_cast<const COptInt*>(
                    current->m_pView->settings()->opt(eOptSetGeneral, "mouseSenseY"));
                rotateAroundPivot(m_pCam, dx, dy,
                                  senseX ? senseX->value() : 50,
                                  senseY ? senseY->value() : 50);
            }
            m_cameraNavigationLastPos = pEvent->pos();
            pEvent->accept();
            return;
        }
    }
    current->mouseMoveEvent(this, pEvent);
}

void COperation::wheelEvent(QWheelEvent* pEvent)
{
    // Camera zoom/height is independent of the current object transform.
    // Tile Brush retains Shift/Alt wheel ownership for its own controls.
    if(current && current->operationMethod() != eOperationMethodTileBrush)
    {
        if(m_pCam)
            m_pCam->enlarge(pEvent->delta() > 0);
        pEvent->accept();
        return;
    }
    current->wheelEvent(this, pEvent);
}

EOperationMethod COperation::operationMethod()
{
    return current->operationMethod();
}

void COperation::attachCam(CCamera *pCam)
{
    m_pCam = pCam;
}

void COperation::updateMouseCoords(const QString& pos)
{
    m_pMouseCoord->clear();
    m_pMouseCoord->setText(pos);
}

CSelect::CSelect(CView *pView):
    CState(pView)
{
    qDebug()<< "CSelect init";
    QTimer::singleShot(0, pView, &CView::refreshSelectContextHelp);
    CButtonConnector::getInstance()->pressButton(EButtonOpSelect);
}

CTileBrush::CTileBrush(CView* pView)
    :CState(pView)
{
    qDebug()<< "CTileBrush init ";
    CStatusConnector::getInstance()->updateStatus("brush.ico", "Esc-Cancel, LMB-paint, RMB-pick tile, Alt+Wheel-size, Shift+Wheel-material/rotate, Wheel-camera. M/O toggle main terrain/overlays.");
    CButtonConnector::getInstance()->pressButton(EButtonOpTilebrush);
    m_pView->showOutliner(false);
}

void CTileBrush::keyPress(COperation* pOp, EKeyCode key)
{
    switch (key) {
    case eKey_Esc:
    {
        qDebug() << "exit CTileBrush operation";
        m_pView->onRestoreCursor();
        m_pView->setPreviewTile(false);
        m_pView->showOutliner(true);
        m_pView->closeLandscapeTools();
        break;
    }
    case eKey_Z:
    case eKey_X:
    case eKey_C:
    {
        if(pOp->keyManager()->isPressed(eKey_Ctrl))
        {
            pOp->keyManager()->press(key);
            break;
        }
        m_pView->selectTerrainPaintPaletteMode(key == eKey_Z
            ? ETerrainPaintPaletteMode::Modern
            : key == eKey_X ? ETerrainPaintPaletteMode::ClassicAuto
                            : ETerrainPaintPaletteMode::Manual);
        break;
    }
    case eKey_1:
    case eKey_2:
    case eKey_3:
    case eKey_4:
    case eKey_5:
    case eKey_6:
    case eKey_7:
    case eKey_8:
    {
        m_pView->pickQuickAccessTile(key-eKey_1);
        const bool bLand = m_pView->terrainTileEditLayerIsLand();

        m_pView->updatePreviewTile(
            m_pView->getTerrainBrushPos(), bLand);
        break;
    }
    default:
    {
        pOp->keyManager()->press(key);
        break;
    }
    }
}

void CTileBrush::keyRelease(COperation* pOp, EKeyCode key)
{
    switch (key) {
    default:
    {
        pOp->keyManager()->release(key);
        break;
    }
    }
}

void CTileBrush::mousePressEvent(COperation* pOp, QMouseEvent* pEvent)
{
    Q_UNUSED(pOp);
    m_lastPos = pEvent->pos();
    const bool bLand = m_pView->terrainTileEditLayerIsLand();
    bool hit = false;
    m_lastLandPos = m_pView->getTerrainBrushPos(
        pEvent->x(), pEvent->y(), &hit, true);
    m_lastLandPosValid = hit;
    //m_lastLandPos = QVector3D(0.0f, 0.0f, 0.0f);// make sure that next mouse move apply tile brushing

    switch (pEvent->buttons()) {
    case Qt::LeftButton:
    {
        if(!hit)
            break;
        m_pView->beginTileBrushGroup();
        m_pView->setTile(m_lastLandPos, bLand);
        break;
    }
    case Qt::RightButton:
    {
        m_pView->beginTerrainPaintSampling();
        if(hit)
            m_pView->pickTile(m_lastLandPos, bLand);
        break;
    }
    }
}

void CTileBrush::mouseReleaseEvent(COperation* pOp, QMouseEvent* pEvent)
{
    Q_UNUSED(pOp);
    if(pEvent->button() == Qt::RightButton)
        m_pView->endTerrainPaintSampling();
    if (pEvent->button() == Qt::LeftButton) {
        const bool bLand = m_pView->terrainTileEditLayerIsLand();
        // Always resolve the release position. The view de-duplicates an
        // unchanged center, while a short/fast gesture cannot lose its tail.
        bool hit = false;
        const QVector3D position = m_pView->getTerrainBrushPos(
            pEvent->pos().x(), pEvent->pos().y(), &hit, true);
        if(hit)
        {
            // The initial press may have missed a disconnected overlay. The
            // first valid release sample still owns a complete undo stroke.
            m_pView->beginTileBrushGroup();
            m_pView->setTile(position, bLand);
        }
        m_pView->endTileBrushGroup();
    }
    if(!m_pView->isPreviewTile()) // if we finished brushing, we will come here
    {
        const bool bLand = m_pView->terrainTileEditLayerIsLand();
        bool hit = false;
        const QVector3D landPos = m_pView->getTerrainBrushPos(
            pEvent->pos().x(), pEvent->pos().y(), &hit);
        if(hit)
            m_pView->updatePreviewTile(landPos, bLand);
        m_lastLandPos = landPos;
        m_lastLandPosValid = hit;
    }
}

void CTileBrush::synchronizePointerAfterCameraNavigation(const QPoint& position)
{
    m_lastPos = position;
    const bool land = m_pView->terrainTileEditLayerIsLand();
    m_lastLandPos = m_pView->getTerrainPos(position.x(), position.y(), land);
    m_pView->updatePreviewTile(m_lastLandPos, land);
}

void CTileBrush::mouseMoveEvent(COperation* pOp, QMouseEvent* pEvent)
{
    const int dx = pEvent->x() - m_lastPos.x();
    const int dy = pEvent->y() - m_lastPos.y();

    if (pEvent->buttons() & Qt::MiddleButton)
    {
        int senseX = dynamic_cast<COptInt*>(m_pView->settings()->opt(eOptSetGeneral, "mouseSenseX"))->value();
        int senseY = dynamic_cast<COptInt*>(m_pView->settings()->opt(eOptSetGeneral, "mouseSenseY"))->value();
        rotateAroundPivot(pOp->camera(), dx, dy, senseX, senseY);
        m_lastPos = pEvent->pos();
    }
    else if (pEvent->buttons() & Qt::LeftButton)
    {
        if(m_pView->isPreviewTile())
            m_pView->setPreviewTile(false);

        const bool bLand = m_pView->terrainTileEditLayerIsLand();
        bool hit = false;
        const QVector3D landPos(m_pView->getTerrainBrushPos(
            pEvent->x(), pEvent->y(), &hit));
        if(!hit)
        {
            // A disconnected or thin overlay can legitimately have no pick
            // beneath the cursor. Never interpolate from the renderer's far
            // unprojection fallback: that creates a stroke across the map.
            m_lastLandPosValid = false;
            m_pView->endTerrainPreviewBatch();
            m_lastPos = pEvent->pos();
            return;
        }
        m_pView->beginTerrainPreviewBatch();
        // A drag can enter a valid projected surface after its press missed.
        // Begin is idempotent and guarantees every following stamp belongs to
        // the release-owned undo transaction.
        m_pView->beginTileBrushGroup();
        const float distance = m_lastLandPosValid
            ? landPos.distanceToPoint(m_lastLandPos) : 0.0f;
        const float spacing = m_pView->terrainPaintStampSpacing();
        if(!m_lastLandPosValid)
        {
            m_pView->setTile(landPos, bLand);
            m_lastLandPos = landPos;
            m_lastLandPosValid = true;
        }
        else if(distance >= spacing)
        {
            // Interpolate fast pointer movement so a small brush cannot leave
            // unpainted tile gaps between mouse events. All stamps remain in
            // the press/release undo macro.
            constexpr int maximumContinuousSteps = 32;
            const int steps = qMax(1, int(std::ceil(distance / spacing)));
            if(steps <= maximumContinuousSteps)
                for(int step = 1; step <= steps; ++step)
                {
                    const float ratio = float(step) / float(steps);
                    m_pView->setTile(m_lastLandPos * (1.0f - ratio) + landPos * ratio, bLand);
                }
            else
                // Treat a delayed/warped pointer event as a discontinuity.
                // The endpoint remains responsive without filling the gap.
                m_pView->setTile(landPos, bLand);
            m_lastLandPos = landPos;
        }
        m_pView->endTerrainPreviewBatch();
        m_pView->updatePreviewTile(landPos, bLand);

    }
    else if(pEvent->buttons() & Qt::RightButton)
    {
        const bool bLand = m_pView->terrainTileEditLayerIsLand();
        bool hit = false;
        const QVector3D landPos = m_pView->getTerrainBrushPos(
            pEvent->x(), pEvent->y(), &hit);
        if(hit)
        {
            if(m_pView->pickTile(landPos, bLand))
                m_pView->updatePreviewTile(landPos, bLand);
            m_lastLandPos = landPos;
            m_lastLandPosValid = true;
        }
        else
            m_lastLandPosValid = false;
    }
    else
    {// just moving mouse. Set tile preview mode
//        if(!m_pView->isPreviewTile())
//            m_pView->setPreviewTile();

        const bool bLand = m_pView->terrainTileEditLayerIsLand();
        bool hit = false;
        const QVector3D landPos = m_pView->getTerrainBrushPos(
            pEvent->x(), pEvent->y(), &hit);
        if(hit)
        {
            // A terrain cell is two world units wide, but its projected screen
            // boundary can be crossed after a much smaller world-space move.
            // The old one-unit throttle therefore left the outline on one cell
            // while mouse press freshly picked and painted its neighbour.
            // Re-pick every hover event; updatePreviewTile already performs the
            // authoritative cell/vertex snap used by the paint path.
            m_pView->updatePreviewTile(landPos, bLand);
            m_lastLandPos = landPos;
            m_lastLandPosValid = true;
        }
        else if(!hit)
            m_lastLandPosValid = false;

    }
    m_lastPos = pEvent->pos();
}

void CTileBrush::wheelEvent(COperation* pOp, QWheelEvent* pEvent)
{
    const int step = landscapeWheelStep(pEvent->angleDelta().y(),
                                        pEvent->delta(), pEvent->pixelDelta().y());
    if(step == 0)
        return;
    if(pEvent->modifiers() & Qt::ShiftModifier)
    {
        if(m_pView->manualTilePaintingEnabled())
        {
            m_pView->addTileRotation(step);
            const bool bLand = m_pView->terrainTileEditLayerIsLand();
            m_pView->updatePreviewTile(
                m_pView->getTerrainBrushPos(), bLand);
            CStatusConnector::getInstance()->updateStatus(
                "brush.ico", "Manual atlas tile rotation changed. Shift+Wheel rotates; Alt+Wheel resizes; Wheel controls the camera.");
        }
        else
        {
            m_pView->adjustTerrainPaintMaterial(step);
            CStatusConnector::getInstance()->updateStatus(
                "brush.ico", "Automatic terrain material changed. Shift+Wheel selects material; Alt+Wheel resizes; Wheel controls the camera.");
        }
        pEvent->accept();
        return;
    }
    if(pEvent->modifiers() & Qt::AltModifier)
    {
        m_pView->adjustTerrainPaintSize(step);
        CStatusConnector::getInstance()->updateStatus(
            "brush.ico", "Terrain brush size changed. Alt+Wheel resizes; Shift+Wheel changes material or manual rotation; Wheel controls the camera.");
        pEvent->accept();
        return;
    }
    pOp->camera()->enlarge(step > 0);
}
