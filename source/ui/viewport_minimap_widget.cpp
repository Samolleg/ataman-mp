#include "viewport_minimap_widget.h"

#include <QtWidgets>
#include <QDir>
#include <QSettings>
#include <functional>

#include "viewport_overlay_style.h"

namespace {
QIcon centeredLockIcon(bool locked)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(245, 245, 245));
    pen.setWidthF(1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(QColor(245, 245, 245));
    painter.drawRoundedRect(QRectF(4.0, 8.0, 10.0, 7.0), 1.2, 1.2);
    painter.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    if(locked)
    {
        shackle.moveTo(6.0, 8.0);
        shackle.lineTo(6.0, 6.4);
        shackle.cubicTo(6.0, 2.9, 12.0, 2.9, 12.0, 6.4);
        shackle.lineTo(12.0, 8.0);
    }
    else
    {
        shackle.moveTo(7.0, 8.0);
        shackle.lineTo(7.0, 6.4);
        shackle.cubicTo(7.0, 3.1, 12.8, 3.1, 12.8, 6.2);
    }
    painter.drawPath(shackle);
    return QIcon(pixmap);
}

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

    void setFootprint(const QPolygonF& footprint)
    {
        m_footprint = footprint;
        updatePixmap();
    }

    void setClickHandler(const std::function<void(const QPointF&)>& handler)
    { m_clickHandler = handler; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updatePixmap();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton && handlePointer(event->pos()))
        {
            m_dragging = true;
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(m_dragging && (event->buttons() & Qt::LeftButton))
        {
            handlePointer(event->pos());
            event->accept();
            return;
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton && m_dragging)
        {
            handlePointer(event->pos());
            m_dragging = false;
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

private:
    bool handlePointer(const QPoint& position)
    {
        const QPixmap* displayed = pixmap();
        if(!displayed || displayed->isNull() || !m_clickHandler)
            return false;
        const QRect imageRect((width() - displayed->width()) / 2,
                              (height() - displayed->height()) / 2,
                              displayed->width(), displayed->height());
        if(!imageRect.contains(position))
            return false;
        const QPointF normalized(
            qBound(0.0, double(position.x() - imageRect.left())
                          / qMax(1, imageRect.width()), 1.0),
            qBound(0.0, double(position.y() - imageRect.top())
                          / qMax(1, imageRect.height()), 1.0));
        m_clickHandler(normalized);
        return true;
    }

    void updatePixmap()
    {
        if(m_image.isNull())
        {
            clear();
            return;
        }
        QImage decorated = m_image.convertToFormat(QImage::Format_ARGB32);
        if(m_footprint.size() >= 3)
        {
            QPolygonF polygon;
            for(const QPointF& point : m_footprint)
                polygon << QPointF(point.x() * decorated.width(),
                                   point.y() * decorated.height());
            QPainter painter(&decorated);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen framePen(Qt::white, 2.0);
            framePen.setCosmetic(true);
            framePen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(framePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(polygon);
        }
        setPixmap(QPixmap::fromImage(decorated).scaled(
            size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }
    QImage m_image;
    QPolygonF m_footprint;
    std::function<void(const QPointF&)> m_clickHandler;
    bool m_dragging{false};
};
}

CViewportMinimapWidget::CViewportMinimapWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("viewportMinimapWidget");
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(
        "#viewportMinimapWidget { background: rgb(28, 28, 28); "
        "border: 1px solid #666; border-radius: 4px; }"
        "#viewportMinimapWidget QLabel { color: white; }"
        "#viewportMinimapWidget QComboBox { background: #252525; color: white; }" );
    ensurePolished();
    configureViewportOverlaySurface(this, QColor(28, 28, 28));

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(5, 5, 5, 5);
    root->setSpacing(4);
    QHBoxLayout* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(tr("Minimap"), this);
    m_title->setObjectName("viewportMinimapTitle");
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_export = new QToolButton(this);
    m_export->setText(tr("Export..."));
    m_export->setToolTip(tr("Review and export the current generated minimap"));
    m_refresh = new QToolButton(this);
    m_refresh->setObjectName("viewportMinimapRefresh");
    m_refresh->setText(QString::fromUtf8("\xE2\x86\xBB"));
    m_refresh->setToolTip(tr("Regenerate now using current object visibility"));
    m_lock = new QToolButton(this);
    m_lock->setObjectName("viewportMinimapLock");
    m_lock->setCheckable(true);
    m_lock->setFixedSize(29, 27);
    m_lock->setIconSize(QSize(18, 18));
    m_lock->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lock->setToolTip(tr("Lock or unlock generated minimap regeneration"));
    m_smaller = new QToolButton(this);
    m_smaller->setObjectName("viewportMinimapSmaller");
    m_smaller->setText(tr("-"));
    m_smaller->setToolTip(tr("Use a smaller minimap"));
    m_larger = new QToolButton(this);
    m_larger->setObjectName("viewportMinimapLarger");
    m_larger->setText(tr("+"));
    m_larger->setToolTip(tr("Use a larger minimap"));
    m_expand = new QToolButton(this);
    m_expand->setObjectName("viewportMinimapExpand");
    m_expand->setText(QString::fromUtf8("\xE2\x96\x81"));
    m_expand->setToolTip(tr("Minimize minimap"));
    header->addWidget(m_title);
    header->addStretch(1);
    header->addWidget(m_export);
    header->addWidget(m_refresh);
    header->addWidget(m_lock);
    header->addWidget(m_smaller);
    header->addWidget(m_larger);
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
    static_cast<CScaledImageLabel*>(m_image)->setClickHandler(
        [this](const QPointF& normalized) {
            moveCameraFromNormalizedPoint(normalized);
        });
    m_detail = new QLabel(tr("Load an MPR to generate a preview."), this);
    m_detail->setWordWrap(true);
    m_detail->setAlignment(Qt::AlignCenter);
    root->addWidget(m_detail);

    connect(m_export, &QToolButton::clicked,
            this, &CViewportMinimapWidget::exportRequested);
    connect(m_refresh, &QToolButton::clicked, this, [this]() {
        m_selectGeneratedAfterRefresh = true;
        emit regenerationRequested();
    });
    connect(m_lock, &QToolButton::toggled, this,
            &CViewportMinimapWidget::generationLockRequested);
    connect(m_smaller, &QToolButton::clicked, this, [this]() {
        setSizeLevel(m_sizeLevel - 1);
    });
    connect(m_larger, &QToolButton::clicked, this, [this]() {
        setSizeLevel(m_sizeLevel + 1);
    });
    connect(m_expand, &QToolButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });
    connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        updateDisplayedImage();
        emit sourceModeChanged(usesGeneratedPreview());
    });
    updateSourceChoices(false);
    m_sizeLevel = qBound(0, QSettings().value(
        "ui/viewportMinimapSizeLevel", 0).toInt(), 2);
    setGenerationLocked(false);
    setExpanded(true);
}

void CViewportMinimapWidget::setMapDimensions(int sectorWidth, int sectorHeight)
{
    m_sectorWidth = qMax(0, sectorWidth);
    m_sectorHeight = qMax(0, sectorHeight);
    updateDisplayedImage();
}

void CViewportMinimapWidget::setGeneratedPreview(
    const QImage& image, bool incomplete, const QString& detail)
{
    const bool preserve = m_source->count() > 0;
    m_generated = image;
    m_generatedIncomplete = incomplete;
    m_generatedDetail = detail;
    updateSourceChoices(preserve);
    if(m_selectGeneratedAfterRefresh)
    {
        m_selectGeneratedAfterRefresh = false;
        preferGenerated(true);
    }
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

void CViewportMinimapWidget::setGenerationLocked(bool locked)
{
    m_generationLocked = locked;
    const QSignalBlocker blocker(m_lock);
    m_lock->setChecked(locked);
    m_lock->setText(QString());
    m_lock->setIcon(centeredLockIcon(locked));
    m_lock->setToolTip(locked ? tr("Locked: click to enable regeneration")
                              : tr("Unlocked: changes regenerate automatically"));
    m_refresh->setEnabled(!locked);
    if(locked)
        m_detail->setToolTip(tr("Applied and locked; unlock before regenerating."));
    else
        m_detail->setToolTip(QString());
}

void CViewportMinimapWidget::setCameraFootprint(
    const QPolygonF& normalizedFootprint)
{
    m_cameraFootprint = normalizedFootprint;
    static_cast<CScaledImageLabel*>(m_image)->setFootprint(m_cameraFootprint);
}

void CViewportMinimapWidget::moveCameraFromNormalizedPoint(
    const QPointF& normalizedPoint)
{
    if(m_sectorWidth <= 0 || m_sectorHeight <= 0)
        return;
    const QPointF normalized(qBound(0.0, normalizedPoint.x(), 1.0),
                             qBound(0.0, normalizedPoint.y(), 1.0));
    emit cameraMoveRequested(QPointF(
        normalized.x() * m_sectorWidth * 32.0,
        (1.0 - normalized.y()) * m_sectorHeight * 32.0));
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
        image->setImage(visibleMapImage(m_generated));
        m_detail->setText(m_generatedIncomplete
            ? tr("Generated (MPR-only: no MOB source is loaded)")
            : (m_generatedDetail.isEmpty() ? tr("Generated preview")
                                            : m_generatedDetail));
        m_detail->setStyleSheet(m_generatedIncomplete ? "color: #f0b35a;" : QString());
    }
    else if(m_source->currentData().toString() == "embedded" && !m_embedded.isNull())
    {
        image->setImage(visibleMapImage(m_embedded));
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
    m_lock->setEnabled(!m_generated.isNull());
}

void CViewportMinimapWidget::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_title->setVisible(true);
    m_export->setVisible(expanded);
    m_refresh->setVisible(expanded);
    m_lock->setVisible(expanded);
    m_smaller->setVisible(expanded);
    m_larger->setVisible(expanded);
    m_source->setVisible(expanded);
    m_image->setVisible(expanded);
    m_detail->setVisible(expanded);
    m_expand->setText(expanded ? QString::fromUtf8("\xE2\x96\x81")
                               : QString::fromUtf8("\xE2\x96\xA1"));
    m_expand->setToolTip(expanded ? tr("Minimize minimap") : tr("Expand minimap"));
    setSizeLevel(m_sizeLevel);
    configureViewportOverlaySurface(this, QColor(28, 28, 28));
}

void CViewportMinimapWidget::setSizeLevel(int level)
{
    m_sizeLevel = qBound(0, level, 2);
    m_smaller->setEnabled(m_sizeLevel > 0);
    m_larger->setEnabled(m_sizeLevel < 2);
    static const QSize expandedSizes[] = {
        QSize(390, 430), QSize(520, 560), QSize(910, 980)
    };
    setFixedSize(m_expanded ? expandedSizes[m_sizeLevel] : QSize(116, 38));
    QSettings().setValue("ui/viewportMinimapSizeLevel", m_sizeLevel);
    emit sizeModeChanged();
}

QImage CViewportMinimapWidget::visibleMapImage(const QImage& image) const
{
    if(image.isNull() || m_sectorWidth <= 0 || m_sectorHeight <= 0
       || image.width() != image.height() || m_sectorWidth == m_sectorHeight)
        return image.mirrored(false, true);
    const int largest = qMax(m_sectorWidth, m_sectorHeight);
    const int width = qBound(1,
        qRound(double(image.width()) * m_sectorWidth / largest), image.width());
    const int height = qBound(1,
        qRound(double(image.height()) * m_sectorHeight / largest), image.height());
    return image.copy(0, 0, width, height).mirrored(false, true);
}
