// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ataman contributors
#pragma once
#include <QDialog>
#include "quest_map_document.h"
#include "map_catalog.h"
class CView;
class QListWidget;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTimer;
class CQuestMapCanvas;

struct SQuestMapSource {
    questmap::Terrain terrain;
    std::vector<questmap::Marker> markers;
    SMapCatalogEntry entry;
    QImage original;
    quint32 originalFormat{0x5551};
    QStringList notes;
    std::vector<questmap::NamedIcon> icons;
};
class CQuestMapSource {
public:
    static bool snapshot(CView* view,const SMapCatalogEntry& entry,SQuestMapSource* source,QString* error);
};
class CQuestMapDialog:public QDialog {
    Q_OBJECT
public:
    explicit CQuestMapDialog(const SQuestMapSource& source,QWidget* parent=nullptr);
    questmap::Document& document(){return m_document;}
    questmap::Options options() const;
    bool regenerate();
    // Same transaction for the real UI and isolated native verification.
    bool exportArchive(const QString& path,bool replacementConfirmed,QString* result);
signals:
    void catalogRegistrationReady(const QString& zoneId,const QString& textureName);
private:
    void updatePreview();
    void exportToMod();
    void saveImage();
    SQuestMapSource m_source;
    questmap::Document m_document;
    CQuestMapCanvas* m_canvas;
    QListWidget* m_palette;
    QSpinBox *m_size,*m_seed,*m_roughness,*m_iconScale,*m_stampScale;
    QComboBox* m_frame;
    QComboBox* m_format;
    QCheckBox *m_relief,*m_water,*m_forests,*m_icons,*m_bridges,*m_exits,*m_sprites;
    QLabel* m_status;
    QLineEdit* m_textureName;
    QTimer* m_regenerateTimer;
};
