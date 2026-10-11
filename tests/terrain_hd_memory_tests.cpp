#include <QtTest>
#include <QtEndian>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include "terrain_atlas_archive_binding.h"
#include "terrain_virtual_atlas.h"
#include "terrain_atlas_profile.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
#include "verified_clipboard.h"
#include "endurance_document.h"
#define private public
#include "landscape.h"
#include "resourcemanager.h"
#undef private
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class TerrainHdMemoryTests : public QObject
{
    Q_OBJECT
private slots:
    void asymmetricDuplicatesUseTheDecodedRow()
    {
        QImage page(256,256,QImage::Format_RGBA8888);
        for(int y=0;y<256;++y)for(int x=0;x<256;++x){int slot=(y/32)*8+x/32;if(slot==43)slot=0;page.setPixelColor(x,y,QColor((slot&7)*32,(slot>>3)*32,0));}
        const auto payload=ei::encodeMmpDxt1(page,true);QVERIFY(!payload.isEmpty());
        QVector<QImage> expected;QSet<QByteArray> identities;
        for(int slot=0;slot<64;++slot){QImage tile;QVERIFY(ei::decodeMmpDxt1AtlasTile(payload,slot,&tile));expected.append(tile.convertToFormat(QImage::Format_RGBA8888));identities.insert(CTerrainAtlasProfileInference::pixelSha256({expected.last()}));}
        QCOMPARE(expected.at(56),expected.at(19));QCOMPARE(identities.size(),63);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument document;QStringList errors;QVERIFY(document.loadBytes(empty,"empty.res",&errors));SResArchiveNewMember member;member.name="row000.mmp";member.payload=payload;QByteArray archive;QVERIFY(document.buildWithAppendedMembers({member},&archive,&errors));
        QTemporaryDir temporary;QVERIFY(temporary.isValid());QFile file(temporary.filePath("row.res"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(archive);file.close();
        QVector<QImage> tiles;const auto binding=CTerrainAtlasArchiveBindingResolver::findExactArchive({file.fileName()},"row",1,64,CTerrainAtlasProfileInference::pixelSha256(expected),file.fileName(),&tiles);
        QVERIFY2(binding.success(),qPrintable(binding.error));QCOMPARE(tiles,expected);QSet<const uchar*> buffers;for(const auto& tile:tiles)buffers.insert(tile.constBits());QCOMPARE(buffers.size(),identities.size());QCOMPARE(tiles.at(56).constBits(),tiles.at(19).constBits());
    }
    void mappedPixelsRemainExactAndDetachForSoleOwner()
    {
#ifdef Q_OS_WIN
        const auto pattern=QStringList{QStringLiteral("ataman-terrain-pixels-*.tmp")};
        const int before=QDir(QDir::tempPath()).entryList(pattern,QDir::Files).size();
        QVector<SResArchiveNewMember> members;QVector<QImage> expected;
        for(int page=0;page<2;++page){QImage raster(2048,2048,QImage::Format_RGBA8888);
            for(int y=0;y<2048;++y)for(int x=0;x<2048;++x){int slot=page*64+(y/256)*8+x/256;raster.setPixelColor(x,y,QColor(slot*17%256,slot*71%256,slot*31%256));}
            auto payload=ei::encodeMmpDxt1(raster,true);QVERIFY(!payload.isEmpty());SResArchiveNewMember member;member.name=QString("mapped%1.mmp").arg(page,3,10,QChar('0'));member.payload=payload;members.append(member);
            for(int slot=0;slot<64;++slot){QImage tile;QVERIFY(ei::decodeMmpDxt1AtlasTile(payload,slot,&tile));expected.append(tile.convertToFormat(QImage::Format_RGBA8888));}}
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument document;QStringList errors;QVERIFY(document.loadBytes(empty,"empty.res",&errors));QByteArray archive;QVERIFY(document.buildWithAppendedMembers(members,&archive,&errors));
        QTemporaryDir temporary;QVERIFY(temporary.isValid());QFile file(temporary.filePath("mapped.res"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(archive);file.close();
        QVector<QImage> tiles;const auto binding=CTerrainAtlasArchiveBindingResolver::findExactArchive({file.fileName()},"mapped",2,128,CTerrainAtlasProfileInference::pixelSha256(expected),file.fileName(),&tiles);
        QVERIFY2(binding.success(),qPrintable(binding.error));QVERIFY(!tiles.isEmpty());
        MEMORY_BASIC_INFORMATION info={};QVERIFY(VirtualQuery(tiles.first().constBits(),&info,sizeof(info))!=0);QCOMPARE(info.Type,DWORD(MEM_MAPPED));
        QCOMPARE(tiles,expected);QCOMPARE(CTerrainAtlasProfileInference::pixelSha256(tiles),binding.decodedAtlasPixelSha256);
        CTerrainAtlasArchiveBindingResolver::releaseDecodedImageResidency(tiles);
        QCOMPARE(tiles,expected);
        CTerrainAtlasArchiveBindingResolver::releaseDecodedImageResidency(expected);
        QCOMPARE(tiles,expected);
        QCOMPARE(QDir(QDir::tempPath()).entryList(pattern,QDir::Files).size(),before+1);
        QImage sole=tiles.takeLast();const auto* readonly=sole.constBits();auto* writable=sole.bits();QVERIFY(writable!=readonly);writable[0]^=1;
        QCOMPARE(tiles.first(),expected.first());QVERIFY(sole!=expected.last());
        tiles.clear();QCOMPARE(QDir(QDir::tempPath()).entryList(pattern,QDir::Files).size(),before);
#else
        QSKIP("Windows mapped-storage policy is unavailable on this platform");
#endif
    }
    void equalPixelsShareAndDetach()
    {
        QImage page(256,256,QImage::Format_RGBA8888); page.fill(QColor(120,70,40,255));
        const QByteArray payload=ei::encodeMmpDxt1(page,true); QVERIFY(!payload.isEmpty());
        QVector<SResArchiveNewMember> members;
        QVector<QImage> expected;
        for(int p=0;p<2;++p) {
            SResArchiveNewMember member; member.name=QString("hdmem%1.mmp").arg(p,3,10,QChar('0'));
            member.payload=payload; members.append(member);
            for(int slot=0;slot<64;++slot){QImage tile;QVERIFY(ei::decodeMmpDxt1AtlasTile(payload,slot,&tile));expected.append(tile.convertToFormat(QImage::Format_RGBA8888));}
        }
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
        qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument document;QStringList errors;QVERIFY(document.loadBytes(empty,"empty.res",&errors));
        QByteArray archive;QVERIFY(document.buildWithAppendedMembers(members,&archive,&errors));
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        QFile file(temporary.filePath("tiles.res")); QVERIFY(file.open(QIODevice::WriteOnly));file.write(archive);file.close();
        QVector<QImage> tiles;
        const auto binding=CTerrainAtlasArchiveBindingResolver::findExactArchive(
            {file.fileName()},"hdmem",2,128,CTerrainAtlasProfileInference::pixelSha256(expected),file.fileName(),&tiles);
        QVERIFY2(binding.success(),qPrintable(binding.error));QCOMPARE(tiles,expected);
        QSet<const uchar*> buffers;for(const auto& tile:tiles)buffers.insert(tile.constBits());
        QCOMPARE(buffers.size(),1);
        auto changed=tiles;changed[0].fill(Qt::red);
        QCOMPARE(tiles,expected);QCOMPARE(changed[1],tiles[1]);QVERIFY(changed[0]!=tiles[0]);
    }

    void sourceImageCacheBoundsDuringUse()
    {
        auto* textures=CTextureList::getInstance();textures->m_aTextureImages.clear();textures->m_textureImageLru.clear();
        QImage retained;
        for(int i=0;i<33;++i){QImage image(1024,1024,QImage::Format_RGBA8888);image.fill(QColor(i,2,3));
            if(i==0)retained=image;textures->cacheTextureImage(QString::number(i),image);}
        QVERIFY(textures->m_textureImageLru.totalBytes()<=128ll*1024*1024);
        QVERIFY(!textures->m_aTextureImages.contains("0"));QCOMPARE(retained.pixelColor(0,0),QColor(0,2,3));
        for(int i=0;i<2100;++i)textures->cacheTextureImage("missing"+QString::number(i),QImage());
        QVERIFY(textures->m_aTextureImages.size()<=2048);
        textures->m_aTextureImages.clear();textures->m_textureImageLru.clear();
    }

    void pageUploadMatchesAllGlMipsAndPadding()
    {
        QOpenGLContext context;QSurfaceFormat format;format.setVersion(2,1);format.setRenderableType(QSurfaceFormat::OpenGL);context.setFormat(format);
        QVERIFY(context.create());QOffscreenSurface surface;surface.setFormat(context.format());surface.create();QVERIFY(context.makeCurrent(&surface));
        qInfo("GL vendor=%s renderer=%s version=%s",context.functions()->glGetString(GL_VENDOR),context.functions()->glGetString(GL_RENDERER),context.functions()->glGetString(GL_VERSION));
        QVector<QImage> tiles;
        for(int slot=0;slot<640;++slot){int size=slot<64?16:32;QImage image(size,size,QImage::Format_RGBA8888);
            for(int y=0;y<size;++y)for(int x=0;x<size;++x)image.setPixelColor(x,y,QColor((slot+x*7)%256,(slot*3+y*11)%256,(x+y+slot*5)%256,(x+y)%4?255:0));
            tiles.append(image);}
        const int columns=4,rows=3;
        const QImage combined=CTerrainVirtualAtlas::composeAtlasGrid(tiles,10,columns);
        QOpenGLTexture reference(combined,QOpenGLTexture::GenerateMipMaps);QVERIFY(reference.isCreated());
        auto land=CLandscape::createReadOnlyBackground();QVERIFY(land->previewTerrainAtlasFullUpload(tiles,10,columns,rows));
        auto* gl=context.functions();GLuint framebuffer=0;gl->glGenFramebuffers(1,&framebuffer);
        auto read=[&](GLuint texture,int level){int w=qMax(1,combined.width()>>level),h=qMax(1,combined.height()>>level);QImage image(w,h,QImage::Format_RGBA8888);
            gl->glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);gl->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,level);
            if(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return QImage();
            gl->glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,image.bits());return image;};
        QCOMPARE(land->glTexture()->mipLevels(),reference.mipLevels());
        for(int level=0;level<reference.mipLevels();++level){const auto a=read(reference.textureId(),level),b=read(land->glTexture()->textureId(),level);QVERIFY(!a.isNull());QCOMPARE(b,a);
            const QString output=qEnvironmentVariable("EI_HDMEM_PNG_ROOT");
            if(level==0 && !output.isEmpty()){QVERIFY(QDir().mkpath(output));QVERIFY(a.save(output+"/reference-gl-atlas.png"));QVERIFY(b.save(output+"/page-upload-gl-atlas.png"));}}
        gl->glBindFramebuffer(GL_FRAMEBUFFER,0);gl->glDeleteFramebuffers(1,&framebuffer);land->clearPreviewTerrainAtlas();land.clear();reference.destroy();context.doneCurrent();
    }
};
int main(int argc,char** argv)
{
    ei::TestClipboardSession clipboard(true);if(!clipboard.prepared())return 2;
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QApplication app(argc,argv);if(!clipboard.install())return 2;
    TerrainHdMemoryTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terrain_hd_memory_tests.moc"
