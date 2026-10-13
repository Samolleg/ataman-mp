#ifndef TERRAIN_HD_PROGRESS_DIALOG_H
#define TERRAIN_HD_PROGRESS_DIALOG_H

#include "terrain_hd_progress.h"
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

class CTerrainHdProgressDialog : public QDialog
{
public:
    explicit CTerrainHdProgressDialog(CTerrainHdProgress& progress,QWidget* parent=nullptr,bool variants=false)
        : QDialog(parent),m_progress(progress),m_variants(variants) {
        setObjectName(QStringLiteral("terrainHdProgressDialog"));
        setWindowTitle(QStringLiteral("Пересборка атласа карты в HD"));
        setWindowModality(Qt::ApplicationModal);setMinimumWidth(540);
        auto layout=new QVBoxLayout(this);
        m_label=new QLabel(this);m_label->setObjectName(QStringLiteral("hdProgressLabel"));
        m_label->setMinimumHeight(80);layout->addWidget(m_label);
        m_bar=new QProgressBar(this);m_bar->setRange(0,100);layout->addWidget(m_bar);
        m_cancel=new QPushButton(QStringLiteral("Отмена"),this);m_cancel->setObjectName(QStringLiteral("hdProgressCancel"));
        layout->addWidget(m_cancel,0,Qt::AlignRight);
        connect(m_cancel,&QPushButton::clicked,this,[this](){reject();});
        connect(&m_timer,&QTimer::timeout,this,[this](){refresh();});m_timer.start(100);refresh();
    }
    void reject() override {
        if(m_committing)return;
        m_progress.cancel();m_cancel->setEnabled(false);refresh();
    }
    void lockForCommit() {m_committing=true;m_cancel->setEnabled(false);m_cancel->setText(QStringLiteral("Завершение сохранения…"));}
    void refresh() {
        const auto s=m_progress.snapshot();m_bar->setRange(0,!s.operation.isEmpty()&&s.total<=0?0:100);m_bar->setValue(s.percent);
        m_label->setText(m_progress.cancelled()?QStringLiteral("Отмена подготовки…\nОжидание завершения текущей операции.\nRES и MPR не изменяются."):CTerrainHdProgress::label(s));
        if(m_variants)m_label->setText(m_label->text().replace(QStringLiteral("Восстановление переходов"),QStringLiteral("Генерация материалов")));
    }
private:
    CTerrainHdProgress& m_progress;
    bool m_variants;
    bool m_committing{false};
    QLabel* m_label;
    QProgressBar* m_bar;
    QPushButton* m_cancel;
    QTimer m_timer;
};
#endif
