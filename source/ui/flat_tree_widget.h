#pragma once

#include <QTreeWidget>
#include <QStyledItemDelegate>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>

// Keep the actual tree model (selection, filters, keyboard navigation and
// expansion), but give every depth the same small disclosure-button gutter.
class CFlatTreeWidget : public QTreeWidget
{
public:
    explicit CFlatTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent)
    {
        setIndentation(0);
        setItemDelegate(new Delegate(this));
    }

    int disclosureWidth() const { return qMax(16, fontMetrics().height()); }

protected:
    void drawBranches(QPainter*, const QRect&, const QModelIndex&) const override {}

    void mousePressEvent(QMouseEvent* event) override
    {
        const QModelIndex index = indexAt(event->pos());
        m_disclosurePressed = event->button() == Qt::LeftButton
            && inDisclosure(index, event->pos()) && model()->hasChildren(index);
        if(m_disclosurePressed)
        {
            setExpanded(index, !isExpanded(index));
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if(m_disclosurePressed && event->button() == Qt::LeftButton)
        {
            m_disclosurePressed = false;
            event->accept();
            return;
        }
        QTreeWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const QModelIndex index = indexAt(event->pos());
        if(event->button() == Qt::LeftButton && inDisclosure(index, event->pos())
           && model()->hasChildren(index))
        {
            event->accept();
            return;
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }

private:
    QRect gutter(QRect rect) const
    {
        if(layoutDirection() == Qt::RightToLeft)
            rect.setLeft(rect.right() - disclosureWidth() + 1);
        else
            rect.setWidth(disclosureWidth());
        return rect;
    }

    bool inDisclosure(const QModelIndex& index, const QPoint& point) const
    {
        return index.isValid() && index.column() == 0
            && gutter(visualRect(index)).contains(point);
    }

    class Delegate : public QStyledItemDelegate
    {
    public:
        explicit Delegate(CFlatTreeWidget* tree) : QStyledItemDelegate(tree), m_tree(tree) {}

        QStyleOptionViewItem contentOption(QStyleOptionViewItem option,
                                           const QModelIndex& index) const
        {
            if(index.column() == 0)
            {
                if(option.direction == Qt::RightToLeft)
                    option.rect.adjust(0, 0, -m_tree->disclosureWidth(), 0);
                else
                    option.rect.adjust(m_tree->disclosureWidth(), 0, 0, 0);
            }
            return option;
        }

        void paint(QPainter* painter, const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
        {
            QStyledItemDelegate::paint(painter, contentOption(option, index), index);
            if(index.column() != 0 || !index.model()->hasChildren(index)) return;
            QStyleOption branch;
            branch.rect = m_tree->gutter(option.rect);
            branch.palette = option.palette;
            branch.direction = option.direction;
            branch.state = option.state | QStyle::State_Children | QStyle::State_Item;
            if(m_tree->isExpanded(index)) branch.state |= QStyle::State_Open;
            else branch.state &= ~QStyle::State_Open;
            m_tree->style()->drawPrimitive(QStyle::PE_IndicatorBranch, &branch,
                                           painter, m_tree);
        }

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QSize size = QStyledItemDelegate::sizeHint(option, index);
            if(index.column() == 0) size.rwidth() += m_tree->disclosureWidth();
            return size;
        }

        bool editorEvent(QEvent* event, QAbstractItemModel* model,
                         const QStyleOptionViewItem& option, const QModelIndex& index) override
        {
            return QStyledItemDelegate::editorEvent(event, model, contentOption(option, index), index);
        }

        void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const override
        {
            QStyledItemDelegate::updateEditorGeometry(editor, contentOption(option, index), index);
        }

    private:
        CFlatTreeWidget* m_tree;
    };

    bool m_disclosurePressed{false};
};
