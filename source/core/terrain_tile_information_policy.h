#pragma once

#include <QPoint>

enum class ETerrainTileInformationAction
{
    None,
    Show,
    Hide
};

class CTerrainTileInformationPolicy
{
public:
    ETerrainTileInformationAction rightPress(const QPoint& cell)
    {
        m_rightHeld = true;
        m_visible = true;
        m_cell = cell;
        return ETerrainTileInformationAction::Show;
    }

    ETerrainTileInformationAction mouseMove(const QPoint& cell,
                                             bool rightButtonDown)
    {
        if(!m_rightHeld)
            return ETerrainTileInformationAction::None;
        if(!rightButtonDown)
        {
            m_rightHeld = false;
            return ETerrainTileInformationAction::None;
        }
        if(m_visible && m_cell == cell)
            return ETerrainTileInformationAction::None;
        m_visible = true;
        m_cell = cell;
        return ETerrainTileInformationAction::Show;
    }

    ETerrainTileInformationAction rightRelease()
    {
        m_rightHeld = false;
        return ETerrainTileInformationAction::None;
    }

    ETerrainTileInformationAction cancel()
    {
        m_rightHeld = false;
        if(!m_visible)
            return ETerrainTileInformationAction::None;
        m_visible = false;
        return ETerrainTileInformationAction::Hide;
    }

    bool visible() const { return m_visible; }
    bool rightHeld() const { return m_rightHeld; }
    QPoint cell() const { return m_cell; }

private:
    bool m_visible{false};
    bool m_rightHeld{false};
    QPoint m_cell;
};
