#include "terrain_atlas_profile.h"
#include "terrain_virtual_atlas.h"
#include "terrain_atlas_archive_binding.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
#include "native_sha256.h"
#include <QtConcurrent>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <QDateTime>

namespace {
// The persisted pre-198 byte stream, deliberately independent of the native
// backend and contiguous-image optimization under test.
QByteArray legacy(const QVector<QImage>& images)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(images.size())); hash.addData("\n",1);
    for(const auto& source:images) {
        const auto image=source.convertToFormat(QImage::Format_RGBA8888);
        hash.addData(QByteArray::number(image.width()));hash.addData("x",1);
        hash.addData(QByteArray::number(image.height()));hash.addData("\n",1);
        for(int y=0;y<image.height();++y)
            hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)),image.width()*4);
    }
    return hash.result().toHex();
}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);int failures=0,passes=0;
    const auto check=[&](bool ok,const char* name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(ok)++passes;else ++failures;};
    CNativeSha256 native;
#ifdef EI_DISABLE_NATIVE_SHA256
    check(!native.isAvailable(),"portable fallback explicitly enabled");
#else
    check(native.isAvailable(),"system SHA256 available on target Windows");
    if(!native.isAvailable())return 2;
    native.addData("a",1);native.addData("",0);native.addData("bc",2);
    check(native.result().toHex()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","NIST abc split stream");
    check(native.result().isEmpty(),"finished native state cannot produce another digest");
    CNativeSha256 empty;
    check(empty.result().toHex()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","NIST empty stream");
    CNativeSha256 failed;
    failed.addData("abc",3);failed.addData(nullptr,1);
    check(failed.result().isEmpty(),"failed stream cannot return a partial digest");
#endif
    QVector<QImage> samples{QImage()};
    check(CTerrainAtlasProfileInference::pixelSha256({})==legacy({}),"empty atlas identity exact");
    for(auto format:{QImage::Format_RGBA8888,QImage::Format_RGB888,QImage::Format_ARGB32_Premultiplied,QImage::Format_Grayscale8}) {
        QImage image(17,9,format);image.fill(QColor(53,17,91,123));samples<<image;
    }
    QByteArray padded(80*9,char(0x5a));QImage strided(reinterpret_cast<uchar*>(padded.data()),17,9,80,QImage::Format_RGBA8888);
    for(int y=0;y<9;++y)for(int x=0;x<17;++x)strided.setPixelColor(x,y,QColor(x*7,y*19,61,x*11));
    samples<<strided;
    const auto expected=legacy(samples);
    check(CTerrainAtlasProfileInference::pixelSha256(samples)==expected,"mixed formats, alpha, null and padded stride retain persisted identity");
    for(int y=0;y<9;++y)for(int x=68;x<80;++x)padded[y*80+x]=char(0xa1);
    check(CTerrainAtlasProfileInference::pixelSha256(samples)==expected,"row padding excluded from identity");
    auto changed=samples;changed[1].setPixelColor(0,0,Qt::red);
    check(CTerrainAtlasProfileInference::pixelSha256(changed)==legacy(changed) && legacy(changed)!=expected,"pixel mutation changes exact identity");
    std::reverse(changed.begin(),changed.end());
    check(CTerrainAtlasProfileInference::pixelSha256(changed)==legacy(changed),"ordered dimensions and pixels retained");
    QVector<QFuture<QByteArray>> workers;
    for(int i=0;i<8;++i)workers<<QtConcurrent::run([samples](){return CTerrainAtlasProfileInference::pixelSha256(samples);});
    bool exact=true;for(auto& worker:workers)exact&=worker.result()==expected;
    check(exact,"independent hash states safe across eight workers");
    QImage sd(128,128,QImage::Format_RGBA8888);sd.fill(QColor(31,77,101));
    QImage hd(256,256,QImage::Format_RGBA8888);hd.fill(QColor(80,20,17,131));
    check(CTerrainAtlasProfileInference::pixelSha256({sd,hd})==legacy({sd,hd}),"native SD/HD atlas identity exact");
    QVector<QImage> large(1024,sd);QElapsedTimer timer;timer.start();const auto baseline=legacy(large);const auto oldMs=timer.elapsed();timer.restart();
    const auto digest=CTerrainAtlasProfileInference::pixelSha256(large);const auto newMs=timer.elapsed();
    check(digest==baseline,"64MiB atlas hash exactly matches legacy stream");
    out<<"TIME atlas64MiB legacy "<<oldMs<<" ms current "<<newMs<<" ms\n";out.flush();

    // Exact RES binding must still reject a complete wrong atlas and detect
    // a same-length payload change. No stat-only validation was introduced.
    QTemporaryDir scratch;check(scratch.isValid(),"private archive fixtures");
    QString diagnostic;QVector<QImage> tiles(64,sd);
    const auto page=CTerrainVirtualAtlas::composePage(tiles,0,128);
    const auto mmp=ei::encodeMmpDxt1(page,true,&diagnostic);
    check(!mmp.isEmpty(),"synthetic native MMP encoded");
    QVector<QImage> actual;for(int i=0;i<64;++i){QImage image;if(!ei::decodeMmpDxt1AtlasTile(mmp,i,&image,&diagnostic))return 3;actual<<image;}
    const auto atlasHash=legacy(actual);
    QByteArray emptyArchive(16,0);qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(emptyArchive.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(emptyArchive.data()+8));
    CResArchiveDocument archive;QStringList errors;check(archive.loadBytes(emptyArchive,"synthetic.res",&errors),"empty RES fixture valid");
    const auto write=[&](const QString& path,const QByteArray& payload){SResArchiveNewMember member;member.name="hash198000.mmp";member.payload=payload;QByteArray bytes;
        if(!archive.buildWithAppendedMembers({member},&bytes,&errors))return false;QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    const QString good=scratch.filePath("good.res"),bad=scratch.filePath("bad.res");auto damaged=mmp;damaged[76]=char(damaged[76]^1);
    check(write(good,mmp)&&write(bad,damaged),"good and same-length wrong RES fixtures");
    auto binding=CTerrainAtlasArchiveBindingResolver::findExactArchive({bad,good},"hash198",1,64,atlasHash);
    check(binding.success()&&QFileInfo(binding.archivePath)==QFileInfo(good),"complete wrong atlas rejected before selecting exact archive");
    QVector<QImage> warmDecoded;
    const auto warm=CTerrainAtlasArchiveBindingResolver::findExactArchive({good},"hash198",1,64,atlasHash,QString(),&warmDecoded);
    check(warm.success()&&warmDecoded.size()==64&&legacy(warmDecoded)==atlasHash,"cached digest still returns exact decoded tiles when requested");
    check(CTerrainAtlasArchiveBindingResolver::matchesArchiveEpoch(binding),"verified file epoch accepted");
    const auto originalTime=QFileInfo(good).lastModified();
    check(write(good,damaged),"replace same-length native payload");
    QFile changedFile(good);check(changedFile.open(QIODevice::ReadWrite) && changedFile.setFileTime(originalTime,QFileDevice::FileModificationTime),"restore original size and modification time");changedFile.close();
    check(!CTerrainAtlasArchiveBindingResolver::matchesArchiveEpoch(binding),"strong epoch rejects same-size edit with restored modification time");
    binding=CTerrainAtlasArchiveBindingResolver::findExactArchive({good},"hash198",1,64,atlasHash);
    check(!binding.success()&&binding.hasCompleteArchive(good),"changed payload fails original atlas identity");
    out<<"RESULT passes "<<passes<<" failures "<<failures<<"\n";return failures?1:0;
}
