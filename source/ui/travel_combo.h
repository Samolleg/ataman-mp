#pragma once
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QLineEdit>

// Non-menu popup respects maxVisibleItems even under the platform menu style.
inline void configureTravelCombo(QComboBox* combo)
{
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setMaxVisibleItems(10);
    combo->setStyleSheet("QComboBox { combobox-popup: 0; }");
    combo->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(12);
    combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    combo->completer()->setFilterMode(Qt::MatchContains);
    combo->completer()->setMaxVisibleItems(10);
    QObject::connect(combo,QOverload<int>::of(&QComboBox::currentIndexChanged),combo,[combo](int){
        if(combo->lineEdit())combo->lineEdit()->setCursorPosition(0);
    });
}
