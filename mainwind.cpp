#include "mainwind.h"
#include "ui_mainwind.h"

#include "ffmsg.h"
#include <thread>
#include <functional>
#include <iostream>
#include <QDebug>

MainWind::MainWind(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWind)
{
    ui->setupUi(this);

    InitSignalsAndSlots();
}

MainWind::~MainWind()
{
    delete ui;
}

int MainWind::InitSignalsAndSlots()
{
    connect(ui->ctrlBarWind,&CtrlBar::SigPlayOrPause,this,&MainWind::OnPlayOrPause);
    connect(ui->ctrlBarWind,&CtrlBar::SigStop,this,&MainWind::OnStop);
}

int MainWind::message_loop(void *arg)
{
    IjkMediaPlayer *mp=(IjkMediaPlayer *)arg;
    //线程循环
    qDebug()<<"message_loop into\n";
    while (1) {
        AVMessage msg;
        //取消息队列的消息，如果没有消息就阻塞，直到有消息被发到消息队列为止
        int retval=mp->ijkmp_get_msg(&msg,1);   //主要处理java->C的消息

        if(retval<0)
            break;

        switch (msg.what) {
        case FFP_MSG_FLUSH:
            qDebug()<< __FUNCTION__ <<" FFP_MSG_FULSH";
            break;
        case FFP_MSG_PREPARED:
            std::cout<<__FUNCTION__ <<" FFP_MSG_PREPARED"<<std::endl;
            mp->ijkmp_start();
            break;
        default:
            qDebug()<< __FUNCTION__ <<" default"<<msg.what;
            break;
        }
        msg_free_res(&msg);
        //先模拟线程运行
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }
    qDebug()<<"message_loop leave";

}

int MainWind::OutputVideo(const Frame *frame)
{
    return ui->showWind->Draw(frame);
}

void MainWind::OnPlayOrPause()
{
    qDebug()<<"OnPlayOrPause call";
    int ret=0;
    //先检测mp是否已经创建
    if(!mp_){
        mp_=new IjkMediaPlayer();
        //创建
        ret=mp_->ijkmp_create(std::bind(&MainWind::message_loop,this,std::placeholders::_1));
        if(ret<0){
            qDebug()<<"IjkMediaPlayer create failed\n";
            delete mp_;
            mp_=NULL;
            return;
        }

        mp_->AddVideoRefreshCallback(std::bind(&MainWind::OutputVideo,this,
                                               std::placeholders::_1));

        //设置url
        mp_->ijkmp_set_data_source("test.mp4");
        //准备工作
        ret=mp_->ijkmp_prepare_async();
        if(ret<0){
            qDebug()<<"IjkMediaPlayer create failed\n";
            delete mp_;
            mp_=NULL;
            return;
        }
    } else{
        //已经准备好了，则暂停或者恢复播放
    }

}

void MainWind::OnStop()
{
    qDebug()<<"OnStop call";
    if(mp_){
        mp_->ijkmp_stop();
        mp_->ijkmp_destroy();
        delete mp_;
        mp_=NULL;
    }
}
 






















