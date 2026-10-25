#include <QApplication>
#include <QEvent>
#include <QMessageBox>
#include <QKeyEvent>
#include <QDir>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QComboBox>

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
EKeyCode cameraKey(const QKeyEvent* event)
{
    const int key=event->nativeVirtualKey()?int(event->nativeVirtualKey()):event->key();
    switch(key) {
    case 'W':return eKey_W;case 'A':return eKey_A;case 'S':return eKey_S;case 'D':return eKey_D;
    case 'Q':return eKey_Q;case 'E':return eKey_E;
    case 0x26:case Qt::Key_Up:return eKey_UpArrow;
    case 0x28:case Qt::Key_Down:return eKey_DownArrow;
    case 0x25:case Qt::Key_Left:return eKey_LeftArrow;
    case 0x27:case Qt::Key_Right:return eKey_RightArrow;
    case 0x10:case Qt::Key_Shift:return eKey_Shift;
    default:return EKeyCode(0);
    }
}
bool textInput(QWidget* widget)
{
    for(auto* w=widget;w;w=w->parentWidget()) {
        if(qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w) || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w))return true;
        if(auto* combo=qobject_cast<QComboBox*>(w))if(combo->isEditable())return true;
    }
    return false;
}
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
    if(m_pOp && (event->type()==QEvent::FocusIn || event->type()==QEvent::WindowDeactivate)) {
        auto* receiver=qobject_cast<QWidget*>(watched);
        if(receiver && (receiver==window() || window()->isAncestorOf(receiver))
           && (event->type()==QEvent::WindowDeactivate || textInput(receiver)))
            for(auto code:{eKey_W,eKey_A,eKey_S,eKey_D,eKey_Q,eKey_E,eKey_UpArrow,eKey_DownArrow,eKey_LeftArrow,eKey_RightArrow,eKey_Shift})m_pOp->keyManager()->release(code);
    }
    // Shared across editing modes. Text editors and open popup menus retain
    // their keyboard; buttons/list backgrounds can still navigate the scene.
    if(m_pOp && (event->type()==QEvent::KeyPress || event->type()==QEvent::KeyRelease)) {
        auto* key=static_cast<QKeyEvent*>(event);const auto code=cameraKey(key);
        auto* receiver=qobject_cast<QWidget*>(watched);
        QWidget* focus=QApplication::focusWidget();
        if(!focus)focus=window()->focusWidget();
        const bool context=receiver && (receiver==this || window()->isAncestorOf(receiver));
        if(code!=EKeyCode(0) && context && !QApplication::activeModalWidget() && !QApplication::activePopupWidget()
           && !textInput(receiver) && !textInput(focus) && !(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))) {
            if(!key->isAutoRepeat()) {
                if(event->type()==QEvent::KeyPress)m_pOp->keyManager()->press(code);
                else m_pOp->keyManager()->release(code);
            }
            event->accept();return true;
        }
        // Focus/modifiers can change between press and release.
        if(code!=EKeyCode(0) && event->type()==QEvent::KeyRelease && !key->isAutoRepeat())m_pOp->keyManager()->release(code);
    }
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
        else if(cameraKey(event)!=EKeyCode(0) && !(event->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)))m_pOp->keyManager()->press(cameraKey(event));
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
    if(m_travelEditing){if(cameraKey(event)!=EKeyCode(0))m_pOp->keyManager()->release(cameraKey(event));event->accept();return;}
    if(handleLandscapeShortcut(event, false))
    {
        event->accept();
        return;
    }
    m_pOp->keyRelease(EKeyCode(event->nativeVirtualKey()));
}
