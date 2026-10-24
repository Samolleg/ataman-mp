#ifndef AGENT_HEADLESS_H
#define AGENT_HEADLESS_H
#include <QtWidgets>
#include "log.h"

// Installed before ANY editor widget is constructed, including first-run setup.
// Native surfaces still exist for the legacy GL renderer, but are never mapped.
class CAgentHeadlessWindows final:public QObject {
public:
    bool eventFilter(QObject* o,QEvent* e)override {
        if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o)) {
            if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
            if(e->type()==QEvent::Show)if(auto* dialog=qobject_cast<QDialog*>(w)) {
                if(qobject_cast<QProgressDialog*>(dialog))return false;
                auto* box=qobject_cast<QMessageBox*>(dialog);
                const auto message=box?box->text():"Interactive dialog required: "+dialog->windowTitle();
                qApp->setProperty("agentHeadlessDialogError",message);
                ei::log(eLogWarning,"AI headless dialog refused: "+message);
                QPointer<QDialog> safe=dialog;
                QTimer::singleShot(0,dialog,[safe](){if(safe)safe->reject();});
            }
        }
        return false;
    }
};
#endif
