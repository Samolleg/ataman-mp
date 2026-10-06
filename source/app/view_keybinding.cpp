#include <QApplication>
#include <QEvent>
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

namespace
{
enum class ELandscapePhysicalShortcut
{
    None,
    ToggleLand,
    ToggleOverlay,
    Raise,
    Lower,
    Smooth,
    Flatten
};

ELandscapePhysicalShortcut landscapePhysicalShortcut(const QKeyEvent* event)
{
#ifdef Q_OS_WIN
    // Set-1 scan codes identify the physical letter positions and therefore
    // do not change when Windows switches between Latin and Cyrillic layouts.
    switch(event->nativeScanCode() & 0xffu)
    {
    case 0x32: return ELandscapePhysicalShortcut::ToggleLand;    // M
    case 0x18: return ELandscapePhysicalShortcut::ToggleOverlay; // O
    case 0x2c: return ELandscapePhysicalShortcut::Raise;          // Z
    case 0x2d: return ELandscapePhysicalShortcut::Lower;          // X
    case 0x2e: return ELandscapePhysicalShortcut::Smooth;         // C
    case 0x2f: return ELandscapePhysicalShortcut::Flatten;        // V
    default: break;
    }
#endif
    switch(event->key())
    {
    case Qt::Key_M: return ELandscapePhysicalShortcut::ToggleLand;
    case Qt::Key_O: return ELandscapePhysicalShortcut::ToggleOverlay;
    case Qt::Key_Z: return ELandscapePhysicalShortcut::Raise;
    case Qt::Key_X: return ELandscapePhysicalShortcut::Lower;
    case Qt::Key_C: return ELandscapePhysicalShortcut::Smooth;
    case Qt::Key_V: return ELandscapePhysicalShortcut::Flatten;
    default: return ELandscapePhysicalShortcut::None;
    }
}
}

bool CView::handleLandscapeShortcut(QKeyEvent* event, bool pressed)
{
    if(!m_landscapeTools || !m_landscapeTools->editingEnabled()
       || event->modifiers() != Qt::NoModifier)
        return false;

    const ELandscapePhysicalShortcut shortcut = landscapePhysicalShortcut(event);
    if(shortcut == ELandscapePhysicalShortcut::None)
        return false;
    if(!pressed || event->isAutoRepeat())
        return true;

    switch(shortcut)
    {
    case ELandscapePhysicalShortcut::ToggleLand:
        setDrawLand(!drawLand());
        return true;
    case ELandscapePhysicalShortcut::ToggleOverlay:
        setDrawWater(!drawWater());
        return true;
    case ELandscapePhysicalShortcut::Raise:
    case ELandscapePhysicalShortcut::Lower:
    case ELandscapePhysicalShortcut::Smooth:
    case ELandscapePhysicalShortcut::Flatten:
        if(!m_landscapeTools->sculptEnabled())
            return false;
        if(shortcut == ELandscapePhysicalShortcut::Raise)
            m_landscapeTools->setSculptTool(ELandscapeSculptTool::Raise);
        else if(shortcut == ELandscapePhysicalShortcut::Lower)
            m_landscapeTools->setSculptTool(ELandscapeSculptTool::Lower);
        else if(shortcut == ELandscapePhysicalShortcut::Smooth)
            m_landscapeTools->setSculptTool(ELandscapeSculptTool::Smooth);
        else
            m_landscapeTools->setSculptTool(ELandscapeSculptTool::Flatten);
        return true;
    case ELandscapePhysicalShortcut::None:
        return false;
    }
    return false;
}

bool CView::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
       && !QApplication::activeModalWidget() && m_landscapeTools)
    {
        QWidget* focus = QApplication::focusWidget();
        const bool landscapeContext = focus == this || (focus && isAncestorOf(focus))
            || focus == m_landscapeTools
            || (focus && m_landscapeTools->isAncestorOf(focus));
        if(landscapeContext && handleLandscapeShortcut(
               static_cast<QKeyEvent*>(event), event->type() == QEvent::KeyPress))
        {
            event->accept();
            return true;
        }
    }
    return QGLWidget::eventFilter(watched, event);
}

void CView::keyPressEvent(QKeyEvent* event)
{
    if(event->key()==Qt::Key_Escape && m_neighborLayoutDragActive) {
        cancelNeighborLayoutDrag(true);event->accept();return;
    }
    if(event->key() == Qt::Key_Escape && isLogicPlacementPending()) {
        cancelPendingOperation();
        event->accept();
        return;
    }
    if(m_travelEditing) {
        if(event->key()==Qt::Key_Escape)cancelTravelGesture();
        event->accept();return;
    }

    if(m_worldPlacementActive)
    {
        if(event->key() == Qt::Key_Escape) cancelWorldMapPlacement();
        event->accept(); return;
    }
    if(handleLandscapeShortcut(event, true))
    {
        event->accept();
        return;
    }
    m_pOp->keyPress(EKeyCode(event->nativeVirtualKey()));
}

void CView::keyReleaseEvent(QKeyEvent* event)
{
    if(m_travelEditing){event->accept();return;}
    if(handleLandscapeShortcut(event, false))
    {
        event->accept();
        return;
    }
    m_pOp->keyRelease(EKeyCode(event->nativeVirtualKey()));
}
