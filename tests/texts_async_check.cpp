#include <QtWidgets>
#include <QThread>
#include "map_table_editor_dialog.h"
#include "res_archive_document.h"
#include "res_archive_index.h"
#include "file_write_activity.h"

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object)) if(widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray seed() {
    const QByteArray map="#zone old island game\r\n#res\r\nold old\r\n";
    QByteArray bytes(16,'\0');bytes.append(map);const int offset=bytes.size();
    QByteArray table;QDataStream stream(&table,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);
    stream<<quint32(-1)<<quint32(map.size())<<quint32(16)<<quint32(123)<<quint16(7)<<quint32(0);
    bytes.append(table);bytes.append("map.txt");
    QDataStream header(&bytes,QIODevice::WriteOnly);header.setByteOrder(QDataStream::LittleEndian);
    header<<quint32(0x019CE23C)<<quint32(1)<<quint32(offset)<<quint32(7);return bytes;
}
}
int runAssemblyCatalogCheck() {
    QTextStream out(stdout);int passes=0;
    const auto check=[&](bool ok,const QString& label){out<<(ok?"PASS ":"FAIL ")<<label<<'\n';out.flush();if(!ok)throw label;++passes;};
    Hidden hidden;qApp->installEventFilter(&hidden);
    try {
        const auto root=qEnvironmentVariable("EI_AUDIT_FIX_ROOT");check(!root.isEmpty(),"isolated root");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        const QString source=root+"/texts.res",destination=root+"/saved.res";
        {CResArchiveDocument document;check(document.loadBytes(seed()),"synthetic map catalog");
         QByteArray bytes;check(document.buildWithAppendedMember("opaque.bin",QByteArray(32*1024*1024,'x'),456,&bytes),"32 MiB untouched member");
         QFile file(source);check(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"isolated RES written");}
        const auto before=resFileSha256(source);
        CMapTableEditorDialog editor;editor.setAttribute(Qt::WA_DontShowOnScreen);
        QElapsedTimer elapsed;elapsed.start();check(editor.openArchive(source),"file-backed editor opens");
        const auto openMs=elapsed.elapsed();
        auto* id=editor.findChild<QLineEdit*>("mapZoneId");check(id!=nullptr,"zone editor control");id->setText("renamed");
        int ticks=0; qint64 lastTick=0,maxGap=0;QTimer heartbeat;heartbeat.setInterval(1);
        QObject::connect(&heartbeat,&QTimer::timeout,[&](){++ticks;auto now=elapsed.elapsed();maxGap=qMax(maxGap,now-lastTick);lastTick=now;});
        bool completed=false,success=false;
        QObject::connect(&editor,&CMapTableEditorDialog::archiveSaveFinished,[&](bool ok){completed=true;success=ok;});
        elapsed.restart();heartbeat.start();
        check(editor.saveArchiveTo(destination),"asynchronous Save As accepted");const auto dispatchMs=elapsed.elapsed();
        check(editor.isSaving()&&CFileWriteActivity::active()>0,"pending worker holds close/write guard immediately");
        check(!id->isEnabled()&&!editor.openArchive(source)&&!editor.saveArchiveTo(destination),"pending save blocks editing/open/reentrant save");
        editor.reject();check(editor.isSaving(),"Escape/reject does not destroy a pending transaction");
        while(!completed&&elapsed.elapsed()<60000) {qApp->processEvents();QThread::msleep(1);}
        heartbeat.stop();
        check(completed&&success,"worker completes and GUI reopens destination");
        check(ticks>2,"GUI heartbeat continues during RES validation and write");
        check(!editor.isSaving()&&id->isEnabled()&&CFileWriteActivity::active()==0,"controls and close guard restored");
        check(resFileSha256(source)==before,"Save As preserves source bytes");
        CResArchiveIndex reopened;QString error;check(reopened.open(destination,&error),"saved RES fresh index");
        check(reopened.readMember("map.txt").contains("#zone renamed island game"),"intended map.txt edit persisted");
        check(reopened.memberSha256("opaque.bin")==QCryptographicHash::hash(QByteArray(32*1024*1024,'x'),QCryptographicHash::Sha256).toHex(),"large unrelated member unchanged");
        check(editor.saveReports().size()==1&&editor.saveReports().front().newArchiveSha256==resFileSha256(destination),"GUI publishes exact committed report");
        out<<"BENCH openMs="<<openMs<<" dispatchMs="<<dispatchMs<<" totalMs="<<elapsed.elapsed()<<" heartbeatTicks="<<ticks<<" maxGapMs="<<maxGap<<'\n';
        const auto realSource=qEnvironmentVariable("EI_AUDIT_TEXTS_RES");
        if(!realSource.isEmpty()) {
            const auto realBefore=resFileSha256(realSource);
            CMapTableEditorDialog real;real.setAttribute(Qt::WA_DontShowOnScreen);
            check(real.openArchive(realSource),"private real texts.res opens through indexed reader");
            bool realDone=false,realSuccess=false;
            QObject::connect(&real,&CMapTableEditorDialog::archiveSaveFinished,[&](bool ok){realDone=true;realSuccess=ok;});
            const auto realCopy=root+"/real-texts-copy.res";
            check(real.saveArchiveTo(realCopy),"real catalog accepts unchanged Save As");elapsed.restart();
            while(!realDone&&elapsed.elapsed()<60000) {qApp->processEvents();QThread::msleep(1);}
            check(realDone&&realSuccess,"real catalog save/reopen completes");
            check(resFileSha256(realSource)==realBefore&&resFileSha256(realCopy)==realBefore,
                  "real texts.res no-op Save As preserves every archive byte");
        }
        // A stale dialog must reject a foreign map.txt edit and remain usable.
        {CResArchiveIndex disk;check(disk.open(destination),"conflict fixture index");
         SResMemberSaveRequest request;request.source=request.destination=QFileInfo(destination);request.memberName="map.txt";
         request.expectedMemberSha256=disk.memberSha256("map.txt");request.replacementPayload="#zone foreign island game\r\n#res\r\nold old\r\n";
         request.backupRootOverride=root+"/backups";
         auto changed=commitStreamedResMemberReplacement(request);check(changed.success,"external member replacement fixture");}
        const auto foreign=resFileSha256(destination);completed=false;success=true;
        check(editor.saveArchiveTo(destination),"stale editor dispatches guarded save");elapsed.restart();
        while(!completed&&elapsed.elapsed()<60000) {qApp->processEvents();QThread::msleep(1);}
        check(completed&&!success&&resFileSha256(destination)==foreign,"worker rejects stale map.txt without overwriting foreign bytes");
        check(!editor.isSaving()&&id->isEnabled()&&editor.saveReports().size()==1,"failed save restores UI without a false report");
        out<<passes<<" PASS / 0 FAIL\n";return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<'\n';return 1;}
}
