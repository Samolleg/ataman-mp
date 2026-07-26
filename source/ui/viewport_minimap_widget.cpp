#include "viewport_minimap_widget.h"

#include <QtWidgets>
#include <QDir>

namespace {
class CScaledImageLabel : public QLabel
{
public:
    explicit CScaledImageLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(48, 48);
    }

    void setImage(const QImage& image)
    {
        m_image = image;
        updatePixmap();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updatePixmap();
    }

private:
    void updatePixmap()
    {
        if(m_image.isNull())
        {
            clear();
            return;
        }
        setPixmap(QPixmap::fromImage(m_image).scaled(
            size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }
    QImage m_image;
};
}

CViewportMinimapWidget::CViewportMinimapWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("viewportMinimapWidget");
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(
        "#viewportMinimapWidget { background: rgba(28, 28, 28, 225); "
        "border: 1px solid #666; border-radius: 4px; }"
        "#viewportMinimapWidget QLabel { color: white; }"
        "#viewportMinimapWidget QComboBox { background: #252525; color: white; }" );

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(5, 5, 5, 5);
    root->setSpacing(4);
    QHBoxLayout* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(tr("Minimap"), this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_export = new QToolButton(this);
    m_export->setText(tr("Export..."));
    m_export->setToolTip(tr("Review and export the current generated minimap"));
    m_expand = new QToolButton(this);
    m_expand->setText(QString::fromUtf8("\xE2\x88\x92"));
    m_expand->setToolTip(tr("Minimize minimap"));
    header->addWidget(m_title);
    header->addStretch(1);
    header->addWidget(m_export);
    header->addWidget(m_expand);
    root->addLayout(header);

    m_source = new QComboBox(this);
    m_source->setObjectName("viewportMinimapSource");
    m_source->setToolTip(tr(
        "Generated preview is automatically staged for Test Current Map. "
        "Embedded uses the active profile texture without staging an override."));
    root->addWidget(m_source);
    m_image = new CScaledImageLabel(this);
    m_image->setObjectName("viewportMinimapImage");
    root->addWidget(m_image, 1);
    m_detail = new QLabel(tr("Load an MPR to generate a preview."), this);
    m_detail->setWordWrap(true);
    m_detail->setAlignment(Qt::AlignCenter);
    root->addWidget(m_detail);

    connect(m_export, &QToolButton::clicked,
            this, &CViewportMinimapWidget::exportRequested);
    connect(m_expand, &QToolButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });
    connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        updateDisplayedImage();
        emit sourceModeChanged(usesGeneratedPreview());
    });
    updateSourceChoices(false);
    setExpanded(true);
}

void CViewportMinimapWidget::setGeneratedPreview(
    const QImage& image, bool incomplete, const QString& detail)
{
    const bool preserve = m_source->count() > 0;
    m_generated = image;
    m_generatedIncomplete = incomplete;
    m_generatedDetail = detail;
    updateSourceChoices(preserve);
}

void CViewportMinimapWidget::clearGeneratedPreview(const QString& detail)
{
    m_generated = QImage();
    m_generatedDetail = detail;
    updateSourceChoices(true);
}

void CViewportMinimapWidget::setEmbeddedPreview(
    const QString& textureName, const QImage& image, const QString& source)
{
    m_embeddedName = textureName;
    m_embedded = image;
    m_embeddedSource = source;
    updateSourceChoices(true);
}

void CViewportMinimapWidget::clearEmbeddedPreview(const QString& textureName)
{
    m_embeddedName = textureName;
    m_embedded = QImage();
    m_embeddedSource.clear();
    updateSourceChoices(true);
}

void CViewportMinimapWidget::preferGenerated(bool generated)
{
    const QString mode = generated || m_embedded.isNull() ? "generated" : "embedded";
    const int index = m_source->findData(mode);
    if(index >= 0)
        m_source->setCurrentIndex(index);
    updateDisplayedImage();
}

bool CViewportMinimapWidget::usesGeneratedPreview() const
{
    return m_source->currentData().toString() == "generated";
}

void CViewportMinimapWidget::updateSourceChoices(bool preserveSelection)
{
    const QString previous = preserveSelection
        ? m_source->currentData().toString() : QString();
    const QSignalBlocker blocker(m_source);
    m_source->clear();
    if(!m_embedded.isNull())
        m_source->addItem(tr("Embedded: %1").arg(m_embeddedName), "embedded");
    if(!m_generated.isNull())
        m_source->addItem(tr("Generated preview"), "generated");
    if(m_source->count() == 0)
        m_source->addItem(tr("No minimap available"), "none");
    int index = previous.isEmpty() ? -1 : m_source->findData(previous);
    if(index < 0)
        index = !m_embedded.isNull() ? m_source->findData("embedded")
                                     : m_source->findData("generated");
    m_source->setCurrentIndex(qMax(0, index));
    updateDisplayedImage();
}

void CViewportMinimapWidget::updateDisplayedImage()
{
    CScaledImageLabel* image = static_cast<CScaledImageLabel*>(m_image);
    if(usesGeneratedPreview() && !m_generated.isNull())
    {
        image->setImage(m_generated);
        m_detail->setText(m_generatedIncomplete
            ? tr("Generated (MPR-only: static collection geometry is missing)")
            : (m_generatedDetail.isEmpty() ? tr("Generated preview")
                                            : m_generatedDetail));
        m_detail->setStyleSheet(m_generatedIncomplete ? "color: #f0b35a;" : QString());
    }
    else if(m_source->currentData().toString() == "embedded" && !m_embedded.isNull())
    {
        image->setImage(m_embedded);
        m_detail->setText(tr("Embedded profile texture\n%1")
                          .arg(QDir::toNativeSeparators(m_embeddedSource)));
        m_detail->setStyleSheet(QString());
    }
    else
    {
        image->setImage(QImage());
        m_detail->setText(m_generatedDetail.isEmpty()
            ? tr("Load an MPR to generate a preview.") : m_generatedDetail);
        m_detail->setStyleSheet(QString());
    }
    m_export->setEnabled(!m_generated.isNull());
}

void CViewportMinimapWidget::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_title->setVisible(expanded);
    m_export->setVisible(expanded);
    m_source->setVisible(expanded);
    m_detail->setVisible(expanded);
    m_expand->setText(expanded ? QString::fromUtf8("\xE2\x88\x92")
                               : QString::fromUtf8("\xE2\x96\xA1"));
    m_expand->setToolTip(expanded ? tr("Minimize minimap") : tr("Expand minimap"));
    setFixedSize(expanded ? QSize(270, 310) : QSize(78, 78));
    emit sizeModeChanged();
}
