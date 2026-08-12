#ifndef RESOURCE_MODEL_PREVIEW_H
#define RESOURCE_MODEL_PREVIEW_H

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

namespace ei {

struct SResourceModelPreview
{
    bool valid{false};
    QImage image;
    QString details;
    QStringList diagnostics;
    int componentCount{0};
    int triangleCount{0};
};

// Bounded, CPU-only preview for browser workers. It follows the established
// FIG8 vertex-component order and MOD/LNK hierarchy but creates no scene node,
// OpenGL object, cache entry, or mutable resource state.
SResourceModelPreview previewFig8(const QByteArray& bytes, const QSize& size = QSize(640, 480));
SResourceModelPreview previewModAssembly(const QByteArray& bytes,
                                         const QByteArray& companionBon = QByteArray(),
                                         const QSize& size = QSize(640, 480));

// ANM is a nested RES whose members are actions and then component channels.
// Without provider ADB metadata, action timing/selection is intentionally not
// inferred; this inventory is paired with a same-stem model preview by the UI.
QStringList inspectAnimationArchive(const QByteArray& bytes, int* actionCount = nullptr,
                                    int* channelCount = nullptr);

}

#endif // RESOURCE_MODEL_PREVIEW_H
