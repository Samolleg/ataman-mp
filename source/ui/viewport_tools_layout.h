#pragma once

#include <QLayout>
#include <QWidget>

// Wrap complete tool groups, keeping each caption and its controls together.
// Height-for-width lets the workspace give the remaining height to the view.
class CViewportToolsLayout final : public QLayout
{
public:
    explicit CViewportToolsLayout(QWidget* parent) : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(6);
    }
    ~CViewportToolsLayout() override
    {
        while(QLayoutItem* item = takeAt(0)) delete item;
    }
    void addItem(QLayoutItem* item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem* itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem* takeAt(int index) override
    {
        return index >= 0 && index < count() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override
    {
        QSize size;
        for(auto* item : m_items)
            if(!item->isEmpty()) size = size.expandedTo(item->minimumSize());
        const auto m=contentsMargins();
        return size+QSize(m.left()+m.right(),m.top()+m.bottom());
    }
    QSize sizeHint() const override
    {
        QSize size;
        for(auto* item : m_items) {
            if(item->isEmpty()) continue;
            size.setWidth(size.width() + item->sizeHint().width() + (size.width() ? spacing() : 0));
            size.setHeight(qMax(size.height(), item->sizeHint().height()));
        }
        const auto m=contentsMargins();
        return size+QSize(m.left()+m.right(),m.top()+m.bottom());
    }
    void setGeometry(const QRect& rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }
private:
    int arrange(const QRect& rect, bool apply) const
    {
        const auto m=contentsMargins();
        const QRect available=rect.adjusted(m.left(),m.top(),-m.right(),-m.bottom());
        int x = available.x(), y = available.y(), rowHeight = 0;
        for(auto* item : m_items) {
            if(item->isEmpty()) continue;
            QSize size = item->sizeHint();
            // A large font can make a group wider than the viewport. Its own
            // flow layout then wraps buttons rather than clipping the group.
            size.setWidth(qMin(size.width(),qMax(0,available.width())));
            if(item->hasHeightForWidth())size.setHeight(item->heightForWidth(size.width()));
            if(x > available.x() && x + size.width() > available.x() + available.width()) {
                x = available.x(); y += rowHeight + spacing(); rowHeight = 0;
            }
            if(apply) item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing();
            rowHeight = qMax(rowHeight, size.height());
        }
        return y + rowHeight - rect.y() + m.bottom();
    }
    QList<QLayoutItem*> m_items;
};
