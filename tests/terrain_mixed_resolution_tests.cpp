#include <QtTest>
#include <QtEndian>
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_profile.h"
#include "terrain_atlas_shape_policy.h"
#include "terrain_virtual_atlas.h"
#include "res_archive_document.h"
#include "tile_form.h"
#include "verified_clipboard.h"
#include "mmp_codec.h"
#include "endurance_document.h"
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include "tile.h"
#define private public
#include "landscape.h"
#undef private

class MixedResolutionTests : public QObject
{
    Q_OBJECT
private slots:
    void mixedGlRegridAndIncrementalUseMaximumRaster()
    {
        QOpenGLContext context;QSurfaceFormat format;format.setVersion(2,1);
        format.setRenderableType(QSurfaceFormat::OpenGL);context.setFormat(format);
        QVERIFY(context.create());QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
        QVERIFY(context.makeCurrent(&surface));
        QVector<QImage> native;
        for(int slot=0;slot<192;++slot) {
            QImage image(slot<64?256:128,slot<64?256:128,QImage::Format_RGBA8888);
            image.fill(slot<64?Qt::red:(slot<128?Qt::green:Qt::blue));native.append(image);
        }
        const auto baseline=native.mid(0,128);
        const int columns=CTile::preferredAtlasColumns(2);
        auto* base=new QOpenGLTexture(CTerrainVirtualAtlas::composeAtlasGrid(baseline,2,columns));
        QVERIFY(base->isCreated());
        auto land=CLandscape::createReadOnlyBackground();land->m_header.nTexture=2;
        land->m_baseAtlasTexture=base;land->m_baseAtlasColumns=columns;land->m_texture=base;
        QSet<int> changed;for(int slot=128;slot<192;++slot)changed.insert(slot);
        QVERIFY(land->previewTerrainAtlas(native,3,changed));
        QCOMPARE(land->glTexture()->width()/CTile::preferredAtlasColumns(3)/8,256);
        QCOMPARE(native.mid(0,128),baseline);QCOMPARE(native.at(128).size(),QSize(128,128));
        auto* gl=context.functions();GLuint framebuffer=0;gl->glGenFramebuffers(1,&framebuffer);
        auto colorAtNewSlot=[&]() {
            gl->glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,land->glTexture()->textureId(),0);
            if(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return QColor();
            uchar pixel[4]={};const int cols=CTile::preferredAtlasColumns(3);
            gl->glReadPixels((2%cols)*2048+128,(2/cols)*2048+7*256+128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            gl->glBindFramebuffer(GL_FRAMEBUFFER,0);return QColor(pixel[0],pixel[1],pixel[2],pixel[3]);
        };
        QCOMPARE(colorAtNewSlot(),QColor(Qt::blue));
        auto edited=native;edited[128].fill(Qt::yellow);
        QVERIFY(land->previewTerrainAtlas(edited,3,{128},native));
        QCOMPARE(colorAtNewSlot(),QColor(Qt::yellow));
        gl->glDeleteFramebuffers(1,&framebuffer);
        land->clearPreviewTerrainAtlas();land->m_baseAtlasTexture=nullptr;land->m_texture=nullptr;land.clear();
        delete base;context.doneCurrent();
    }
    void nativeBindingAdoptionAndPicker()
    {
        QVector<QImage> native;
        QVector<SResArchiveNewMember> members;
        for(int page=0;page<2;++page)
        {
            const int size=page==0?128:256;
            QImage raster(size*8,size*8,QImage::Format_RGBA8888);
            raster.fill(page==0?Qt::red:Qt::green);
            const auto bytes=ei::encodeMmpDxt1(raster,true);
            QVERIFY(!bytes.isEmpty());
            SResArchiveNewMember member;
            member.name=QString("mixed137%1.mmp").arg(page,3,10,QChar('0'));
            member.payload=bytes;members.append(member);
            for(int slot=0;slot<64;++slot) {
                QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(bytes,slot,&image));native.append(image.convertToFormat(QImage::Format_RGBA8888));
            }
        }
        STerrainAtlasProfile profile;profile.mprNamespace="mixed137";
        profile.atlasSlotCount=128;profile.textureCount=2;profile.approved=true;
        profile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(native);
        STerrainAtlasProfileFamily family;family.id=1;family.name="Ground";family.thumbnailSlot=0;family.tileType=1;
        profile.families.append(family);
        for(int slot=0;slot<128;++slot) {
            STerrainAtlasProfileSlot definition;definition.slot=slot;definition.assigned=true;
            definition.signature.northWest=definition.signature.northEast=definition.signature.southEast=definition.signature.southWest=1;
            profile.slotDefinitions.append(definition);
        }
        CValidationReport report;QVERIFY(profile.normalizeMembership(&report));
        SResArchiveNewMember embedded;embedded.name=CTerrainAtlasProfileStore::embeddedMemberName("mixed137");
        embedded.payload=profile.toBytes();members.append(embedded);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
        qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QStringList errors;QVERIFY(archive.loadBytes(empty,"empty.res",&errors));
        QByteArray bytes;QVERIFY(archive.buildWithAppendedMembers(members,&bytes,&errors));
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("mixed.res");
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        const auto binding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({path},"mixed137");
        QVERIFY2(binding.success(),qPrintable(binding.error));
        QCOMPARE(binding.decodedAtlasTiles,native);
        QCOMPARE(binding.decodedAtlasPixelSha256,profile.atlasPixelSha256);
        QVERIFY(binding.canReuseDecodedAtlasTiles(path,"MIXED137",128,&report));
        QVERIFY(!binding.canReuseDecodedAtlasTiles(path,"wrong",128));
        QVERIFY(!binding.canReuseDecodedAtlasTiles(path,"mixed137",64));
        CTileForm picker;QVERIFY(picker.adoptTileImages(binding.decodedAtlasTiles,QVector<qint32>(128,1)));
        QCOMPARE(picker.tileImages(),native);
        QCOMPARE(CTerrainAtlasProfileInference::pixelSha256(picker.tileImages()),profile.atlasPixelSha256);
        auto scaled=native;for(auto& tile:scaled)tile=terrainAtlasRasterTile(tile,QSize(256,256));
        QVERIFY(CTerrainAtlasProfileInference::pixelSha256(scaled)!=profile.atlasPixelSha256);
        QVERIFY(!binding.matchesLoadedAtlas(path,"mixed137",CTerrainAtlasProfileInference::pixelSha256(scaled),128));
        auto invalid=binding;invalid.decodedAtlasTiles[0]=QImage(256,128,QImage::Format_RGBA8888);
        QVERIFY(!invalid.canReuseDecodedAtlasTiles(path,"mixed137",128));
        QVERIFY(file.open(QIODevice::Append));file.write("changed");file.close();
        QVERIFY(!binding.canReuseDecodedAtlasTiles(path,"mixed137",128));
    }
};
int main(int argc,char**argv)
{
    ei::TestClipboardSession clipboard(true);if(!clipboard.prepared())return 2;
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication app(argc,argv);if(!clipboard.install())return 2;
    MixedResolutionTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terrain_mixed_resolution_tests.moc"
