// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kyr4l (quest-map algorithms and icon metadata)
// Copyright (C) 2026 Ataman contributors (Qt integration)
#pragma once
#include "quest_map.h"
#include <QImage>
#include <QStringList>
#include <QUndoStack>
#include <functional>

namespace questmap {
questpixels::Image pixels(const QImage& image);
QImage image(const questpixels::Image& pixels);
// Source bytes are supplied by the configured game resource loader. No game
// pixels or generated icon arrays belong in the executable/package.
using TextureLoader = std::function<QByteArray(const QString&)>;
QStringList loadRuntimeIcons(const TextureLoader& loader, QString* cacheDirectory = nullptr);
QImage cutIcon(const QImage& source, const QRect& rectangle);
QByteArray encode5551(const QImage& image, bool opaqueCopy = false);
QByteArray encodeDds(const QImage& image);

struct State { QImage paper; std::vector<PlacedIcon> stamps; };
class Document {
public:
    Document();
    const State& state() const { return m_state; }
    QUndoStack* history() { return &m_history; }
    const std::vector<NamedIcon>& icons() const { return m_icons; }
    QImage composed() const;
    void replace(const State& state, const QString& text);
    bool add(const QString& name, QPoint position, int scale);
    bool move(int index, QPoint position);
    bool scale(int index, int scale);
    bool remove(int index);
    void bake();
private:
    friend class StateCommand;
    State m_state;
    QUndoStack m_history;
    std::vector<NamedIcon> m_icons;
};
}
