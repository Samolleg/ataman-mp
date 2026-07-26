#ifndef VIEWPORT_MINIMAP_WIDGET_H
#define VIEWPORT_MINIMAP_WIDGET_H

#include <QFrame>
#include <QImage>

class QComboBox;
class QLabel;
class QToolButton;

class CViewportMinimapWidget : public QFrame
{
    Q_OBJECT
public:
    explicit CViewportMinimapWidget(QWidget* parent = nullptr);

    void setMapDimensions(int sectorWidth, int sectorHeight);
    void setGeneratedPreview(const QImage& image, bool incomplete,
                             const QString& detail);
    void clearGeneratedPreview(const QString& detail = QString());
    void setEmbeddedPreview(const QString& textureName, const QImage& image,
                            const QString& source);
    void clearEmbeddedPreview(const QString& textureName = QString());
    void preferGenerated(bool generated);
    bool usesGeneratedPreview() const;
    bool hasGeneratedPreview() const { return !m_generated.isNull(); }
    bool hasEmbeddedPreview() const { return !m_embedded.isNull(); }
    bool autoRegenerationEnabled() const;
    void setGenerationLocked(bool locked);
    bool isExpanded() const { return m_expanded; }
    int sizeLevel() const { return m_sizeLevel; }

signals:
    void exportRequested();
    void sourceModeChanged(bool generated);
    void sizeModeChanged();
    void regenerationRequested();
    void autoRegenerationChanged(bool enabled);
    void unlockRequested();

private:
    void updateSourceChoices(bool preserveSelection);
    void updateDisplayedImage();
    void setExpanded(bool expanded);
    void setSizeLevel(int level);
    QImage visibleMapImage(const QImage& image) const;

    QComboBox* m_source{nullptr};
    QLabel* m_image{nullptr};
    QLabel* m_detail{nullptr};
    QLabel* m_title{nullptr};
    QToolButton* m_export{nullptr};
    QToolButton* m_refresh{nullptr};
    QToolButton* m_autoRegenerate{nullptr};
    QToolButton* m_unlock{nullptr};
    QToolButton* m_expand{nullptr};
    QToolButton* m_smaller{nullptr};
    QToolButton* m_larger{nullptr};
    QImage m_generated;
    QImage m_embedded;
    QString m_generatedDetail;
    QString m_embeddedName;
    QString m_embeddedSource;
    bool m_generatedIncomplete{false};
    bool m_generationLocked{false};
    bool m_expanded{true};
    int m_sizeLevel{0};
    int m_sectorWidth{0};
    int m_sectorHeight{0};
};

#endif
