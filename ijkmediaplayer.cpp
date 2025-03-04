#include "ijkmediaplayer.h"
#include <iostream>

IjkMediaPlayer::IjkMediaPlayer()
{

}

IjkMediaPlayer::~IjkMediaPlayer()
{
    std::cout<< "~IjkMediaPlayer()"<<std::endl;
}

int IjkMediaPlayer::ijkmp_create(std::function<int (void *)> msg_loop)
{
    int ret=0;
    ffplayer_= new FFPlayer();
    if(!ffplayer_){
        std::cout<<"new FFPlayer() failed\n";
        return -1;
    }

    msg_loop_=msg_loop;

    ret=ffplayer_->ffp_create();
    if(ret<0){
        return -1;
    }
    return 0;
}

int IjkMediaPlayer::ijkmp_destroy()
{
    ffplayer_->ffp_destroy();
    return 0;
}

//设置要播放的url 方法设计来源于Android mediaplayer
int IjkMediaPlayer::ijkmp_set_data_source(const char *url)
{
    if(!url){
        std::cout<<"url为空";
        return -1;
    }
    data_source_=SDL_strdup(url);   //分配内存，拷贝字符串
    return 0;
}

//准备播放
int IjkMediaPlayer::ijkmp_prepare_async()
{
    //判断mediaplayer的状态
    //正在准备中
    mp_state_=MP_STATE_ASYNC_PREPARING;

    //启用消息队列
    msg_queue_start(&ffplayer_->msg_queue_);
    //创建循环线程
    msg_thread_=new std::thread(&IjkMediaPlayer::ijkmp_msg_loop,this,this);
    //调用ffplayer
    int ret=ffplayer_->ffp_prepare_async_l(data_source_);
    if(ret<0){
        mp_state_= MP_STATE_ERROR;
        return -1;
    }
    return 0;
}

//触发播放
int IjkMediaPlayer::ijkmp_start()
{
    ffp_notify_msg(ffplayer_,FFP_REQ_START);
}

//停止
int IjkMediaPlayer::ijkmp_stop()
{
    int retval=ffplayer_->ffp_stop_l();
    if(retval<0){
        return retval;
    }
}

//读取消息
int IjkMediaPlayer::ijkmp_get_msg(AVMessage *msg, int block)
{
    while(1){
        int continue_wait_next_msg=0;
        //取消息，如果没有消息阻塞
        int retval=msg_queue_get(&ffplayer_->msg_queue_, msg, block);
        if(retval<=0)
            return retval;
        switch (msg->what) {
        case FFP_MSG_PREPARED:
            std::cout<<__FUNCTION__<<" FFP_MSG_PREPARED"<<std::endl;
            break;
        case FFP_REQ_START:
            std::cout<<__FUNCTION__<<" FFP_REQ_START"<<std::endl;
            continue_wait_next_msg=1;
            //ffplayer_->start();
            break;
        default:
            std::cout<<__FUNCTION__<<" default "<<msg->what<<std::endl;
            break;
        }
        if(continue_wait_next_msg){
            msg_free_res(msg);
            continue;
        }
        return retval;
    }
    return -1;
}

int IjkMediaPlayer::ijkmp_msg_loop(void *arg)
{
    msg_loop_(arg);
    return 0;
}

















