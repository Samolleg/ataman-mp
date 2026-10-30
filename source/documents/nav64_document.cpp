#include "nav64_document.h"
#include "ai_graph_preview_builder.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLibrary>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QColor>
#include <QCryptographicHash>
#include <QSettings>
#include <set>
#include <tuple>
#include <QtEndian>
#include <windows.h>
#include <cstring>
#include <cmath>
#include "DetourNavMesh.h"
#include "DetourAlloc.h"
#include "DetourCommon.h"

namespace {
const quint32 Limit=512u*1024*1024;
// Accepted wire pairs, independent of whichever producer is currently vendored.
const struct NavFormat {quint32 container,algorithm;} Formats[]={{3,8},{3,9},{3,10},{8,21},{9,24},{9,25},{10,26}};
bool supportedFormat(quint32 container,quint32 algorithm){for(const auto& f:Formats)if(f.container==container&&f.algorithm==algorithm)return true;return false;}
struct Reader {
    QByteArray data; int at=0;
    QByteArray bytes(quint32 n) { if(n>quint32(data.size()-at))throw QStringLiteral("Обрезанный контейнер Nav64"); auto b=data.mid(at,int(n));at+=int(n);return b; }
    quint32 u32(){auto b=bytes(4);return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()));}
    float f32(){quint32 n=u32();float f;std::memcpy(&f,&n,4);if(!std::isfinite(f))throw QStringLiteral("Нечисловое поле Nav64");return f;}
    QString string(){quint32 n=u32();if(n>4096)throw QStringLiteral("Слишком длинная строка Nav64");auto b=bytes(n);auto s=QString::fromUtf8(b);if(s.toUtf8()!=b||s.contains(QChar(0)))throw QStringLiteral("Неверная UTF-8 строка Nav64");return s;}
    void end(){if(at!=data.size())throw QStringLiteral("Лишние байты секции Nav64");}
};
SNav64Header header(Reader& r) {
    if(r.u32()!=0x3656414e||r.u32()!=0x3436)throw QStringLiteral("Неверная сигнатура Nav64");
    SNav64Header h;h.container=r.u32();
    if(!nav64SupportedContainers().contains(h.container))throw QStringLiteral("Контейнер Nav64 %1 не поддерживается: перепеките/обновите маппер").arg(h.container);
    h.algorithm=r.u32();
    if(h.algorithm==7)throw QStringLiteral("Устаревшая сетка: алгоритм 7 игра не примет. Перепеките navmesh запекателем r13 или новее.");
    if(!nav64SupportedAlgorithms().contains(h.algorithm))throw QStringLiteral("Алгоритм Nav64 %1 не поддерживается: перепеките/обновите маппер").arg(h.algorithm);
    if(r.u32()!=0x010600||r.u32()!=0x12345678||r.u32()!=8)throw QStringLiteral("Несовместимый ABI Nav64: нужен Detour 1.6.0 / DT_POLYREF64 / little endian");
    if(!supportedFormat(h.container,h.algorithm))throw QStringLiteral("Несовместимая пара контейнер/алгоритм Nav64");
    if(h.container==3)h.fingerprint=r.string();
    h.mpr=r.string();h.mob=r.string();h.profile=r.string();
    if((h.container==3&&!QRegularExpression("^[0-9a-f]{64}$").match(h.fingerprint).hasMatch())||
       !QRegularExpression("^[a-z0-9_-]+\\.mpr$").match(h.mpr).hasMatch()||
       !QRegularExpression("^[a-z0-9_-]+\\.mob$").match(h.mob).hasMatch()||!(nav64Profiles()+QStringList{"tiny","levitate_tiny","levitate_small"}).contains(h.profile))throw QStringLiteral("Неверная идентичность Nav64");
    auto& p=h.body;p.radius=r.f32();p.height=r.f32();p.slope=r.f32();p.climb=r.f32();p.cellSize=r.f32();p.cellHeight=r.f32();p.tileCells=r.u32();
    if(h.container!=3){p.materialClass=r.u32();if(p.materialClass>7||p.tileCells!=96)throw QStringLiteral("Неверный класс движения / размер тайла Nav64");}
    if(h.container>=10){h.movement=r.u32();if(h.movement>2||h.movement!=(h.profile=="flying"?2u:h.profile=="levitate"?1u:0u))throw QStringLiteral("Неверный режим движения Nav64");}
    if(p.radius<0||p.radius>100||p.height<=0||p.height>100||p.slope<0||p.slope>90||p.climb<0||p.cellSize<=0||p.cellHeight<=0||!p.tileCells||p.tileCells>4096)throw QStringLiteral("Неверный профиль Nav64");
    return h;
}
QByteArray expand(const QByteArray& input,quint32 size) {
    if(size<4||size>Limit)throw QStringLiteral("Недопустимый размер LZMS");
    wchar_t system[MAX_PATH];if(!GetSystemDirectoryW(system,MAX_PATH))throw QStringLiteral("Не найден системный cabinet.dll");
    QLibrary lib(QString::fromWCharArray(system)+"/cabinet.dll");
    using Create=BOOL(WINAPI*)(DWORD,void*,HANDLE*);
    using Unpack=BOOL(WINAPI*)(HANDLE,const void*,SIZE_T,void*,SIZE_T,SIZE_T*);
    using Close=BOOL(WINAPI*)(HANDLE);
    auto create=reinterpret_cast<Create>(lib.resolve("CreateDecompressor"));auto unpack=reinterpret_cast<Unpack>(lib.resolve("Decompress"));auto close=reinterpret_cast<Close>(lib.resolve("CloseDecompressor"));
    if(!create||!unpack||!close)throw QStringLiteral("LZMS недоступен: нужен Windows Compression API (Windows 8+)");
    HANDLE handle=nullptr;if(!create(5,nullptr,&handle))throw QStringLiteral("CreateDecompressor(LZMS) не выполнен");
    QByteArray result(int(size),Qt::Uninitialized);SIZE_T written=0;
    const bool ok=unpack(handle,input.constData(),SIZE_T(input.size()),result.data(),size,&written);close(handle);
    if(!ok||written!=size)throw QStringLiteral("Повреждённый LZMS payload Nav64");return result;
}
QVector3D world(const float* p){auto v=Navigation::FromNav({p[0],p[1],p[2]});return {v.x,v.y,v.z+.12f};}
// Mapper-owned inverse of README Container 3 byte lanes. No producer/build code
// is copied: bound every array using the unchanged header before touching lanes.
QByteArray unshuffleTile(const QByteArray& packed){
    static_assert(sizeof(dtMeshHeader)==100&&sizeof(dtPoly)==32&&sizeof(dtLink)==16&&sizeof(dtPolyDetail)==12&&sizeof(dtBVNode)==16,"Nav64 container3 ABI");
    if(packed.size()<int(sizeof(dtMeshHeader))||packed.size()>16*1024*1024)throw QStringLiteral("Размер filtered-тайла вне лимита");
    dtMeshHeader h;std::memcpy(&h,packed.constData(),sizeof(h));
    if(h.magic!=DT_NAVMESH_MAGIC||h.version!=DT_NAVMESH_VERSION||h.polyCount<1||h.polyCount>32768||h.vertCount<3||h.vertCount>65535||h.maxLinkCount<1||h.detailMeshCount!=h.polyCount||h.detailVertCount<0||h.detailTriCount<0||h.bvNodeCount<0||h.bvNodeCount>h.polyCount*2||h.offMeshConCount!=0||h.offMeshBase!=h.polyCount)throw QStringLiteral("Неверный заголовок filtered-тайла");
    const quint64 counts[]={quint64(h.vertCount),quint64(h.polyCount),quint64(h.maxLinkCount),quint64(h.detailMeshCount),quint64(h.detailVertCount),quint64(h.detailTriCount),quint64(h.bvNodeCount)};
    const quint64 widths[]={12,32,16,12,12,4,16};quint64 total=dtAlign4(sizeof(h));
    for(int a=0;a<7;++a){total+=(counts[a]*widths[a]+3)&~quint64(3);if(total>quint64(packed.size()))throw QStringLiteral("Обрезанные byte-lanes тайла");}
    if(total!=quint64(packed.size()))throw QStringLiteral("Неверная длина byte-lanes тайла");
    QByteArray plain(packed.size(),char(0));quint64 offset=dtAlign4(sizeof(h));std::memcpy(plain.data(),packed.constData(),size_t(offset));
    for(int a=0;a<7;++a){const auto n=counts[a],w=widths[a];for(quint64 lane=0;lane<w;++lane)for(quint64 i=0;i<n;++i)plain[int(offset+i*w+lane)]=packed[int(offset+lane*n+i)];offset+=(n*w+3)&~quint64(3);}
    return plain;
}
// Validate the pinned native layout BEFORE Detour can dereference any input.
int validateTile(const QByteArray& b,const dtNavMeshParams& params,float heightTolerance) {
    static_assert(sizeof(dtPolyRef)==8&&sizeof(dtLink)==16,"Nav64 ABI");
    if(b.size()<int(sizeof(dtMeshHeader))||b.size()>16*1024*1024)throw QStringLiteral("Неверный размер тайла");
    const auto* h=reinterpret_cast<const dtMeshHeader*>(b.constData());
    if(h->magic!=DT_NAVMESH_MAGIC||h->version!=DT_NAVMESH_VERSION||h->polyCount<1||h->polyCount>params.maxPolys||h->vertCount<3||h->vertCount>65535||h->detailMeshCount!=h->offMeshBase||h->offMeshBase<0||h->offMeshBase>h->polyCount||h->offMeshConCount!=h->polyCount-h->offMeshBase||h->maxLinkCount<0||h->detailVertCount<0||h->detailTriCount<0||h->bvNodeCount<0||h->bvNodeCount>h->polyCount*2)throw QStringLiteral("Неверные счётчики/ABI тайла");
    quint64 offset=dtAlign4(sizeof(dtMeshHeader));
    auto block=[&](quint64 n,quint64 stride)->const char*{const auto start=offset;offset+=(n*stride+3)&~quint64(3);if(offset>quint64(b.size()))throw QStringLiteral("Обрезанные массивы тайла");return b.constData()+start;};
    const auto* v=reinterpret_cast<const float*>(block(h->vertCount,12));
    const auto* p=reinterpret_cast<const dtPoly*>(block(h->polyCount,sizeof(dtPoly)));
    block(h->maxLinkCount,sizeof(dtLink));
    const auto* d=reinterpret_cast<const dtPolyDetail*>(block(h->detailMeshCount,sizeof(dtPolyDetail)));
    const auto* dv=reinterpret_cast<const float*>(block(h->detailVertCount,12));const auto* tris=reinterpret_cast<const uchar*>(block(h->detailTriCount,4));
    const auto* bv=reinterpret_cast<const dtBVNode*>(block(h->bvNodeCount,sizeof(dtBVNode)));
    const auto* off=reinterpret_cast<const dtOffMeshConnection*>(block(h->offMeshConCount,sizeof(dtOffMeshConnection)));
    if(offset!=quint64(b.size()))throw QStringLiteral("Размер нативного тайла не совпадает с ABI");
    for(int i=0;i<h->vertCount*3;++i)if(!std::isfinite(v[i]))throw QStringLiteral("Нечисловая вершина");
    for(int i=0;i<h->detailVertCount*3;++i)if(!std::isfinite(dv[i]))throw QStringLiteral("Нечисловая detail-вершина");
    for(int a=0;a<3;++a)if(!std::isfinite(h->bmin[a])||!std::isfinite(h->bmax[a])||h->bmin[a]>h->bmax[a])throw QStringLiteral("Неверные границы тайла");
    int outlierVertices=0;
    for(int i=0;i<h->detailVertCount;++i)if(dv[i*3+1]<h->bmin[1]-heightTolerance||dv[i*3+1]>h->bmax[1]+heightTolerance)++outlierVertices;
    if(!std::isfinite(h->bvQuantFactor)||h->bvQuantFactor<=0||!std::isfinite(h->walkableClimb)||h->walkableClimb<0)throw QStringLiteral("Неверные параметры тайла");
    for(int i=0;i<h->polyCount;++i){
        if(p[i].firstLink!=DT_NULL_LINK)throw QStringLiteral("Контейнер3 содержит служебные firstLink");
        if(p[i].getType()!=(i<h->offMeshBase?DT_POLYTYPE_GROUND:DT_POLYTYPE_OFFMESH_CONNECTION)||p[i].vertCount<(i<h->offMeshBase?3:2)||p[i].vertCount>DT_VERTS_PER_POLYGON)throw QStringLiteral("Неверный полигон");
        for(int j=0;j<p[i].vertCount;++j){if(p[i].verts[j]>=h->vertCount)throw QStringLiteral("Индекс вершины вне тайла");const auto n=p[i].neis[j];if(n&DT_EXT_LINK){if((n&~DT_EXT_LINK)>7)throw QStringLiteral("Неверная сторона тайла");}else if(n>h->polyCount)throw QStringLiteral("Индекс соседа вне тайла");}
        if(i<h->offMeshBase){if(quint64(d[i].vertBase)+d[i].vertCount>quint64(h->detailVertCount)||quint64(d[i].triBase)+d[i].triCount>quint64(h->detailTriCount))throw QStringLiteral("Detail-диапазон вне тайла");for(unsigned j=0;j<d[i].triCount;++j)for(int k=0;k<3;++k)if(tris[(d[i].triBase+j)*4+k]>=p[i].vertCount+d[i].vertCount)throw QStringLiteral("Detail-индекс вне полигона");}
    }
    for(int i=0;i<h->bvNodeCount;++i)if((bv[i].i>=0&&bv[i].i>=h->offMeshBase)||(bv[i].i<0&&(-qint64(bv[i].i)>h->bvNodeCount-i)))throw QStringLiteral("Неверное BV-дерево");
    for(int i=0;i<h->offMeshConCount;++i){if(off[i].poly<h->offMeshBase||off[i].poly>=h->polyCount||!std::isfinite(off[i].rad)||off[i].rad<0||(off[i].side!=0xff&&off[i].side>7))throw QStringLiteral("Неверная offmesh-связь");for(float x:off[i].pos)if(!std::isfinite(x))throw QStringLiteral("Нечисловая offmesh-связь");}
    return outlierVertices;
}
}
QStringList nav64Profiles(){return {"small","human","large","giant","levitate","flying"};}
QVector<quint32> nav64SupportedContainers(){QVector<quint32> result;for(const auto& f:Formats)if(!result.contains(f.container))result<<f.container;return result;}
QVector<quint32> nav64SupportedAlgorithms(){QVector<quint32> result;for(const auto& f:Formats)if(!result.contains(f.algorithm))result<<f.algorithm;return result;}
QVector4D nav64AreaColor(int area){const auto c=QColor::fromHsv((area*43+100)%360,160,235);return {float(c.redF()),float(c.greenF()),float(c.blueF()),.33f};}
QString nav64AreaName(int area){
    // Producer aiMap assigns the reduced movement class (dwTerrains nibble),
    // not the original grass/sand/road material enum. The same class may come
    // from multiple materials. Use the shared legacy route-cost table.
    const int cost=CAiGraphPreviewBuilder::areaRouteCost(area);
    if(area==14)return QStringLiteral("Обычная поверхность (area 14)");
    if(cost>=0)return QStringLiteral("Затраты пути ×%1 (area %2)").arg(cost/1024.,0,'f',2).arg(area);
    return QStringLiteral("Область поверхности %1").arg(area);
}
SNav64BakerIdentity nav64BakerIdentity(const QString& path){
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&f))return {};
    const auto digest=QString::fromLatin1(hash.result().toHex());
    QSettings settings;const int algorithm=settings.value("nav64/bakerObservedAlgorithm",0).toInt();
    return {digest,settings.value("nav64/bakerObservedSha256").toString()==digest&&algorithm>0?algorithm:0};
}
QString nav64AlgorithmMismatch(quint32 meshAlgorithm,int bakerAlgorithm){return meshAlgorithm&&bakerAlgorithm&&int(meshAlgorithm)!=bakerAlgorithm?QStringLiteral("сетка алгоритма %1, запекатель — %2: перепеките").arg(meshAlgorithm).arg(bakerAlgorithm):QString();}
SNav64Bodies readNav64Bodies(const QString& path){
    SNav64Bodies out;if(path.isEmpty())return out;QFile f(path);out.exists=QFileInfo::exists(path);if(!out.exists)return out;
    try{
        if(!f.open(QIODevice::ReadOnly)||f.size()>64*1024*1024)throw QStringLiteral("Таблица тел недоступна / превышен лимит");
        Reader r{f.readAll()};if(r.u32()!=0x59444f42)throw QStringLiteral("Неверная сигнатура таблицы тел");
        out.version=r.u32();out.algorithm=r.u32();out.sampleCount=r.u32();
        if(out.version!=1)throw QStringLiteral("Версия таблицы тел %1 не поддерживается").arg(out.version);
        if(!nav64SupportedAlgorithms().contains(out.algorithm)||out.algorithm<10)throw QStringLiteral("Алгоритм таблицы тел %1 не поддерживается").arg(out.algorithm);
        if(out.sampleCount>1000000||quint64(out.sampleCount)*40>quint64(r.data.size()-r.at))throw QStringLiteral("Неверное число образцов таблицы тел");
        using Key=std::tuple<QString,float,float,float>;std::set<Key> keys;Key previous;bool first=true;
        for(quint32 i=0;i<out.sampleCount;++i){
            const auto model=r.string();if(model.isEmpty()||model.contains('/')||model.contains('\\')||model=="."||model=="..")throw QStringLiteral("Неверное имя модели таблицы тел");
            const float a=r.f32(),b=r.f32(),c=r.f32();Key key(model,a,b,c);
            if(!keys.insert(key).second||(!first&&key<previous))throw QStringLiteral("Повтор / неверный порядок образцов таблицы тел");previous=key;first=false;
            const auto ground=r.u32(),levitation=r.u32();if(out.algorithm>=26?(ground>3||levitation!=4):(ground>4||levitation<5||levitation>7))throw QStringLiteral("Неверный профиль таблицы тел");
            if(r.f32()<0)throw QStringLiteral("Отрицательный радиус таблицы тел");
            for(int pose=0;pose<3;++pose)if(r.f32()<=0)throw QStringLiteral("Неположительный рост таблицы тел");
        }r.end();out.valid=true;
    }catch(const QString& e){out.error=e;}catch(const std::bad_alloc&){out.error=QStringLiteral("Недостаточно памяти для таблицы тел");}
    return out;
}
SNav64Header readNav64Header(const QString& path,QString* error){if(error)error->clear();try{QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>Limit)throw QStringLiteral("Nav64 недоступен/превышен лимит");Reader r{f.read(65536)};return header(r);}catch(const QString& e){if(error)*error=e;return {};}}
QString findNav64ForScene(const QString& directory,const QString& mpr,const QString& mob,const QString& profile,QString* error){
    if(error)error->clear();const QString a=QFileInfo(mpr).fileName().toLower(),b=QFileInfo(mob).fileName().toLower();
    QDir dir(directory);QStringList files=dir.entryList({"*.nav64"},QDir::Files,QDir::Name);const QString fast=QFileInfo(a).completeBaseName()+"--"+QFileInfo(b).completeBaseName()+"."+profile+".nav64";files.removeAll(fast);if(dir.exists(fast))files.prepend(fast);
    QString matched;QString candidateError;
    for(const auto& name:files){QString e;auto h=readNav64Header(dir.filePath(name),&e);if(name==fast)candidateError=e;if(e.isEmpty()&&h.mpr==a&&h.mob==b&&h.profile==profile){if(name==fast)return dir.filePath(name);if(!matched.isEmpty()){if(error)*error=QStringLiteral("Несколько Nav64 с одной идентичностью: устраните дубликаты");return {};}matched=dir.filePath(name);}}
    if(matched.isEmpty()&&error)*error=candidateError.isEmpty()?QStringLiteral("Сетка не запечена: %1 / %2 / %3").arg(a,b,profile):candidateError;
    return matched;
}
SNav64Document loadNav64(const QString& path){
    SNav64Document out;out.path=path;
    try{
        QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>Limit)throw QStringLiteral("Не удалось открыть Nav64 / превышен лимит");Reader r{file.readAll()};out.header=header(r);
        dtNavMeshParams params{};for(float& x:params.orig)x=r.f32();params.tileWidth=r.f32();params.tileHeight=r.f32();params.maxTiles=int(r.u32());params.maxPolys=int(r.u32());
        const int sections=out.header.container!=3?2:3;
        if(params.tileWidth<=0||params.tileHeight<=0||params.maxTiles<=0||params.maxTiles>131072||params.maxPolys<=0||params.maxPolys>32768||r.u32()!=quint32(sections))throw QStringLiteral("Неверные параметры Detour/секции");
        if(out.header.container!=3&&((params.maxTiles&(params.maxTiles-1))||params.maxPolys!=32768||params.tileWidth!=out.header.body.cellSize*out.header.body.tileCells||params.tileHeight!=params.tileWidth))throw QStringLiteral("Несовместимые границы тайлов Nav64");
        QByteArray tileBytes;QSet<quint32> seen;
        for(int i=0;i<sections;++i){const auto type=r.u32();Reader s{r.bytes(r.u32())};if(seen.contains(type))throw QStringLiteral("Повтор секции");seen.insert(type);
            if(type==1){if(s.u32()!=5)throw QStringLiteral("Неподдерживаемый codec Nav64");const auto expanded=s.u32(),compressed=s.u32();auto input=s.bytes(compressed);s.end();tileBytes=expand(input,expanded);}
            else if(type==2){if(s.u32()!=1||s.u32()!=quint32(out.header.container!=3?2:1))throw QStringLiteral("Неверный домен Nav64");s.end();}
            else if(type==3&&out.header.container==3){if(s.u32()!=0)throw QStringLiteral("Неизвестный build layer Nav64");s.end();}
            else throw QStringLiteral("Неизвестная секция Nav64");
        }r.end();if(!seen.contains(1)||!seen.contains(2)||(sections==3&&!seen.contains(3)))throw QStringLiteral("Нет обязательной секции Nav64");
        auto mesh=std::shared_ptr<dtNavMesh>(dtAllocNavMesh(),dtFreeNavMesh);if(!mesh||dtStatusFailed(mesh->init(&params)))throw QStringLiteral("Detour init: ошибка");Reader t{tileBytes};const auto count=t.u32();if(count>quint32(params.maxTiles))throw QStringLiteral("Слишком много тайлов");
        QHash<const dtMeshTile*,QVector<quint8>> terrainSupports;
        for(quint32 i=0;i<count;++i){
            auto b=unshuffleTile(t.bytes(t.u32()));out.detailOutlierVertices+=validateTile(b,params,2*out.header.body.cellHeight);
            const auto* tileHeader=reinterpret_cast<const dtMeshHeader*>(b.constData());
            QVector<quint8> terrainOnly(tileHeader->polyCount,0);
            if(out.header.container!=3){
                const auto* h=reinterpret_cast<const dtMeshHeader*>(b.constData());
                const auto supports=t.u32();if(supports!=quint32(h->polyCount))throw QStringLiteral("Число привязок поверхностей не совпадает с полигонами");
                for(quint32 j=0;j<supports;++j){const auto sources=t.u32();if(!sources||sources>1024||sources>quint32((t.data.size()-t.at)/8))throw QStringLiteral("Неверное число исходных поверхностей полигона");
                    QSet<quint64> ids;for(quint32 k=0;k<sources;++k){const quint64 low=t.u32(),high=t.u32(),id=low|(high<<32);if(!id||ids.contains(id))throw QStringLiteral("Неверная / повторная поверхность полигона");ids.insert(id);}
                    terrainOnly[int(j)]=ids.size()==1&&ids.contains(1);
                }
            }
            auto* owned=static_cast<unsigned char*>(dtAlloc(b.size(),DT_ALLOC_PERM));if(!owned)throw QStringLiteral("Недостаточно памяти для тайла");std::memcpy(owned,b.constData(),b.size());if(dtStatusFailed(mesh->addTile(owned,b.size(),DT_TILE_FREE_DATA,0,nullptr))){dtFree(owned);throw QStringLiteral("Detour addTile: несовместимые/повторные тайлы");}
            terrainSupports.insert(static_cast<const dtNavMesh*>(mesh.get())->getTileAt(tileHeader->x,tileHeader->y,tileHeader->layer),terrainOnly);
        }t.end();out.tileCount=int(count);
        if(out.detailOutlierVertices)out.warnings<<QStringLiteral("Предупреждение: %1 detail-вершин выходят за высотные границы тайлов. Слой показывает данные файла без исправления. Нужны исправленный запекатель и повторное запекание.").arg(out.detailOutlierVertices);
        struct Poly {const dtMeshTile* tile;int index;};QVector<Poly> polys;QHash<quint64,int> indices;QHash<const dtMeshTile*,int> renderIndex;
        out.minimum=QVector3D(1e30f,1e30f,1e30f);out.maximum=-out.minimum;
        for(int i=0;i<mesh->getMaxTiles();++i){const dtMeshTile* tile=static_cast<const dtNavMesh*>(mesh.get())->getTile(i);if(!tile->header)continue;auto base=mesh->getPolyRefBase(tile);for(int j=0;j<tile->header->offMeshBase;++j)if(tile->polys[j].flags){indices.insert(base|quint64(j),polys.size());polys.append({tile,j});}
            const auto* h=tile->header;float a[3]={h->bmin[0],h->bmin[1],h->bmin[2]},b[3]={h->bmax[0],h->bmin[1],h->bmin[2]},c[3]={h->bmax[0],h->bmin[1],h->bmax[2]},d[3]={h->bmin[0],h->bmin[1],h->bmax[2]};out.tiles<<world(a)<<world(b)<<world(b)<<world(c)<<world(c)<<world(d)<<world(d)<<world(a);
            SNav64RenderTile render;const auto lo=world(h->bmin),hi=world(h->bmax);
            for(int k=0;k<3;++k){render.minimum[k]=qMin(lo[k],hi[k]);render.maximum[k]=qMax(lo[k],hi[k]);out.minimum[k]=qMin(out.minimum[k],render.minimum[k]);out.maximum[k]=qMax(out.maximum[k],render.maximum[k]);}
            for(int area=0;area<64;++area){SNav64Batch batch;batch.color=nav64AreaColor(area);render.areas.append(batch);}
            renderIndex.insert(tile,out.renderTiles.size());out.renderTiles.append(render);
        }
        QVector<int> parent(polys.size());for(int i=0;i<parent.size();++i)parent[i]=i;
        auto root=[&](int i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
        for(int i=0;i<polys.size();++i){const auto item=polys[i];const auto& p=item.tile->polys[item.index];for(unsigned l=p.firstLink;l!=DT_NULL_LINK;l=item.tile->links[l].next){auto it=indices.constFind(item.tile->links[l].ref);if(it!=indices.cend())parent[root(i)]=root(it.value());}}
        QHash<int,int> sizes;for(int i=0;i<polys.size();++i)++sizes[root(i)];int largest=-1;for(auto it=sizes.cbegin();it!=sizes.cend();++it)if(largest<0||it.value()>sizes[largest])largest=it.key();out.islandCount=sizes.size();
        QHash<int,QVector3D> islandCenters;
        for(int i=0;i<polys.size();++i){auto item=polys[i];auto* tile=item.tile;auto& render=out.renderTiles[renderIndex.value(tile)];const auto& p=tile->polys[item.index];const auto& d=tile->detailMeshes[item.index];auto& vertices=render.areas[p.getArea()].vertices;QVector3D center;
            for(int j=0;j<p.vertCount;++j){auto a=world(&tile->verts[p.verts[j]*3]),b=world(&tile->verts[p.verts[(j+1)%p.vertCount]*3]);center+=a;render.edges<<a<<b;
                // A short open portal is a diagnostic candidate, not raw wall clearance.
                if(p.neis[j]&&out.header.body.radius>0&&(a-b).length()<2*out.header.body.radius){render.narrow<<a+QVector3D(0,0,.08)<<b+QVector3D(0,0,.08);++out.narrowCount;}
            }center/=p.vertCount;if(root(i)!=largest)islandCenters[root(i)]+=center;
            for(unsigned j=0;j<d.triCount;++j){const auto* tri=&tile->detailTris[(d.triBase+j)*4];QVector3D points[3];for(int k=0;k<3;++k){const int v=tri[k];points[k]=world(v<p.vertCount?&tile->verts[p.verts[v]*3]:&tile->detailVerts[(d.vertBase+v-p.vertCount)*3]);vertices<<points[k];}out.triangles.append({points[0],points[1],points[2],p.flags,out.header.profile!="flying"&&terrainSupports.value(tile).value(item.index)!=0});}
        }for(auto it=islandCenters.cbegin();it!=islandCenters.cend();++it)out.islands<<it.value()/sizes[it.key()];
        out.polygonCount=polys.size();out.valid=true;
    }catch(const QString& e){out.error=e;out.renderTiles.clear();out.tiles.clear();out.islands.clear();}catch(const std::bad_alloc&){out.error=QStringLiteral("Недостаточно памяти для Nav64");}
    return out;
}
