#include "topdown_export.h"
#include <QBuffer>
#include <vector>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace {
bool within(const QString& path,const QString& root) {
    return path.compare(root,Qt::CaseInsensitive)==0 || path.startsWith(root+"/",Qt::CaseInsensitive);
}
bool linked(const QString& path) {
    if(QFileInfo(path).isSymLink())return true;
#ifdef Q_OS_WIN
    const auto attrs=GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
    return attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return false;
#endif
}
}
QString ai::topdownWorkspaceRoot()
{
    // The installed local mapper sits inside its workspace. A standalone clean
    // package uses its working directory. No persisted permission/config toggle.
    QDir dir(QCoreApplication::applicationDirPath());
    do {if(dir.exists("AGENTS.md")||dir.exists(".git"))return dir.canonicalPath();}while(dir.cdUp());
    return QDir::current().canonicalPath();
}
QJsonObject ai::validateTopdownOutput(const QString& output,const QString& workspace,const QStringList& forbidden)
{
    const auto fail=[](QString code,QString reason){return QJsonObject{{"code",code},{"error",reason}};};
    const QString path=QDir::cleanPath(QDir::fromNativeSeparators(output));
    const QString root=QFileInfo(workspace).canonicalFilePath();
    if(output.isEmpty()||!QDir::isAbsolutePath(output)||root.isEmpty()||!within(path,root)||path==root
       ||path.contains(':',Qt::CaseSensitive)&&path.mid(2).contains(':')||path.startsWith("//"))
        return fail("OUTPUT_PATH_ERROR","Export destination must be a new absolute directory inside workspace: "+root);
    for(const auto& source:forbidden) {
        const QString absolute=QDir::cleanPath(QFileInfo(source).absoluteFilePath());
        const auto canonical=QFileInfo(source).canonicalFilePath();
        if(within(path,absolute)||(!canonical.isEmpty()&&within(path,canonical)))
            return fail("OUTPUT_PATH_ERROR","Destination overlaps profile/game sources: "+absolute);
    }
    // Require an existing parent. This avoids creating arbitrary ancestor trees
    // and lets us verify the complete route before the sole owned mkdir.
    QFileInfo target(path);QDir parent=target.dir();
    if(!parent.exists())return fail("OUTPUT_PATH_ERROR","Export parent directory must already exist");
    QDir probe(parent);
    do {if(linked(probe.absolutePath()))return fail("OUTPUT_PATH_ERROR","Symlink/junction/reparse ancestor refused: "+probe.absolutePath());}while(probe.cdUp());
    if(linked(path))return fail("OUTPUT_PATH_ERROR","Linked export destination refused");
    if(target.exists())return fail("OUTPUT_EXISTS","Destination already exists; choose a new export directory");
    const QString canonical=QDir(parent.canonicalPath()).filePath(target.fileName());
    if(!within(canonical,root)||canonical!=path)return fail("OUTPUT_PATH_ERROR","Non-canonical output route refused");
    if(target.fileName().contains(QRegularExpression("[<>\"|?*]"))||target.fileName().endsWith('.')||target.fileName().endsWith(' '))
        return fail("OUTPUT_PATH_ERROR","Invalid export directory name");
    return {{"path",canonical},{"workspace",root}};
}
std::shared_ptr<void> ai::lockTopdownParent(const QString& output,const QString& workspace,QString* error)
{
#ifdef Q_OS_WIN
    // Hold every workspace ancestor without FILE_SHARE_DELETE so another
    // process cannot replace a validated route with a junction during encoding.
    struct Handles {std::vector<HANDLE> values;~Handles(){for(auto h:values)CloseHandle(h);}};
    auto handles=std::make_shared<Handles>();QDir dir=QFileInfo(output).dir();
    do {
        const QString path=QDir::toNativeSeparators(dir.absolutePath());
        // FILE_READ_ATTRIBUTES alone does not participate in directory rename
        // sharing checks on Windows. LIST_DIRECTORY is needed for the lease.
        HANDLE h=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE){*error="Cannot retain validated export parent: "+dir.absolutePath();return {};}
        handles->values.push_back(h);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
            *error="Export route changed to a reparse point: "+dir.absolutePath();return {};
        }
        if(dir.absolutePath().compare(workspace,Qt::CaseInsensitive)==0)return handles;
    }while(dir.cdUp());
    *error="Export route left workspace while acquiring directory lease";return {};
#else
    Q_UNUSED(output);Q_UNUSED(workspace);Q_UNUSED(error);
    return std::make_shared<int>(1);
#endif
}
QJsonObject ai::writeTopdownImages(QMap<QString,QImage> images,const QString& temporary,
                                  QJsonObject manifest,const Cancellation& cancel)
{
    QJsonArray files;QJsonObject paths;
    const auto fail=[&](QString reason){return QJsonObject{{"error",reason},{"incompletePath",temporary}};};
    QString leaseError;const auto lease=lockTopdownParent(temporary+"/manifest.json",topdownWorkspaceRoot(),&leaseError);
    if(!lease)return fail(leaseError);
    const QImage preview=images.value("composite.png").scaled(768,768,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    images.insert("preview.png",preview);
    for(auto it=images.constBegin();it!=images.constEnd();++it) {
        if(cancel->load())return fail("Encoding cancelled");
        const auto path=QDir(temporary).filePath(it.key());
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||!it.value().save(&file,"PNG")||!file.commit())return fail("PNG write failed: "+path);
        QFile input(path);if(!input.open(QIODevice::ReadOnly))return fail("Cannot verify PNG: "+path);
        QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&input))return fail("Cannot hash PNG: "+path);
        files.append(QJsonObject{{"name",it.key()},{"bytes",double(input.size())},{"sha256",QString::fromLatin1(hash.result().toHex())},
            {"width",it.value().width()},{"height",it.value().height()}});
        paths[it.key()]=it.key();
    }
    if(cancel->load())return fail("Encoding cancelled");
    manifest["files"]=files;manifest["status"]="prepared";
    // This manifest remains private in the .incomplete directory. Only the GUI
    // owner can publish the directory after its final revision/deadline guard.
    QSaveFile file(QDir(temporary).filePath("manifest.json"));
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(manifest).toJson())<0||!file.commit())return fail("Manifest write failed");
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);preview.save(&buffer,"PNG");
    // Inline transport remains well below the 512 KiB result cap.
    QImage inlinePreview=preview;
    while(png.size()>180*1024 && inlinePreview.width()>128) {
        inlinePreview=inlinePreview.scaled(inlinePreview.size()/2,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        png.clear();buffer.seek(0);inlinePreview.save(&buffer,"PNG");
    }
    return {{"files",paths},{"preview",QJsonObject{{"mimeType","image/png"},{"data",QString::fromLatin1(png.toBase64())},
        {"width",inlinePreview.width()},{"height",inlinePreview.height()}}}};
}
