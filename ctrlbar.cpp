#include "ctrlbar.h"
#include "ui_ctrlbar.h"

#include <QIcon>
#include <QDebug>

CtrlBar::CtrlBar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CtrlBar)
{
    ui->setupUi(this);

    //设置播放图标
    QIcon icon_play(":/ctrl/icon/play.png");
    ui->playOrpauseBtn->setIcon(icon_play);
    //设置停止图标
    QIcon icon_stop(":/ctrl/icon/stop.png");
    ui->stopBtn->setIcon(icon_stop);


}

CtrlBar::~CtrlBar()
{
    delete ui;
}

void CtrlBar::on_playOrpauseBtn_clicked()
{
    qDebug()<<"on_playOrpauseBtn_clicked";
    emit SigPlayOrPause();
}

void CtrlBar::on_stopBtn_clicked()
{
    qDebug()<<"on_stopBtn_clicked";
    emit SigStop();
}
