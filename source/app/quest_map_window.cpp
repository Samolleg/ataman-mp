// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ataman contributors
#include "main_window.h"
#include "quest_map_dialog.h"
#include "map_table_editor_dialog.h"
#include <QMessageBox>

void MainWindow::generateQuestMap()
{
    SQuestMapSource source;QString error;
    if(!CQuestMapSource::snapshot(m_pView,m_mapTestEntry,&source,&error)) {
        QMessageBox::information(this,tr("Quest Map Editor"),error);return;
    }
    auto* dialog=new CQuestMapDialog(source,this);
    const QString catalogPath=source.entry.sourceArchive;
    connect(dialog,&CQuestMapDialog::catalogRegistrationReady,this,[this,catalogPath](const QString& zone,const QString& texture){
        if(catalogPath.isEmpty())return;
        CMapTableEditorDialog catalog(this);
        if(catalog.openArchive(catalogPath)&&catalog.stageObjectivesTextureForZone(zone,texture)) {
            catalog.exec();processMapTableSaveReports(catalog.saveReports());
        }
    });
    dialog->show();
}
