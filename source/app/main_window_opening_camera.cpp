#include "view.h"
#include "main_window.h"
#include "resource_profile.h"
#include <QDir>
#include <QFileInfo>
QString MainWindow::openingCameraPath(const QFileInfo& mpr) const {
    QSet<QString> names;
    if(mpr.completeBaseName().compare("zonemainmenunew",Qt::CaseInsensitive)==0)names.insert("mainmenu.cam");
    else for(const auto& entry:m_mapCatalog.entries)
        if(QFileInfo(entry.mprPath).canonicalFilePath().compare(mpr.canonicalFilePath(),Qt::CaseInsensitive)==0)
            names.insert(entry.cameraName.toCaseFolded());
    if(names.size()!=1)return {};
    auto name=*names.cbegin();if(name.isEmpty())return {};if(!name.endsWith(".cam",Qt::CaseInsensitive))name+=".cam";
    if(QFileInfo(name).fileName()!=name||name.contains('\\')||name.contains('/'))return {};
    const QDir root(mpr.absoluteDir().absoluteFilePath(".."));
    QString own=root.filePath("Camera");
    for(const auto& dir:root.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot))
        if(dir.fileName().compare("Camera",Qt::CaseInsensitive)==0)own=dir.absoluteFilePath();
    const auto find=[&](const QString& dir){for(const auto& f:QDir(dir).entryInfoList(QDir::Files|QDir::Readable))if(f.fileName().compare(name,Qt::CaseInsensitive)==0)return f.absoluteFilePath();return QString();};
    auto path=find(own);return path.isEmpty()?find(QDir(resourceGameDataRoot(activeResourceProfile().gameRoot)).filePath("Camera")):path;
}

void MainWindow::configureOpeningEnvironment(const QFileInfo& mpr) {
    for(const auto& entry:m_mapCatalog.entries)
        if(QFileInfo(entry.mprPath).canonicalFilePath().compare(mpr.canonicalFilePath(),Qt::CaseInsensitive)==0) {
            m_pView->setMapSkyType(entry.skyType,entry.allodId);return;
        }
    m_pView->setMapSkyType(QString(),QString());
}
