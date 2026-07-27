#include <QApplication>
#include <QMessageBox>
#include <QKeyEvent>
#include <QDir>

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>

#include "view.h"
#include "key_manager.h"
#include "mob\mob.h"
#include "camera.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "operationmanager.h"
#include "scene.h"

void CView::keyPressEvent(QKeyEvent* event)
{
    if(m_landscapeTools && m_landscapeTools->editingEnabled()
       && event->modifiers() == Qt::NoModifier)
    {
        if(event->key() == Qt::Key_M)
        {
            setDrawLand(!drawLand());
            event->accept();
            return;
        }
        if(event->key() == Qt::Key_O)
        {
            setDrawWater(!drawWater());
            event->accept();
            return;
        }
        if(m_landscapeTools->sculptEnabled())
        {
            ELandscapeSculptTool tool;
            bool recognized = true;
            switch(event->key())
            {
            case Qt::Key_Z: tool = ELandscapeSculptTool::Raise; break;
            case Qt::Key_X: tool = ELandscapeSculptTool::Lower; break;
            case Qt::Key_C: tool = ELandscapeSculptTool::Flatten; break;
            case Qt::Key_V: tool = ELandscapeSculptTool::Smooth; break;
            default: recognized = false; break;
            }
            if(recognized)
            {
                m_landscapeTools->setSculptTool(tool);
                event->accept();
                return;
            }
        }
    }
    m_pOp->keyPress(EKeyCode(event->nativeVirtualKey()));
}

void CView::keyReleaseEvent(QKeyEvent* event)
{
    if(m_landscapeTools && m_landscapeTools->editingEnabled()
       && event->modifiers() == Qt::NoModifier
       && (event->key() == Qt::Key_M || event->key() == Qt::Key_O
           || (m_landscapeTools->sculptEnabled()
               && (event->key() == Qt::Key_Z || event->key() == Qt::Key_X
                   || event->key() == Qt::Key_C || event->key() == Qt::Key_V))))
    {
        event->accept();
        return;
    }
    m_pOp->keyRelease(EKeyCode(event->nativeVirtualKey()));
}
