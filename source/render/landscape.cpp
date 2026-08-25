#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QVector2D>
#include <QtConcurrent/QtConcurrentRun>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLTextureBlitter>

#include "landscape.h"
#include "map_object_placement_policy.h"

#include <cmath>
#include <cstring>
#include "render_performance.h"
#include "view_frustum.h"
#include "res_file.h"
#include "utils.h"
#include "resourcemanager.h"
#include "node.h"
#include "sector.h"
#include "backup_manager.h"
#include "staged_save_validator.h"
#include "log.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"
#include "terrain_virtual_atlas.h"
#include "terrain_atlas_shape_policy.h"

CLandscape* CLandscape::m_pLand = nullptr;

CLandscape* CLandscape::getInstance()
{
    if(nullptr == m_pLand)
        m_pLand = new CLandscape();
    return m_pLand;
}

void CLandscape::unloadMpr()
{
    m_canonicalTerrain = nullptr;
    clearPreviewTerrainAtlas();
    m_baseAtlasTexture = nullptr;
    m_baseAtlasColumns = 1;
    m_texture = nullptr;
    for (auto& xSec: m_aSector)
    {
        for(auto& ySec: xSec)
            delete ySec;
    }
    m_aSector.clear();
    m_aMaterial.clear();
    m_aTileTypes.clear();
    m_aAnimTile.clear();
    m_renderTextureCount = 1;
    m_renderAtlasColumns = 1;
    m_bDirty = false;
    delete m_terrainShadowTexture;
    m_terrainShadowTexture = nullptr;
    m_terrainShadowTextureWidth = 0;
    m_terrainShadowTextureHeight = 0;
    m_terrainShadowFieldGeneration = 0;
    m_terrainShadowSunDirection = QVector3D();
    m_terrainShadowDesiredGeneration = 0;
    m_terrainShadowDesiredDirection = QVector3D();
    m_terrainShadowFutureActive = false;
    ++m_shadowGeneration;
    m_materialDrawDataDirty = false;
}

CLandscape::CLandscape():
  m_texture(nullptr),
  m_bDirty(false),
  m_materialDrawDataDirty(false),
  m_canonicalTerrain(nullptr)
{
    m_aSector.clear();
    m_aAnimTile.clear();
    m_aMaterial.clear();
    m_aTileTypes.clear();
}

bool CLandscape::previewTerrainAtlasFullUpload(
    const QVector<QImage>& tileImages, int textureCount, int columns, int rows)
{
    const QImage combined = CTerrainVirtualAtlas::composeAtlasGrid(
        tileImages, textureCount, columns);
    if(combined.isNull())
        return false;
    QOpenGLTexture* replacement = new QOpenGLTexture(
        combined, QOpenGLTexture::GenerateMipMaps);
    if(!replacement->isCreated())
    {
        delete replacement;
        return false;
    }
    replacement->setMinificationFilter(QOpenGLTexture::NearestMipMapLinear);
    replacement->setMagnificationFilter(QOpenGLTexture::Nearest);
    replacement->setWrapMode(QOpenGLTexture::Repeat);
    if(m_previewAtlasTexture && m_previewAtlasTexture->isCreated())
        m_previewAtlasTexture->destroy();
    delete m_previewAtlasTexture;
    m_previewAtlasTexture = replacement;
    m_texture = replacement;
    ei::log(eLogInfo, QString(
        "Working terrain atlas full upload: slots:%1 pages:%2 grid:%3x%4 dimensions:%5x%6")
        .arg(tileImages.size()).arg(textureCount).arg(columns).arg(rows)
        .arg(combined.width()).arg(combined.height()));
    return true;
}

bool CLandscape::previewTerrainAtlasIncremental(
    const QVector<QImage>& tileImages, int textureCount, int columns, int rows,
    const QSet<int>& changedSlots)
{
    if(!m_previewAtlasTexture || !m_previewAtlasTexture->isCreated()
       || textureCount != m_previewAtlasTextureCount
       || columns != m_previewAtlasColumns || changedSlots.isEmpty())
        return false;
    const int tileSize = tileImages.front().width();
    const int pageSize = tileSize * 8;
    for(int slot : changedSlots)
        if(slot < 0 || slot >= tileImages.size()
           || tileImages.at(slot).size() != QSize(tileSize, tileSize))
            return false;

    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions* glExtra =
        QOpenGLContext::currentContext()->extraFunctions();
    GLint priorFramebuffer = 0;
    GLint priorReadBuffer = GL_BACK;
    GLint priorActiveTexture = GL_TEXTURE0;
    GLint priorTexture = 0;
    GLint priorUnpackAlignment = 4;
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &priorFramebuffer);
    gl->glGetIntegerv(GL_READ_BUFFER, &priorReadBuffer);
    gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &priorActiveTexture);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &priorTexture);
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &priorUnpackAlignment);
    while(gl->glGetError() != GL_NO_ERROR) {}

    const int width = columns * pageSize;
    const int height = rows * pageSize;
    QOpenGLTexture* candidate = new QOpenGLTexture(QOpenGLTexture::Target2D);
    const int mipLevels = 1 + int(std::floor(std::log2(
        double(qMax(width, height)))));
    bool installed = candidate->create();
    if(installed)
    {
        candidate->setSize(width, height);
        candidate->setFormat(QOpenGLTexture::RGBA8_UNorm);
        candidate->setMipLevels(mipLevels);
        candidate->allocateStorage(QOpenGLTexture::RGBA,
                                   QOpenGLTexture::UInt8);
        candidate->setMinificationFilter(
            QOpenGLTexture::NearestMipMapLinear);
        candidate->setMagnificationFilter(QOpenGLTexture::Nearest);
        candidate->setWrapMode(QOpenGLTexture::Repeat);
        installed = candidate->isStorageAllocated();
    }
    GLuint framebuffer = 0;
    if(installed)
    {
        gl->glGenFramebuffers(1, &framebuffer);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   m_previewAtlasTexture->textureId(), 0);
        installed = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)
            == GL_FRAMEBUFFER_COMPLETE;
        if(installed)
            glExtra->glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    if(installed)
    {
        gl->glBindTexture(GL_TEXTURE_2D, candidate->textureId());
        gl->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                width, height);
        installed = gl->glGetError() == GL_NO_ERROR;
    }
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for(int slot : changedSlots)
    {
        if(!installed)
            break;
        const int page = slot / 64;
        const int local = slot % 64;
        const int x = (page % columns) * pageSize
            + (local % 8) * tileSize;
        const int y = (page / columns) * pageSize
            + (7 - local / 8) * tileSize;
        const QImage image = tileImages.at(slot).format()
            == QImage::Format_RGBA8888 ? tileImages.at(slot)
            : tileImages.at(slot).convertToFormat(QImage::Format_RGBA8888);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, tileSize, tileSize,
                            GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    }
    if(installed)
        installed = gl->glGetError() == GL_NO_ERROR;
    if(installed)
    {
        candidate->generateMipMaps();
        installed = gl->glGetError() == GL_NO_ERROR;
    }
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, priorUnpackAlignment);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(priorFramebuffer));
    glExtra->glReadBuffer(GLenum(priorReadBuffer));
    if(framebuffer)
        gl->glDeleteFramebuffers(1, &framebuffer);
    const GLuint oldPreviewId = m_previewAtlasTexture->textureId();
    if(installed)
    {
        QOpenGLTexture* previous = m_previewAtlasTexture;
        m_previewAtlasTexture = candidate;
        m_texture = candidate;
        previous->destroy();
        delete previous;
        gl->glBindTexture(GL_TEXTURE_2D,
            GLuint(priorTexture) == oldPreviewId
                ? candidate->textureId() : GLuint(priorTexture));
    }
    else
    {
        if(candidate->isCreated())
            candidate->destroy();
        delete candidate;
        gl->glBindTexture(GL_TEXTURE_2D, GLuint(priorTexture));
    }
    gl->glActiveTexture(GLenum(priorActiveTexture));
    if(!installed)
    {
        ei::log(eLogWarning,
            "Incremental terrain atlas staging failed before publication; prior preview retained");
        return false;
    }
    ei::log(eLogInfo, QString(
        "Working terrain atlas incremental upload: changedSlots:%1 pages:%2 grid:%3")
        .arg(changedSlots.size()).arg(textureCount).arg(columns));
    return true;
}

bool CLandscape::previewTerrainAtlasRegridCopy(
    const QVector<QImage>& tileImages, int textureCount, int columns, int rows,
    const QSet<int>& changedSlots)
{
    if(!m_baseAtlasTexture || !m_baseAtlasTexture->isCreated()
       || m_previewAtlasTexture || changedSlots.isEmpty()
       || int(m_header.nTexture) <= 0 || textureCount <= int(m_header.nTexture))
        return false;
    const int tileSize = tileImages.front().width();
    const int pageSize = tileSize * 8;
    const int width = columns * pageSize;
    const int height = rows * pageSize;
    if(m_baseAtlasTexture->width() != m_baseAtlasColumns * pageSize
       || m_baseAtlasTexture->height()
            != ((int(m_header.nTexture) + m_baseAtlasColumns - 1)
                / m_baseAtlasColumns) * pageSize)
        return false;
    for(int slot : changedSlots)
        if(slot < int(m_header.nTexture) * 64 || slot >= tileImages.size()
           || tileImages.at(slot).size() != QSize(tileSize, tileSize))
            return false;

    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions* glExtra =
        QOpenGLContext::currentContext()->extraFunctions();
    GLint priorFramebuffer = 0;
    GLint priorReadBuffer = GL_BACK;
    GLint priorActiveTexture = GL_TEXTURE0;
    GLint priorTexture = 0;
    GLint priorUnpackAlignment = 4;
    GLfloat priorClearColor[4] = {0, 0, 0, 0};
    GLboolean priorColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint priorViewport[4] = {0, 0, 0, 0};
    GLint priorProgram = 0;
    GLint priorArrayBuffer = 0;
    const GLboolean scissorEnabled = gl->glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean framebufferSrgbEnabled =
        gl->glIsEnabled(GL_FRAMEBUFFER_SRGB);
    const GLboolean blendEnabled = gl->glIsEnabled(GL_BLEND);
    const GLboolean depthEnabled = gl->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = gl->glIsEnabled(GL_CULL_FACE);
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &priorFramebuffer);
    gl->glGetIntegerv(GL_READ_BUFFER, &priorReadBuffer);
    gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &priorActiveTexture);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &priorTexture);
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &priorUnpackAlignment);
    gl->glGetFloatv(GL_COLOR_CLEAR_VALUE, priorClearColor);
    gl->glGetBooleanv(GL_COLOR_WRITEMASK, priorColorMask);
    gl->glGetIntegerv(GL_VIEWPORT, priorViewport);
    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &priorProgram);
    gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &priorArrayBuffer);
    while(gl->glGetError() != GL_NO_ERROR) {}

    QOpenGLTexture* candidate = new QOpenGLTexture(QOpenGLTexture::Target2D);
    const int mipLevels = 1 + int(std::floor(std::log2(double(qMax(width, height)))));
    if(!candidate->create())
    {
        delete candidate;
        gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(priorFramebuffer));
        gl->glBindTexture(GL_TEXTURE_2D, GLuint(priorTexture));
        gl->glActiveTexture(GLenum(priorActiveTexture));
        return false;
    }
    candidate->setSize(width, height);
    candidate->setFormat(QOpenGLTexture::RGBA8_UNorm);
    candidate->setMipLevels(mipLevels);
    candidate->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    candidate->setMinificationFilter(QOpenGLTexture::NearestMipMapLinear);
    candidate->setMagnificationFilter(QOpenGLTexture::Nearest);
    candidate->setWrapMode(QOpenGLTexture::Repeat);
    GLuint framebuffer = 0;
    gl->glGenFramebuffers(1, &framebuffer);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, candidate->textureId(), 0);
    bool copied = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)
        == GL_FRAMEBUFFER_COMPLETE;
    if(copied)
    {
        if(scissorEnabled) gl->glDisable(GL_SCISSOR_TEST);
        if(framebufferSrgbEnabled) gl->glDisable(GL_FRAMEBUFFER_SRGB);
        if(blendEnabled) gl->glDisable(GL_BLEND);
        if(depthEnabled) gl->glDisable(GL_DEPTH_TEST);
        if(cullEnabled) gl->glDisable(GL_CULL_FACE);
        gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->glViewport(0, 0, width, height);
        gl->glClearColor(0, 0, 0, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glBindTexture(GL_TEXTURE_2D, candidate->textureId());
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   m_baseAtlasTexture->textureId(), 0);
        const bool baseFramebufferReadable =
            gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)
                == GL_FRAMEBUFFER_COMPLETE;
        if(baseFramebufferReadable)
            glExtra->glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(baseFramebufferReadable)
            for(int page = 0; page < int(m_header.nTexture); ++page)
            {
                const int sourceX = (page % m_baseAtlasColumns) * pageSize;
                const int sourceY = (page / m_baseAtlasColumns) * pageSize;
                const int destinationX = (page % columns) * pageSize;
                const int destinationY = (page / columns) * pageSize;
                gl->glCopyTexSubImage2D(GL_TEXTURE_2D, 0,
                    destinationX, destinationY, sourceX, sourceY,
                    pageSize, pageSize);
            }
        else
        {
            // Shipped terrain pages are DXT1. They are sampleable but the
            // retained Windows context reports an incomplete attachment when
            // used as an FBO color source. Render each authored page into the
            // RGBA candidate instead; no authored QImage is recomposed/uploaded.
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, candidate->textureId(), 0);
            copied = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)
                == GL_FRAMEBUFFER_COMPLETE;
            QOpenGLTextureBlitter blitter;
            copied = copied && blitter.create();
            if(copied)
            {
                blitter.bind();
                const QSize sourceSize(m_baseAtlasTexture->width(),
                                       m_baseAtlasTexture->height());
                const QRect viewport(0, 0, width, height);
                for(int page = 0; page < int(m_header.nTexture); ++page)
                {
                    const int sourceX =
                        (page % m_baseAtlasColumns) * pageSize;
                    const int sourceY =
                        (page / m_baseAtlasColumns) * pageSize;
                    const int destinationX = (page % columns) * pageSize;
                    const int destinationY = (page / columns) * pageSize;
                    const QRectF target(destinationX,
                        height - destinationY - pageSize,
                        pageSize, pageSize);
                    const QRectF source(sourceX,
                        sourceSize.height() - sourceY - pageSize,
                        pageSize, pageSize);
                    blitter.blit(m_baseAtlasTexture->textureId(),
                        QOpenGLTextureBlitter::targetTransform(
                            target, viewport),
                        QOpenGLTextureBlitter::sourceTransform(
                            source, sourceSize,
                            QOpenGLTextureBlitter::OriginBottomLeft));
                }
                blitter.release();
                copied = gl->glGetError() == GL_NO_ERROR;
            }
        }
    }
    if(copied)
    {
        gl->glBindTexture(GL_TEXTURE_2D, candidate->textureId());
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        for(int slot : changedSlots)
        {
            const int page = slot / 64;
            const int local = slot % 64;
            const int x = (page % columns) * pageSize
                + (local % 8) * tileSize;
            const int y = (page / columns) * pageSize
                + (7 - local / 8) * tileSize;
            const QImage image = tileImages.at(slot).format()
                == QImage::Format_RGBA8888 ? tileImages.at(slot)
                : tileImages.at(slot).convertToFormat(QImage::Format_RGBA8888);
            gl->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, tileSize, tileSize,
                                GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
        }
        copied = gl->glGetError() == GL_NO_ERROR;
    }
    if(copied)
    {
        candidate->generateMipMaps();
        copied = gl->glGetError() == GL_NO_ERROR;
    }
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, priorUnpackAlignment);
    gl->glClearColor(priorClearColor[0], priorClearColor[1],
                     priorClearColor[2], priorClearColor[3]);
    gl->glColorMask(priorColorMask[0], priorColorMask[1],
                    priorColorMask[2], priorColorMask[3]);
    gl->glViewport(priorViewport[0], priorViewport[1],
                   priorViewport[2], priorViewport[3]);
    gl->glUseProgram(GLuint(priorProgram));
    gl->glBindBuffer(GL_ARRAY_BUFFER, GLuint(priorArrayBuffer));
    if(scissorEnabled) gl->glEnable(GL_SCISSOR_TEST);
    if(framebufferSrgbEnabled) gl->glEnable(GL_FRAMEBUFFER_SRGB);
    if(blendEnabled) gl->glEnable(GL_BLEND);
    if(depthEnabled) gl->glEnable(GL_DEPTH_TEST);
    if(cullEnabled) gl->glEnable(GL_CULL_FACE);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(priorFramebuffer));
    glExtra->glReadBuffer(GLenum(priorReadBuffer));
    gl->glBindTexture(GL_TEXTURE_2D, GLuint(priorTexture));
    gl->glActiveTexture(GLenum(priorActiveTexture));
    gl->glDeleteFramebuffers(1, &framebuffer);
    if(!copied)
    {
        candidate->destroy();
        delete candidate;
        ei::log(eLogWarning,
            "Terrain base-atlas FBO regrid copy unsupported; using full CPU upload fallback");
        return false;
    }
    m_previewAtlasTexture = candidate;
    m_texture = candidate;
    ei::log(eLogInfo, QString(
        "Working terrain atlas regrid copy: authoredPages:%1 changedSlots:%2 grid:%3x%4")
        .arg(m_header.nTexture).arg(changedSlots.size()).arg(columns).arg(rows));
    return true;
}

bool CLandscape::previewTerrainAtlas(const QVector<QImage>& tileImages,
                                     int textureCount,
                                     const QSet<int>& changedSlots)
{
    if(textureCount <= 0 || tileImages.size() != textureCount * 64
       || !QOpenGLContext::currentContext())
        return false;
    const int tileSize = tileImages.front().width();
    if(tileSize <= 0 || tileImages.front().height() != tileSize)
        return false;
    const int pageSize = tileSize * 8;
    const int columns = CTile::preferredAtlasColumns(textureCount);
    const int rows = (textureCount + columns - 1) / columns;
    GLint maximumTextureSize = 0;
    QOpenGLContext::currentContext()->functions()->glGetIntegerv(
        GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    if(pageSize * columns > maximumTextureSize
       || pageSize * rows > maximumTextureSize)
    {
        ei::log(eLogWarning, QString(
            "Working terrain atlas grid exceeds this GPU: pages:%1 grid:%2x%3 page:%4 max:%5")
            .arg(textureCount).arg(columns).arg(rows).arg(pageSize)
            .arg(maximumTextureSize));
        return false;
    }
    QElapsedTimer uploadTimer;
    uploadTimer.start();
    QString route = QStringLiteral("full");
    bool installed = previewTerrainAtlasIncremental(
        tileImages, textureCount, columns, rows, changedSlots);
    if(installed)
        route = QStringLiteral("incremental");
    else if(previewTerrainAtlasRegridCopy(
                tileImages, textureCount, columns, rows, changedSlots))
    {
        installed = true;
        route = QStringLiteral("regrid-copy");
    }
    if(!installed)
        installed = previewTerrainAtlasFullUpload(
            tileImages, textureCount, columns, rows);
    if(!installed)
        return false;
    m_renderTextureCount = textureCount;
    m_renderAtlasColumns = columns;
    if(textureCount != m_previewAtlasTextureCount
       || columns != m_previewAtlasColumns)
    {
        m_previewAtlasTextureCount = textureCount;
        m_previewAtlasColumns = columns;
        for(QVector<CSector*>& row : m_aSector)
            for(CSector* sector : row)
                if(sector) sector->setTerrainAtlasLayout(textureCount, columns);
    }
    ei::log(eLogInfo, QString(
        "Working terrain atlas uploaded: route:%1 changedSlots:%2 slots:%3 pages:%4 grid:%5x%6 elapsedMs:%7")
        .arg(route).arg(changedSlots.size()).arg(tileImages.size())
        .arg(textureCount).arg(columns).arg(rows).arg(uploadTimer.elapsed()));
    return true;
}

void CLandscape::clearPreviewTerrainAtlas()
{
    m_texture = m_baseAtlasTexture;
    m_renderTextureCount = qMax(1, int(m_header.nTexture));
    m_renderAtlasColumns = qMax(1, m_baseAtlasColumns);
    if(m_previewAtlasTextureCount > 0 && QOpenGLContext::currentContext())
        for(QVector<CSector*>& row : m_aSector)
            for(CSector* sector : row)
                if(sector) sector->setTerrainAtlasLayout(
                    int(m_header.nTexture), m_baseAtlasColumns);
    m_previewAtlasTextureCount = 0;
    m_previewAtlasColumns = 0;
    if(!m_previewAtlasTexture)
        return;
    if(QOpenGLContext::currentContext())
    {
        if(m_previewAtlasTexture->isCreated())
            m_previewAtlasTexture->destroy();
        delete m_previewAtlasTexture;
    }
    else
    {
        // At process teardown the driver releases the texture with its
        // context. Deleting Qt's wrapper after that context has vanished can
        // dereference a dead QOpenGLContext; retain the tiny wrapper instead.
        ei::log(eLogWarning,
            "Working terrain atlas wrapper retained because no OpenGL context is current");
    }
    m_previewAtlasTexture = nullptr;
}

CLandscape::~CLandscape()
{
    unloadMpr();
}

bool CLandscape::readHeader(QDataStream& stream)
{
    const uint mapSignature = 0xce4af672;
    stream >> m_header;
    if (m_header.signature != mapSignature)
    {
        Q_ASSERT("Incorrect landscape signature" && false);
        return false;
    }

    SMaterial mat;
    for(uint i(0); i<m_header.nMaterial; ++i)
    {
        stream >> mat;
        m_aMaterial.append(mat);
    }

    int type;
    for(uint i(0); i<m_header.nTile; ++i)
    {
        stream >> type;
        if(type > 15 || type < 0)
        {
            ei::log(eLogWarning, "incorrect tile type at index: " + QString::number(i));
            m_arrIncorectTiles.append(i);
        }
        m_aTileTypes.append((ETileType)type);
    }

    SAnimTile animTile;
    for(uint i(0); i<m_header.nAnimTile; ++i)
    {
        stream >> animTile;
        m_aAnimTile.append(animTile);
    }
    return true;
}

bool CLandscape::serializeMpr(const QString& zoneName, CResFile& mprFile)
{
    QByteArray mpData;
    QDataStream mpStream(&mpData, QIODevice::WriteOnly);
    util::formatStream(mpStream);

    //write map header data (*.mp)
    m_header.nAnimTile = m_aAnimTile.size();
    m_header.nMaterial = m_aMaterial.size();
    mpStream << m_header;
    for(auto& mat: m_aMaterial)
    {
        mpStream << mat;
    }

    int type;
    for(auto& tileTyle: m_aTileTypes)
    {
        type = int(tileTyle);
        mpStream << type;
    }

    for(auto& animTile: m_aAnimTile)
    {
        mpStream << animTile;
    }
    // end of header data
    mprFile.addFiledataCaseInsensitive(zoneName + ".mp", mpData);

    for(int row(0); row < m_aSector.size(); ++row)
    {
        for(int col(0); col<m_aSector[row].size(); ++col)
        {
            QByteArray secData = m_aSector[row][col]->serializeSector();
            QString secName = QString("%1%2%3.sec").arg(zoneName).arg(col, 3, 10, QChar('0')).arg(row, 3, 10, QChar('0'));
            mprFile.addFiledataCaseInsensitive(secName, secData);
        }
    }
    return true;
}

// Generates sector suffix by number: 001002 - sector x:1 y:2
QString genSectorSuffix(int x, int y)
{
    QString name;
    name.append('0');
    if (x < 10)
    {
        name.append('0');
        name.append(QString::number(x));
    }
    else {
        name.append(QString::number(x));
    }
    name.append('0');
    if (y < 10)
    {
        name.append('0');
        name.append(QString::number(y));
    }
    else {
        name.append(QString::number(y));
    }
    return name;
}

void CLandscape::readMap(const QFileInfo& path)
{
    if (!path.exists())
        return;

    ei::setCrashContext("mpr/open/source:" + path.fileName());
    m_filePath = path;

    QFile source(path.filePath());
    if(source.open(QIODevice::ReadOnly))
        m_originalMprData = source.readAll();

    const SBackupResult openBackup = CBackupManager().snapshotOnOpen(path);
    if(!openBackup.success)
    {
        ei::log(eLogWarning, QString("MPR opened without recovery snapshot:%1 error:%2")
            .arg(path.absoluteFilePath(), openBackup.error));
        QMessageBox::warning(nullptr, "Endurance backup warning",
            QString("The MPR was opened, but its recovery snapshot could not be created.\n\n%1\n\n"
                    "Do not edit or save this file until the backup location is writable.")
                .arg(openBackup.error));
    }

    ei::log(eLogInfo, "Start read terrain: " + m_filePath.absoluteFilePath());
    ei::setCrashContext("mpr/archive/read:" + path.fileName());
    CResFile map(path.filePath());
    QMap<QString, QByteArray> aTmp =  map.bufferOfFiles();  // map contains DIfferentCaseName
    ei::log(eLogInfo, QString("MPR archive read complete file:%1 members:%2")
            .arg(path.fileName()).arg(aTmp.size()));
    ei::setCrashContext("mpr/archive/normalize:" + path.fileName());
    QMap<QString, QByteArray> aComponent;
    QString innerMapName;
    for (auto& file: aTmp.toStdMap())
    {
        QString fName = file.first.toLower();
        aComponent.insert(fName, file.second);
        if(fName.endsWith(".mp"))
            innerMapName = fName.left(fName.size() - 3);
    }
    m_innerMapName = innerMapName;

    QDataStream mpStream(aComponent[innerMapName + ".mp"]);
    util::formatStream(mpStream);
    ei::setCrashContext("mpr/header/read:" + path.fileName());
    if (!readHeader(mpStream))
    {
        ei::setCrashContext("idle");
        return;
    }

    ei::log(eLogInfo, QString("MPR namespace outer:%1 inner:%2 expectedTerrainAtlases:%3")
            .arg(path.baseName(), innerMapName).arg(m_header.nTexture));
    int texCount;
    int atlasColumns = 1;
    ei::setCrashContext("mpr/terrain-atlas/build:" + path.fileName());
    m_texture = CTextureList::getInstance()->buildLandTex(
        innerMapName, int(m_header.nTexture), texCount, atlasColumns);
    m_renderTextureCount = qMax(1, texCount);
    m_baseAtlasTexture = m_texture;
    m_baseAtlasColumns = atlasColumns;
    m_renderAtlasColumns = qMax(1, atlasColumns);
    ei::log(eLogInfo, QString("MPR terrain atlas complete file:%1 textureCount:%2")
            .arg(path.fileName()).arg(texCount));

    // read sectors
    UI2 secIndex;
    for (uint y(0); y<m_header.nYSector; ++y)
    {
        QVector<CSector*> xSec;
        for (uint x(0); x<m_header.nXSector; ++x)
        {
            ei::setCrashContext(QString("mpr/sector/read:%1:%2,%3").arg(path.fileName()).arg(x).arg(y));
            secIndex.reset(x, y);
            QDataStream secStream(aComponent.take(innerMapName + genSectorSuffix(int(x), int(y)) + ".sec"));
            util::formatStream(secStream);
            CSector* sector = new CSector(secStream, m_header.maxZ, texCount,
                                          &m_aMaterial, &m_aAnimTile);
            sector->setTerrainAtlasLayout(texCount, atlasColumns);
            sector->setIndex(secIndex);
            xSec.append(sector);
            if(sector->existsTileIndices(m_arrIncorectTiles))
            {
                qDebug() << "sector (" << x << "," << y << ")";
            }
        }
        m_aSector.append(xSec);
    }

    ei::log(eLogInfo, "End read terrain");
    ++m_shadowGeneration;
    ei::setCrashContext("idle");
}

bool CLandscape::save(const SaveProgress& progress)
{
    return saveMapAs(m_filePath, progress);
}

bool CLandscape::saveMapAs(const QFileInfo& path, const SaveProgress& progress)
{
    // TODO: check if source texture has correct textureSize and tileSize with map header

    if(m_bDirty)
    {
        ei::log(eLogFatal,
            QString("Refusing changed MPR save through the legacy rebuilding serializer path:%1")
                .arg(path.absoluteFilePath()));
        QMessageBox::critical(nullptr, "Endurance safe save",
            "Changed landscape saving remains disabled while the lossless MPR/SEC edit path is "
            "being validated. The destination was not created or replaced.");
        return false;
    }
    const QByteArray data = m_originalMprData;
    if(data.isEmpty())
    {
        QMessageBox::critical(nullptr, "Endurance safe save",
            "The original MPR bytes are unavailable. The destination was not created or replaced.");
        return false;
    }

    const QString sourcePath = QDir::cleanPath(m_filePath.absoluteFilePath());
    const QString destinationPath = QDir::cleanPath(path.absoluteFilePath());
    if(QString::compare(sourcePath, destinationPath, Qt::CaseInsensitive) == 0)
    {
        const SExistingFileComparison comparison = compareExistingFile(path, data);
        if(comparison.exists && comparison.readable && comparison.equal)
        {
            ei::log(eLogInfo, QString("Unchanged MPR save is a no-op; source bytes and timestamp preserved:%1")
                .arg(destinationPath));
            return true;
        }
        if(comparison.exists)
        {
            const QString detail = comparison.readable
                ? QString("The loaded MPR changed on disk after it was opened.")
                : QString("The loaded MPR could not be read: %1").arg(comparison.error);
            ei::log(eLogFatal, QString("Refusing unchanged MPR overwrite:%1 error:%2")
                .arg(destinationPath, detail));
            QMessageBox::critical(nullptr, "Endurance safe save",
                detail + "\n\nEndurance did not overwrite the file. Reopen it before saving.");
            return false;
        }
    }

    if(progress) progress("Freshly validating the staged MPR...", 1, 4);
    const SStagedSaveValidation validation = validateStagedSave(data, path);
    if(!validation.success)
    {
        ei::log(eLogFatal, QString("Refusing MPR save because staged validation failed path:%1 error:%2")
            .arg(path.absoluteFilePath(), validation.error));
        QMessageBox::critical(nullptr, "Endurance safe save",
            QString("The staged MPR failed fresh validation, so the destination was not replaced.\n\n%1")
                .arg(validation.error));
        return false;
    }
    ei::log(eLogInfo, QString("Staged MPR validation passed path:%1 size:%2 sha256:%3")
        .arg(path.absoluteFilePath()).arg(validation.size).arg(validation.sha256));

    if(progress) progress("Creating the pre-save backup...", 2, 4);
    if(path.exists())
    {
        const SBackupResult saveBackup = CBackupManager().snapshotBeforeSave(path);
        if(!saveBackup.success)
        {
            ei::log(eLogFatal, QString("Refusing MPR save because pre-save backup failed:%1 error:%2")
                .arg(path.absoluteFilePath(), saveBackup.error));
            QMessageBox::critical(nullptr, "Endurance safe save",
                QString("The pre-save backup failed, so the source file was not replaced.\n\n%1")
                    .arg(saveBackup.error));
            return false;
        }
    }

    if(progress) progress("Atomically replacing the destination...", 3, 4);
    QSaveFile out(path.filePath());
    if(!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
    {
        ei::log(eLogFatal, QString("Atomic MPR save failed:%1").arg(path.filePath()));
        QMessageBox::critical(nullptr, "Endurance safe save",
            "Atomic MPR save failed. The destination was not replaced.");
        return false;
    }
    m_originalMprData = data;
    m_bDirty = false;
    if(progress) progress("MPR save complete.", 4, 4);
    return true;
}

bool CLandscape::adoptCanonicalSave(const QFileInfo& path, const QByteArray& bytes)
{
    const SExistingFileComparison comparison = compareExistingFile(path, bytes);
    if(!comparison.exists || !comparison.readable || !comparison.equal)
        return false;
    m_filePath = QFileInfo(path.absoluteFilePath());
    m_originalMprData = bytes;
    m_bDirty = false;
    return true;
}

void CLandscape::setMaterials(const QVector<SMaterial>& arrMat)
{
    m_aMaterial = arrMat;
    m_materialDrawDataDirty = true;
}

void CLandscape::setAnimTils(const QVector<SAnimTile>& arrAnimTile)
{
    m_aAnimTile = arrAnimTile;
    m_materialDrawDataDirty = true;
}

void CLandscape::refreshMaterialDrawDataIfNeeded()
{
    if(!m_materialDrawDataDirty)
        return;

    for(auto& sectorRow : m_aSector)
        for(CSector* sector : sectorRow)
            if(sector)
                sector->updateDrawData();
    m_materialDrawDataDirty = false;
}

bool CLandscape::adoptCanonicalTerrainSave(
    const QFileInfo& path, const QByteArray& bytes,
    const QString& innerNamespace, uint textureCount,
    uint textureSize, uint tileCount, uint tileSize, float maxZ,
    const QVector<qint32>& tileTypes,
    const QVector<SMprAnimatedTileRecord>& animatedTiles)
{
    if(!terrainAtlasLogicalCountsFitPhysicalPages(
           int(textureCount), int(tileCount), int(textureCount * 64u),
           tileTypes.size())
       || innerNamespace.trimmed().isEmpty()
       || !adoptCanonicalSave(path, bytes))
        return false;
    m_innerMapName = innerNamespace;
    m_header.nTexture = textureCount;
    m_header.textureSize = textureSize;
    m_header.nTile = tileCount;
    m_header.tileSize = tileSize;
    m_header.maxZ = maxZ;
    m_header.nAnimTile = uint(animatedTiles.size());
    m_aTileTypes.clear();
    for(qint32 type : tileTypes)
        m_aTileTypes.append(ETileType(type));
    m_aAnimTile.clear();
    for(const SMprAnimatedTileRecord& animation : animatedTiles)
    {
        SAnimTile tile;
        tile.tileIndex = animation.tileIndex;
        tile.nPhase = animation.frameCount;
        m_aAnimTile.append(tile);
    }
    return previewCanonicalMaxZ(maxZ);
}

void CLandscape::draw(QOpenGLShaderProgram* program, const CViewFrustum* frustum)
{
    refreshMaterialDrawDataIfNeeded();
    m_texture->bind(0);
    program->setUniformValue("qt_Texture0", 0);
    const int atlasRows = qMax(1, (m_renderTextureCount
        + m_renderAtlasColumns - 1) / m_renderAtlasColumns);
    program->setUniformValue("u_overlayAtlasGrid", QVector3D(
        float(m_renderAtlasColumns), float(atlasRows), 8.0f));
    for (auto& xSec: m_aSector)
        for(auto& ySec: xSec)
        {
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
                ++performance.counters().terrainSectorsTested;
            if(frustum && !ySec->intersectsFrustum(*frustum))
            {
                if(performance.collecting())
                    ++performance.counters().terrainSectorsCulled;
                continue;
            }
            if(performance.collecting())
                ++performance.counters().terrainSectorsVisible;
            ySec->draw(program);
        }
}

namespace
{
constexpr float kTerrainShadowLengthScale = 3.0f;

bool sampleTerrainHeight(const QVector<float>& heights,
                         const QVector<quint8>& present,
                         int width, int height, float x, float y, float& result)
{
    if(x < 0.0f || y < 0.0f || x >= float(width - 1)
            || y >= float(height - 1))
        return false;
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int i00 = y0 * width + x0;
    const int i10 = y0 * width + x1;
    const int i01 = y1 * width + x0;
    const int i11 = y1 * width + x1;
    if(!present.value(i00) || !present.value(i10)
            || !present.value(i01) || !present.value(i11))
        return false;
    const float tx = x - float(x0);
    const float ty = y - float(y0);
    const float lower = heights[i00] * (1.0f - tx) + heights[i10] * tx;
    const float upper = heights[i01] * (1.0f - tx) + heights[i11] * tx;
    result = lower * (1.0f - ty) + upper * ty;
    return true;
}

float traceTerrainSunVisibility(const QVector<float>& heights,
                                const QVector<quint8>& present,
                                int width, int height, int receiverX, int receiverY,
                                const QVector2D& towardSun, float risePerUnit,
                                quint64& samples)
{
    const float receiverHeight = heights[receiverY * width + receiverX];
    const QVector2D lateral(-towardSun.y(), towardSun.x());
    int visibleRays = 0;
    const float lateralOffsets[] = {-0.35f, 0.0f, 0.35f};
    const float maximumDistance = float(qMax(width, height));
    for(float lateralOffset : lateralOffsets)
    {
        bool occluded = false;
        for(float distance = 1.5f; distance <= maximumDistance; distance += 1.0f)
        {
            const QVector2D point = QVector2D(float(receiverX), float(receiverY));
            const QVector2D sample = point + towardSun * distance
                + lateral * lateralOffset;
            float terrainHeight = 0.0f;
            if(!sampleTerrainHeight(heights, present, width, height,
                                    sample.x(), sample.y(), terrainHeight))
                break;
            ++samples;
            const float rayHeight = receiverHeight + distance * risePerUnit;
            if(terrainHeight > rayHeight + 0.06f)
            {
                occluded = true;
                break;
            }
        }
        if(!occluded)
            ++visibleRays;
    }
    return float(visibleRays) / 3.0f;
}
}

bool CLandscape::uploadTerrainShadowTexture(const STerrainShadowFieldResult& result)
{
    if(result.gridWidth < 2 || result.gridHeight < 2
            || result.visibility.size() != result.gridWidth * result.gridHeight)
        return false;

    QByteArray pixels(result.gridWidth * result.gridHeight * 4, Qt::Uninitialized);
    uchar* destination = reinterpret_cast<uchar*>(pixels.data());
    for(int index = 0; index < result.visibility.size(); ++index)
    {
        const uchar value = uchar(qRound(qBound(0.0f,
            result.visibility.at(index), 1.0f) * 255.0f));
        destination[index * 4 + 0] = value;
        destination[index * 4 + 1] = value;
        destination[index * 4 + 2] = value;
        destination[index * 4 + 3] = 255;
    }

    // Build a complete immutable snapshot before publishing it. Terrain VBOs,
    // sculpt previews, and picking passes never own or rewrite this texture.
    QOpenGLTexture* candidate = new QOpenGLTexture(QOpenGLTexture::Target2D);
    if(!candidate->create())
    {
        delete candidate;
        ei::log(eLogWarning, "Terrain shadow texture creation failed; retaining prior snapshot");
        return false;
    }
    candidate->setSize(result.gridWidth, result.gridHeight);
    candidate->setFormat(QOpenGLTexture::RGBA8_UNorm);
    candidate->setMipLevels(1);
    candidate->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    candidate->setMinificationFilter(QOpenGLTexture::Linear);
    candidate->setMagnificationFilter(QOpenGLTexture::Linear);
    candidate->setWrapMode(QOpenGLTexture::ClampToEdge);
    candidate->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                       pixels.constData());

    QOpenGLTexture* previous = m_terrainShadowTexture;
    m_terrainShadowTexture = candidate;
    m_terrainShadowTextureWidth = result.gridWidth;
    m_terrainShadowTextureHeight = result.gridHeight;
    delete previous;
    return true;
}

void CLandscape::applyTerrainShadowUniforms(QOpenGLShaderProgram* program,
                                            bool enabled)
{
    const bool available = enabled
        && m_terrainShadowTexture && m_terrainShadowTexture->isCreated()
        && m_terrainShadowTextureWidth > 1 && m_terrainShadowTextureHeight > 1;
    program->setUniformValue("u_terrainShadowEnabled", available);
    program->setUniformValue("u_terrainShadowMap", 6);
    program->setUniformValue("u_terrainShadowTextureSize",
        QVector2D(float(qMax(1, m_terrainShadowTextureWidth)),
                  float(qMax(1, m_terrainShadowTextureHeight))));
    if(available)
        m_terrainShadowTexture->bind(6);
}

void CLandscape::updateTerrainShadowVisibility(const QVector3D& sunDirection,
                                               bool allowRebuild)
{
    const int gridWidth = int(widthInWorldUnits()) + 1;
    const int gridHeight = int(heightInWorldUnits()) + 1;
    if(gridWidth < 2 || gridHeight < 2 || m_aSector.isEmpty())
        return;

    const QVector3D normalized = sunDirection.lengthSquared() > 0.000001f
        ? sunDirection.normalized() : QVector3D(0.0f, 0.0f, -1.0f);
    m_terrainShadowDesiredGeneration = m_shadowGeneration;
    m_terrainShadowDesiredDirection = normalized;

    if(m_terrainShadowFutureActive && m_terrainShadowFuture.isFinished())
    {
        const STerrainShadowFieldResult result = m_terrainShadowFuture.result();
        m_terrainShadowFutureActive = false;
        const bool stillDesired = allowRebuild
            && m_terrainShadowFutureGeneration == m_terrainShadowDesiredGeneration
            && m_terrainShadowFutureGeneration == m_shadowGeneration
            && QVector3D::dotProduct(m_terrainShadowFutureDirection,
                                     m_terrainShadowDesiredDirection) > 0.99999f
            && result.gridWidth == gridWidth && result.gridHeight == gridHeight;
        if(stillDesired)
        {
            if(!uploadTerrainShadowTexture(result))
                return;
            m_terrainShadowFieldGeneration = m_terrainShadowFutureGeneration;
            m_terrainShadowSunDirection = m_terrainShadowFutureDirection;
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
            {
                ++performance.counters().terrainShadowFieldRebuilds;
                performance.counters().terrainShadowFieldNodes += result.nodes;
                performance.counters().terrainShadowFieldSamples += result.samples;
                performance.counters().terrainShadowNanoseconds += result.nanoseconds;
            }
            ei::log(eLogInfo,
                QString("Terrain sun visibility published nodes:%1 samples:%2 worker:%3 ms")
                    .arg(result.nodes).arg(result.samples)
                    .arg(double(result.nanoseconds) / 1000000.0, 0, 'f', 2));
        }
    }

    if(m_terrainShadowTexture
            && m_terrainShadowFieldGeneration == m_shadowGeneration
            && QVector3D::dotProduct(normalized, m_terrainShadowSunDirection) > 0.99999f)
        return;

    // A stroke can advance the terrain generation dozens of times per second.
    // Keep sampling the pre-stroke snapshot and calculate only the final field;
    // publishing every complete intermediate field looks like a full-scene flash.
    if(!allowRebuild)
        return;

    // Slider changes coalesce behind the one immutable worker. The last complete
    // field remains displayed until the newest requested direction is ready.
    if(m_terrainShadowFutureActive)
        return;

    QVector<float> heights(gridWidth * gridHeight, 0.0f);
    QVector<quint8> present(gridWidth * gridHeight, 0);
    for(auto& row : m_aSector)
        for(CSector* sector : row)
            if(sector)
                sector->collectTerrainHeights(heights, present, gridWidth, gridHeight);

    m_terrainShadowFutureGeneration = m_shadowGeneration;
    m_terrainShadowFutureDirection = normalized;
    m_terrainShadowFutureActive = true;
    m_terrainShadowFuture = QtConcurrent::run(
        [heights, present, gridWidth, gridHeight, normalized]()
        {
            QElapsedTimer timer;
            timer.start();
            STerrainShadowFieldResult result;
            result.gridWidth = gridWidth;
            result.gridHeight = gridHeight;
            result.visibility.fill(1.0f, gridWidth * gridHeight);
            const QVector2D horizontal(-normalized.x(), -normalized.y());
            const float horizontalLength = horizontal.length();
            if(horizontalLength > 0.0001f && normalized.z() < -0.0001f)
            {
                const QVector2D towardSun = horizontal / horizontalLength;
                // The accepted game proxy stretches terrain self-shadows by
                // three without changing object projection geometry.
                const float risePerUnit = (-normalized.z() / horizontalLength)
                    / kTerrainShadowLengthScale;
                for(int y = 0; y < gridHeight; ++y)
                    for(int x = 0; x < gridWidth; ++x)
                    {
                        const int index = y * gridWidth + x;
                        if(!present[index])
                            continue;
                        ++result.nodes;
                        result.visibility[index] = traceTerrainSunVisibility(
                            heights, present, gridWidth, gridHeight, x, y,
                            towardSun, risePerUnit, result.samples);
                    }
            }
            result.nanoseconds = quint64(timer.nsecsElapsed());
            return result;
        });
}

void CLandscape::drawWater(QOpenGLShaderProgram *program, const CViewFrustum* frustum)
{
    refreshMaterialDrawDataIfNeeded();
    m_texture->bind(0);
    program->setUniformValue("qt_Texture0", 0);
    const int atlasRows = qMax(1, (m_renderTextureCount
        + m_renderAtlasColumns - 1) / m_renderAtlasColumns);
    program->setUniformValue("u_overlayAtlasGrid", QVector3D(
        float(m_renderAtlasColumns), float(atlasRows), 8.0f));
    for (auto& xSec: m_aSector)
        for(auto& ySec: xSec)
        {
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
                ++performance.counters().terrainSectorsTested;
            if(frustum && !ySec->intersectsFrustum(*frustum))
            {
                if(performance.collecting())
                    ++performance.counters().terrainSectorsCulled;
                continue;
            }
            if(performance.collecting())
                ++performance.counters().terrainSectorsVisible;
            ySec->drawWater(program);
        }
}

bool CLandscape::projectPt(QVector3D& point, bool land)
{
    int xIndex = int(point.x()/32.0f);
    int yIndex = int(point.y()/32.0f);
    if(m_aSector.isEmpty() || yIndex < 0 || yIndex >= m_aSector.size()
        || m_aSector.at(yIndex).isEmpty() || xIndex < 0 || xIndex >= m_aSector.at(yIndex).size()
        || m_aSector.at(yIndex).at(xIndex) == nullptr)
    {
//        point.setX(0.0f);
//        point.setY(0.0f);
        point.setZ(0.0f);
        return false;
    }
    //Q_ASSERT(yIndex < m_aSector.size() && xIndex < m_aSector.first().size());
    return m_aSector[yIndex][xIndex]->projectPt(point, land);
}

bool CLandscape::projectPt(QVector<QVector3D>& aPoint)
{
    int projectedPt(0);
    for(auto& pt: aPoint)
    {
        projectPt(pt);
        ++projectedPt;
    }

    return projectedPt == aPoint.size();
}

void CLandscape::projectPosition(CNode* pNode)
{
    if(!isMprLoad())
    {
        // Standalone MOB mode: stored coordinates are also display coordinates.
        pNode->setDrawPosition(pNode->position());
        return;
    }

    if(pNode->nodeType() == eParticle || pNode->nodeType() == eLight || pNode->nodeType() == eSound || pNode->nodeType() == ePatrolPoint || pNode->nodeType() == eLookPoint)
    {
        pNode->setDrawPosition(pNode->position());
        return;
    }
    QVector3D landPos(pNode->position());
    if(ei::usesTerrainRelativeMapObjectHeight(pNode->nodeType()))
    {
        bool sampled = false;
        if(m_canonicalTerrain)
        {
            landPos.setZ(m_canonicalTerrain->terrainSurfaceHeight(
                landPos.x(), landPos.y(), &sampled));
        }
        if(!sampled)
        {
            // Compatibility fallback for legacy/non-canonical loads only.
            landPos.setZ(0.0f);
            projectPt(landPos);
        }
        landPos = ei::resolveTerrainRelativeMapObjectPosition(
            pNode->nodeType(), pNode->position(), landPos);
    }
    else
    {
        if(pNode->nodeType() == eUnit)
            landPos -= pNode->minPosition();
        projectPt(landPos);
    }
    pNode->setDrawPosition(landPos);
}

bool CLandscape::pickTile(QVector3D& point, CTile*& pTileOut, STileLocation& tileLoc, bool bLand)
{
    int xIndex = int(point.x()/32.0f);
    int yIndex = int(point.y()/32.0f);
    if(m_aSector.isEmpty() || yIndex < 0 || yIndex >= m_aSector.size()
        || m_aSector.at(yIndex).isEmpty() || xIndex < 0 || xIndex >= m_aSector.at(yIndex).size()
        || m_aSector.at(yIndex).at(xIndex) == nullptr)
    {
        return false;
    }

    point.setZ(-1.0f);
    int row, col;
    if(m_aSector[yIndex][xIndex]->pickTile(row, col, point, bLand))
    {
        tileLoc = STileLocation{xIndex, yIndex, row, col, bLand};
        if(bLand)
            pTileOut = &(m_aSector[yIndex][xIndex]->arrTileEdit()[row][col]);
        else
            pTileOut = &(m_aSector[yIndex][xIndex]->arrWaterEdit()[row][col]);

        return true;
    }

    return false;
}

void CLandscape::setTile(const QMap<STileLocation, STileInfo>& arrTileInfo)
{
    int xSec(-1), ySec(-1);
    QMap<STileLocation, STileInfo> arrSecTileInfo;
    for(auto& tile: arrTileInfo.toStdMap())
    {
        if(xSec != tile.first.xSec)
        {
            if(xSec != -1 && ySec != -1 && !arrSecTileInfo.isEmpty())
                m_aSector[ySec][xSec]->setTile(arrSecTileInfo);
            xSec = tile.first.xSec;
            arrSecTileInfo.clear();
        }
        if(ySec != tile.first.ySec)
        {
            if(xSec != -1 && ySec != -1 && !arrSecTileInfo.isEmpty())
                m_aSector[ySec][xSec]->setTile(arrSecTileInfo);
            ySec = tile.first.ySec;
            arrSecTileInfo.clear();
        }

        arrSecTileInfo[tile.first] = tile.second;
    }
    //draw last tile data
    if(!arrSecTileInfo.isEmpty())
        m_aSector[ySec][xSec]->setTile(arrSecTileInfo);
}

void CLandscape::updateSectorDrawData(int xSec, int ySec)
{
    m_aSector[ySec][xSec]->updateDrawData();
}

void CLandscape::invalidateTerrainShadowField()
{
    ++m_shadowGeneration;
}

void CLandscape::drawTileGrid(QOpenGLShaderProgram* program)
{
    for(auto& row : m_aSector)
        for(CSector* sector : row)
            sector->drawTileGrid(program);
}

bool CLandscape::previewCanonicalSector(int xSec, int ySec, const QByteArray& payload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    return m_aSector[ySec][xSec]->previewCanonicalPayload(payload);
}

bool CLandscape::previewCanonicalTerrainSectorDelta(
    int xSec, int ySec, const QByteArray& beforePayload,
    const QByteArray& afterPayload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    return m_aSector[ySec][xSec]->previewCanonicalTerrainDelta(
        beforePayload, afterPayload);
}

bool CLandscape::previewCanonicalOverlaySector(
    int xSec, int ySec, const QByteArray& payload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    return m_aSector[ySec][xSec]->previewCanonicalOverlayPayload(payload);
}

bool CLandscape::previewCanonicalOverlaySectorDelta(
    int xSec, int ySec, const QByteArray& beforePayload,
    const QByteArray& afterPayload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    return m_aSector[ySec][xSec]->previewCanonicalOverlayDelta(
        beforePayload, afterPayload);
}

bool CLandscape::previewCanonicalMaxZ(float maxZ)
{
    if(!std::isfinite(maxZ) || maxZ <= 0.0f || m_aSector.isEmpty())
        return false;
    m_header.maxZ = maxZ;
    for(QVector<CSector*>& row : m_aSector)
        for(CSector* sector : row)
            if(sector)
                sector->setPreviewMaxZ(maxZ);
    invalidateTerrainShadowField();
    return true;
}

bool CLandscape::previewCanonicalTile(int globalX, int globalY, bool land,
                                      quint16 packedTile, bool updateSector)
{
    if(globalX < 0 || globalY < 0)
        return false;
    const int xSec = globalX / 16;
    const int ySec = globalY / 16;
    if(ySec >= m_aSector.size() || xSec >= m_aSector.at(ySec).size()
        || !m_aSector.at(ySec).at(xSec))
        return false;
    QVector<QVector<CTile>>& tiles = land
        ? m_aSector[ySec][xSec]->arrTileEdit()
        : m_aSector[ySec][xSec]->arrWaterEdit();
    if(tiles.isEmpty())
        return false;
    const int row = globalY % 16;
    const int col = globalX % 16;
    const SMprPackedTile decoded = SMprPackedTile::decode(packedTile);
    tiles[row][col].setTile(decoded.atlasSlot(), decoded.rotation);
    if(updateSector)
        m_aSector[ySec][xSec]->updateDrawData();
    return true;
}

void CLandscape::projectPositions(QList<CNode*>& aNode)
{
    for(auto& node: aNode)
    {
        projectPosition(node);
    }
}
