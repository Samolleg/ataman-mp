#include <QCoreApplication>
#include "progressview.h"

CProgressView::CProgressView(QWidget* parent):
    QProgressBar(parent)
  , m_value(0.0)
{
    reset();
}

void CProgressView::reset()
{
    m_value = 0.0;
    setValue(m_value);
    setVisible(false);
}

void CProgressView::finish()
{
    m_value = 100.0;
    setValue(100);
    setVisible(false);
    QCoreApplication::processEvents();
}

void CProgressView::update(double val)
{
    m_value += val;
    if (!isVisible() && m_value > 0.0)
        setVisible(true);
    else if(isVisible() && m_value >= 100.0)
        setVisible(false);
    if(m_value - value() > 1)
    {
        QCoreApplication::processEvents();
    }
    setValue(m_value);
}
