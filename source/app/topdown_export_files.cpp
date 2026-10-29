#include "topdown_export.h"
#include <QBuffer>
#include <QImageReader>
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
QJsonObject ai::encodeImageBounded(QImage image,QSize maximum,const Cancellation& cancel)
{
    if(image.isNull())return {{"code","RENDER_UNAVAILABLE"},{"error","Empty source image"}};
    if(image.width()>maximum.width()||image.height()>maximum.height())image=image.scaled(maximum,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    for(;;) {
        if(cancel->load())return {{"code","CANCELLED"},{"error","Image encoding cancelled"}};
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
        if(!image.save(&buffer,"PNG"))return {{"code","SNAPSHOT_SIZE_LIMIT"},{"error","PNG encoder unavailable"}};
        // Includes base64/envelope overhead below the existing 512 KiB IPC cap.
        if(png.size()<=340*1024)return {{"mimeType","image/png"},{"data",QString::fromLatin1(png.toBase64())},
            {"width",image.width()},{"height",image.height()}};
        if(qMin(image.width(),image.height())<64)return {{"code","SNAPSHOT_SIZE_LIMIT"},{"error","PNG cannot fit 384 KiB / IPC envelope"}};
        image=image.scaled(qMax(1,image.width()*3/4),qMax(1,image.height()*3/4),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    }
}
namespace {
QString registryRoot() {
    return ai::discoveryDirectory()+"/exports/"+QString::fromLatin1(QCryptographicHash::hash(
        (QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath().toLower()+"|"+ai::topdownWorkspaceRoot().toLower()).toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool safeRoute(QString path) {
    QFileInfo info(path);if(!info.exists()||linked(path))return false;
    QDir dir=info.isDir()?QDir(path):info.dir();
    do{if(linked(dir.absolutePath()))return false;}while(dir.cdUp());
    return true;
}
QImage cartographicBase(const QMap<QString,QImage>& images) {
    const auto relief=images.value("relief.png");const auto water=images.value("water-mask.png");
    const auto structures=images.value("structures-mask.png");
    QImage result(relief.size(),QImage::Format_RGB32);
    const auto boundary=[](const QImage& mask,int x,int y) {
        for(const QPoint d:{QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)}) {
            const int nx=x+d.x(),ny=y+d.y();if(mask.rect().contains(nx,ny)&&qGray(mask.pixel(nx,ny))<128)return true;
        }return false;
    };
    for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x) {
        const int shade=qGray(relief.pixel(x,y));
        const int dx=qAbs(shade-qGray(relief.pixel(qMin(x+1,result.width()-1),y)));
        const int dy=qAbs(shade-qGray(relief.pixel(x,qMin(y+1,result.height()-1))));
        const int land=qBound(155,207+shade/6-qMin(48,(dx+dy)*2),248);
        QRgb color=qRgb(land,land,land);
        if(qGray(water.pixel(x,y))>=128)color=boundary(water,x,y)?qRgb(72,118,138):qRgb(166,202,216);
        if(qGray(structures.pixel(x,y))>=128)color=boundary(structures,x,y)?qRgb(53,57,61):qRgb(166,171,177);
        result.setPixel(x,y,color);
    }
    return result;
}
}
bool ai::registerTopdownExport(const QString& manifestPath,const QString& exportId,const QByteArray& sha256,QString* error)
{
    const auto root=registryRoot();
    if(!secureDirectory(root)||!safeRoute(root)){*error="Cannot protect export registry";return false;}
    QSaveFile file(root+"/"+exportId+".json");
    const QByteArray bytes=QJsonDocument(QJsonObject{{"exportId",exportId},{"manifestPath",manifestPath},
        {"manifestSha256",QString::fromLatin1(sha256)},{"workspace",topdownWorkspaceRoot()}}).toJson();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){*error="Cannot persist export registration";return false;}
    return true;
}
QJsonObject ai::readExportImage(const QJsonObject& args)
{
    const auto fail=[](QString reason){return QJsonObject{{"code","EXPORT_IMAGE_ERROR"},{"error",reason}};};
    const QString id=args["exportId"].toString(),layer=args["layer"].toString();
    if(!QRegularExpression("^[a-f0-9-]{36}$").match(id).hasMatch())return fail("Unknown registered exportId");
    const QString recordPath=registryRoot()+"/"+id+".json";
    if(!safeRoute(recordPath)||QFileInfo(recordPath).size()>16384)return fail("Export registration unavailable");
    QFile record(recordPath);if(!record.open(QIODevice::ReadOnly))return fail("Cannot read export registration");
    const auto registration=QJsonDocument::fromJson(record.readAll()).object();
    const QString manifestPath=registration["manifestPath"].toString();
    if(registration["exportId"]!=id||registration["workspace"]!=topdownWorkspaceRoot()||!within(manifestPath,topdownWorkspaceRoot())
       ||!safeRoute(manifestPath)||QFileInfo(manifestPath).size()>32*1024*1024)return fail("Registered package unavailable or unsafe");
    QString leaseError;const auto lease=lockTopdownParent(manifestPath,topdownWorkspaceRoot(),&leaseError);
    if(!lease)return fail(leaseError);
    QFile mf(manifestPath);if(!mf.open(QIODevice::ReadOnly))return fail("Cannot open registered manifest");
    const auto manifestBytes=mf.readAll();const auto manifest=QJsonDocument::fromJson(manifestBytes).object();
    if(QString::fromLatin1(QCryptographicHash::hash(manifestBytes,QCryptographicHash::Sha256).toHex())!=registration["manifestSha256"].toString()
       ||manifest["status"]!="complete"||manifest["exportId"]!=id)return fail("Registered manifest identity changed");
    QJsonObject entry;for(const auto& f:manifest["files"].toArray())if(f.toObject()["name"]==layer)entry=f.toObject();
    if(entry.isEmpty()||QFileInfo(layer).fileName()!=layer||!layer.endsWith(".png"))return fail("Layer is not a registered manifest image");
    const QString path=QFileInfo(manifestPath).dir().filePath(layer);
    if(!safeRoute(path)||QFileInfo(path).size()!=entry["bytes"].toDouble()||QFileInfo(path).size()>64*1024*1024)return fail("Registered layer changed or unavailable");
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail("Cannot open registered layer");const auto bytes=file.readAll();
    if(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=entry["sha256"].toString())return fail("Registered layer SHA-256 changed");
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");
    const QSize size(entry["width"].toInt(),entry["height"].toInt());
    if(reader.size()!=size||size.isEmpty()||qint64(size.width())*size.height()>8*1024*1024)return fail("Invalid registered image dimensions");
    QRect rect(QPoint(0,0),size);
    if(args.contains("pixelRect")) {
        const auto r=args["pixelRect"].toArray();if(r.size()!=4)return fail("pixelRect must be [x,y,width,height]");
        rect=QRect(r[0].toInt(),r[1].toInt(),r[2].toInt(),r[3].toInt());
        if(rect.isEmpty()||!QRect(QPoint(0,0),size).contains(rect))return fail("pixelRect lies outside registered PNG");
    }
    auto image=reader.read();if(image.isNull())return fail("Registered PNG cannot be decoded");
    image=image.copy(rect);auto result=encodeImageBounded(image,QSize(args["maxSide"].toInt(1024),args["maxSide"].toInt(1024)),std::make_shared<std::atomic_bool>(false));
    const auto bounds=manifest["worldBounds"].toArray();const double xmin=bounds[0].toDouble(),ymin=bounds[1].toDouble(),xmax=bounds[2].toDouble(),ymax=bounds[3].toDouble();
    result["exportId"]=id;result["layer"]=layer;result["pixelRect"]=QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()};
    result["worldBounds"]=QJsonArray{xmin+rect.x()*(xmax-xmin)/size.width(),ymax-(rect.y()+rect.height())*(ymax-ymin)/size.height(),
        xmin+(rect.x()+rect.width())*(xmax-xmin)/size.width(),ymax-rect.y()*(ymax-ymin)/size.height()};
    result["sourceSize"]=QJsonArray{size.width(),size.height()};result["retention"]=manifest["retention"];return result;
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
    images.insert("cartographic-base.png",cartographicBase(images));
    const QImage preview=images.value("cartographic-base.png").scaled(1024,1024,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    images.insert("composite-preview.png",images.value("composite.png").scaled(1024,1024,Qt::KeepAspectRatio,Qt::SmoothTransformation));
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
