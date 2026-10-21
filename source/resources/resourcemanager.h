#ifndef CRESOURCEMANAGER_H
#define CRESOURCEMANAGER_H

#include <QListWidget>
#include <QFileInfo>
#include <QMap>
#include <QImage>
#include <QOpenGLTexture>
#include "persistent_visual_cache.h"
#include <QJsonObject>
#include "figure.h"
#include "types.h"
#include "render_material.h"
#include "prepared_static_visual.h"

//forward declarations
class CSettings;

// Stable identity for all configured resource sources. It includes normalized
// source paths, sizes, and modification times, so persistent derived caches are
// invalidated when the active profile or any contributing archive changes.
QString configuredResourceFingerprint(CSettings* settings);
QStringList configuredResourceSourcePaths(CSettings* settings);

///
/// \brief The CObjectList class stores information about the currently read 3D figures from the game resources.
///
class CObjectList
{
public:
    static CObjectList* getInstance();
    CObjectList(CObjectList const&) = delete;
    void operator=(CObjectList const&)  = delete;

    void loadFigures(QSet<QString>& aFigure);
    void readFigure(const QByteArray& file, const QString& name);
    void readAssembly(const QMap<QString, QByteArray>& aFile, const QString& assemblyRoot);
    ei::CFigure* getFigure(const QString& name);
    // Resolve a specific figure resource, including standalone inventory .fig
    // assets. Unlike getFigure(), this never rewrites the requested extension.
    ei::CFigure* getFigureResource(const QString& resourceName);
    CSettings* settings(){Q_ASSERT(m_pSettings); return m_pSettings;}
    void attachSettings(CSettings* pSettings) {m_pSettings = pSettings;};
    QList<QString>& figureList() {return m_arrFigureForComboBox;}
    QStringList figurePartNames(const QString& name);
    QByteArray modelAnimation(const QString& name);
    QByteArray resourceData(const QString& name, QString* sourcePath = nullptr) const;

    void initResource();
    void refreshConfiguredSources();
    static QVector<SPreparedStaticPart> prepareStaticAssembly(
        const QMap<QString,QByteArray>& files, const QString& model,
        QVector3D complexion, QStringList parts);
private:
    explicit CObjectList(bool loadAuxiliary = true);
    ~CObjectList();
    ei::CFigure* figureDefault();

private:
    static CObjectList* m_pObjectContainer;
    CSettings* m_pSettings;
    QMap<QString, ei::CFigure*> m_aFigure;
    QSet<QString> m_missingFigureNames;
    QMap<QString, QByteArray> m_animationArchives;
    QSet<QString> m_missingAnimationNames;
    QList<QString> m_arrFigureForComboBox; //optimization for cell widget
};



// https://www.gipat.ru/forum/index.php?showtopic=3357 - format description

enum ETextureFormat
{
//    DD = 0x00006666
//    ,DXT1 = 0x31545844
//    ,DXT3 = 0x33545844
//    ,PNT3 = 0x33544E50
    eMMP_5650    = 0x5650
    ,eMMP_5550    = 0x5550
    ,eMMP_5551    = 0x5551
    ,eMMP_4444    = 0x4444
    ,eMMP_8888    = 0x8888
    ,eMMP_DXT1    = 0x31545844
    ,eMMP_DXT2    = 0x32545844
    ,eMMP_DXT3    = 0x33545844
    ,eMMP_DXT4    = 0x34545844
    ,eMMP_DXT5    = 0x35545844
    ,eMMP_DXTN    = 0x00545844
    ,eMMP_PAINT   = 0x00544E50
    ,eMMP_PAINT32 = 0x32544E50
    ,eMMP_PNT3    = 0x33544E50
};

struct SMmpHeader
{
    uint m_signature;
    int m_width;
    int m_height;
    int m_mipcount;
    uint m_format;
    int size() const {return 76;}

    friend QDataStream& operator >> (QDataStream& data, SMmpHeader& header)
    {
        uint w;
        uint h;
        data >> header.m_signature >> w >> h >> header.m_mipcount >> header.m_format;
        header.m_width = int(w);
        header.m_height = int(h);
        return data;
    }
    friend QDataStream& operator << (QDataStream& data, SMmpHeader& header)
    {
        uint w = uint(header.m_width);
        uint h = uint(header.m_height);
        data << header.m_signature << w << h << header.m_mipcount << header.m_format;
        return data;
    }
};

///
/// \brief The CTextureList class stores information about the currently read textures from the game resources.
///
class CTextureList
{
public:
    static CTextureList* getInstance();
    CTextureList(CTextureList const&) = delete;
    void operator=(CTextureList const&)  = delete;

    void loadTexture(QSet<QString>& aName);
    QOpenGLTexture* texture(const QString& name);
    QOpenGLTexture* texture(const QString& name, const QString& modelName);
    // Uses the renderer's actual loader (including DXT), not the PNT3 atlas decoder.
    bool hasTexture(const QString& name, const QString& modelName);
    QOpenGLTexture* texture(const QStringList& names, const QString& modelName,
                            QString* resolvedName);
    QOpenGLTexture* layeredTexture(const QStringList& layerNames, const QString& modelName);
    // Read-only CPU images used by diagnostics. These use the same resource
    // lookup and layer composition path as the OpenGL texture above.
    QImage textureImage(const QString& name, const QString& modelName);
    QImage layeredTextureImage(const QStringList& layerNames, const QString& modelName,
                               QString* diagnostic = nullptr);
    // Acquires one landscape lease, including cache hits. Release on unload.
    QOpenGLTexture* buildLandTex(const QString& name, int expectedTexCount,
                                 int& texCount, int& atlasColumns,
                                 const QString& boundArchive = QString());
    void releaseTerrainTexture(QOpenGLTexture* texture);
    // Terrain atlases have an authored DXT mip chain. This only changes the
    // terrain sampler; character/equipment atlases deliberately remain on
    // their conservative non-mipmapped path because their small islands can
    // disappear when filtered.
    void setTerrainMipmappingEnabled(bool enabled);
    QOpenGLTexture* textureDefault();
    ERenderMaterialClass materialClass(QOpenGLTexture* texture) const;
    void attachSettings(CSettings* pSettings) {m_pSettings = pSettings;};
    void initResource();
    void refreshConfiguredSources(bool sceneObjectsRetainTextures = false);
    // Reproduce a resource refresh while a rendered MOB still owns a composed texture.
    bool layeredTextureRefreshLifetimeRegressionCheck();
    // Retain recently used composed atlases across ordinary map replacement.
    // Trimming is safe only after the last MOB has unloaded because scene
    // objects retain raw QOpenGLTexture pointers while alive.
    void trimTransientVisualCache();
    // Invalidate all composed/source images after a profile/resource change.
    void clearTransientVisualCache();
    const QList<QString>& textureList() const {return m_arrCellComboBox;}
    int extractMmpToDxt1(QVector<QImage>& outArrImage, const QStringList& inArrTextureName);
    int extractMmpToDxt1(QImage& outImage, const QString textureName);
    // Returns the exact authored MMP payload used by terrain loading. Test
    // Current Map uses this to preserve stock pages byte-for-byte while
    // appending generated working-atlas pages to a run-local RES archive.
    bool terrainMmpPayload(const QString& textureName, QByteArray* payload,
                           QString* sourcePath = nullptr) const;
    // Built-in semantic catalog sources refer to stock configured archives,
    // never to the currently open map's private/finalized atlas. A private
    // archive may reuse stock member names for completely different slots.
    bool catalogTerrainMmpPayload(const QString& textureName,
                                  QByteArray* payload,
                                  QString* sourcePath = nullptr) const;
    // Resolve the winning terrain page archive without reading the page body.
    bool terrainMmpSourceArchive(const QString& textureName,
                                 QString* sourcePath) const;
    // Read and decode only one DXT1 tile band from the winning configured RES
    // member. The exact archive/member identity keys the persistent cache.
    bool catalogTerrainMmpTile(const QString& textureName, int logicalSlot,
                               QImage* tile,
                               QString* sourceIdentity = nullptr) const;
    bool terrainMmpTile(const QString& textureName, int logicalSlot,
                        QImage* tile, QString* sourcePath = nullptr) const;
    // A finalized isolated MPR may carry a sibling <mpr>-atlas.res. This
    // session-only source is searched before configured shared terrain data.
    void setMapLocalTerrainArchive(const QString& path);
    // Expanded, ordered RES files from the active resource profile. Used to
    // bind a finalized atlas to one coherent archive before renderer loading.
    QStringList configuredResourceArchivePaths() const;
    QString terrainAuthoringArchivePath() const;
    QByteArray terrainAuthoringReferenceSha256() const;
    static SPreparedStaticTexture prepareStaticTexture(const QByteArray& data);
    QOpenGLTexture* uploadStaticTexture(const SPreparedStaticTexture& data,
                                       const QString& name, const QString& model);

private:
    CTextureList();
    ~CTextureList();
    void parse(QByteArray& data, const QString& name);
    void initAuxTexture();
    QOpenGLTexture* loadTextureResource(const QString& name, const QString& modelName,
                                        bool* found = nullptr);
    QImage loadTextureImageResource(const QString& name, const QString& modelName);

private:
    static CTextureList* m_pTextureContainer;
    QMap<QString, QOpenGLTexture*> m_aTexture;
    QMap<QString, QImage> m_aTextureImages;
    ei::CVisualCacheLru m_layeredTextureLru;
    ei::CVisualCacheLru m_textureImageLru;
    QMap<QOpenGLTexture*, ERenderMaterialClass> m_textureMaterialClasses;
    QSet<QOpenGLTexture*> m_terrainTextures;
    QHash<QOpenGLTexture*, int> m_terrainTextureUsers;
    bool m_terrainMipmappingEnabled{true};
    QSet<QString> m_missingTextureNames;
    CSettings* m_pSettings;
    QList<QString> m_arrCellComboBox; //optimization for cell widget
    QString m_mapLocalTerrainArchive;
    mutable QMap<QString, QImage> m_catalogTerrainTileCache;
};


///
/// \brief The CResourceStringList class manages string localization
///
class CResourceStringList
{
public:
    static CResourceStringList* getInstance();
    CResourceStringList(CResourceStringList const&) = delete;
    void operator=(CResourceStringList const&)  = delete;
    bool getPropList(QMap<uint, QString>& map, const EObjParam propType);
    const QMap<ETileType, QString>& tileTypes() {return m_tileType;}
    const QMap<ETerrainType, QString>& materialType() {return m_materialType;}
    const char* noLiquidIndexName();

private:
    CResourceStringList();
    ~CResourceStringList();
    void initResourceString();

private:
    static CResourceStringList* m_pResourceStringContainer;
    QMap<EObjParam, QMap<uint, QString>> m_propValueName;
    QMap<ETileType, QString> m_tileType;
    QMap<ETerrainType, QString> m_materialType;
};

bool isDIfferent(const QString& value);
QString valueDifferent();


class CNvttManager
{
public:
    CNvttManager(QString appDir);
    CNvttManager() = delete;
    ~CNvttManager() {}
    int dxtToBmp(QString pathToDxt, QString pathToBmp);
    QString bmpFromTexture(QString textureName);

private:
    QString m_nvcompress;
    bool m_bInit;
};

class CSessionDataManager
{
public:
    static CSessionDataManager* getInstance();
    void getLastSession(QString& mprPath, QVector<QString>& arrMobPath);
    void addZoneData(const QString& mprPath, const QVector<QString>& arrMobPath);
    bool getZoneData(QString& mprPath, QVector<QString>& arrMobPath);
    void addWorkspaceIdentity(const QString& kind, const QString& identifier,
                              const QString& displayName, const QString& mapZoneId);
    bool getWorkspaceIdentity(QString& kind, QString& identifier,
                              QString& displayName, QString& mapZoneId);
    void addCameraData(const QVector3D& position, const QVector3D& pivot, const QVector3D& rotation);
    bool getCameraData(QVector3D& position, QVector3D& pivot, QVector3D& rotation);
    void addQuickTileIndices(const QVector<int>& arrInd);
    bool getdQuickTileIndices(QVector<int>& arrInd);
    void saveSession();
    void reset();
    void loadSession();

private:
    CSessionDataManager();
    ~CSessionDataManager();
    QString sessionDataFile();

private:
    static CSessionDataManager* m_pSessionDataManger;
    QJsonObject m_lastSession;
};

namespace ei {
enum class ECachedCompositePreparation
{
    Pending,
    Ready,
    Unavailable
};
ECachedCompositePreparation requestCachedLayerComposite(
    const QString& resourceFingerprint, const QString& modelName,
    const QStringList& layerNames);
bool takePreparedLayerComposite(const QString& resourceFingerprint,
                                const QString& modelName,
                                const QStringList& layerNames,
                                QImage* image);
}

#endif // CRESOURCEMANAGER_H
