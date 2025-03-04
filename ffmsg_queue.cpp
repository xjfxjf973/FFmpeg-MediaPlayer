#include "ffmsg_queue.h"
#include "ffmsg.h"

extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}


void msg_free_res(AVMessage *msg)
{
    if(!msg || !msg->obj)
        return;
    msg->feel_l(msg->obj);
    msg->obj=NULL;
}

//消息队列内部重新去构建 AVMessage（重新申请AVMessage，或者来自于recycle_msg）
int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
    AVMessage *msg1;

    if(q->abort_request)
        return -1;

    //消息体使用回收资源还是重新malloc
    msg1=q->recycle_msg;
    if(msg1){
        q->recycle_msg=msg1->next;
        q->recycle_count++;
    } else{
        q->alloc_count++;
        msg1=(AVMessage *)av_malloc(sizeof(AVMessage));
    }

    *msg1= *msg;
    msg1->next=NULL;
    if(!q->first_msg){
        q->first_msg=msg1;
    } else{
        q->last_msg->next=msg1;
    }

    q->last_msg=msg1;
    q->nb_message++;
    SDL_CondSignal(q->cond);
    return 0;
}


int msg_queue_put(MessageQueue *q,AVMessage *msg)
{
    int ret;
    SDL_LockMutex(q->mutex);
    ret=msg_queue_put_private(q,msg);
    SDL_UnlockMutex(q->mutex);
    return ret;
}

//从头部first_msg取消息
// return <0 if aborted, 0 if no msg, >0 if msg
int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    AVMessage *msg1;
    int ret;

    SDL_LockMutex(q->mutex);

    for(;;){
        if(q->abort_request){
            ret=-1;
            break;
        }
        //获取消息
        msg1=q->first_msg;
        if(msg1){
            q->first_msg=msg1->next;
            if(!q->first_msg)
                q->last_msg=NULL;

            q->nb_message--;
            *msg=*msg1;
            msg1->obj=NULL;
            msg1->next=q->recycle_msg;
            q->recycle_msg=msg1;
            ret=1;
            break;
        } else if(!block){
            ret=0;
            break;
        } else{
            SDL_CondWait(q->cond,q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

//初始化消息
void msg_init_msg(AVMessage *msg)
{
    memset(msg,0,sizeof(AVMessage));
}

//插入简单消息
void msg_queue_put_simple(MessageQueue *q,int what){
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what=what;
    msg_queue_put(q,&msg);
}
void msg_queue_put_simple(MessageQueue *q,int what,int arg1){
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what=what;
    msg.arg1=arg1;
    msg_queue_put(q,&msg);
}
void msg_queue_put_simple(MessageQueue *q,int what,int arg1,int arg2){
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what=what;
    msg.arg1=arg1;
    msg.arg2=arg2;
    msg_queue_put(q,&msg);
}
//插入消息，带obj
void msg_queue_put_simple(MessageQueue *q,int what,int arg1,int arg2,void *obj,int obj_len){
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what=what;
    msg.arg1=arg1;
    msg.arg2=arg2;
    msg.obj=av_malloc(obj_len);
    memcpy(msg.obj,obj,obj_len);
    msg.feel_l=msg_obj_free_l;
    msg_queue_put(q,&msg);
}

//释放msg的obj资源
void msg_obj_free_l(void *obj){
    av_free(obj);
}

//消息队列初始化
void msg_queue_init(MessageQueue *q){
    memset(q,0,sizeof(MessageQueue));
    q->mutex=SDL_CreateMutex();
    q->cond=SDL_CreateCond();
    q->abort_request=1;
}

//消息队列flush，清空所有消息
void msg_queue_flush(MessageQueue *q){
    AVMessage *msg,*msg1;

    SDL_LockMutex(q->mutex);
    for(msg=q->first_msg; msg; msg=msg1){
        msg1=msg->next;
        msg->next=q->recycle_msg;
        q->recycle_msg=msg;
    }
    q->first_msg=NULL;
    q->last_msg=NULL;
    q->nb_message=0;
    SDL_UnlockMutex(q->mutex);
}

//消息队列销毁
void msg_queue_destroy(MessageQueue *q){
    msg_queue_flush(q);

    SDL_LockMutex(q->mutex);
    AVMessage *msg;
    while(q->recycle_msg){
        msg=q->recycle_msg;
        if(msg)
            q->recycle_msg=msg->next;
        msg_free_res(msg);
        av_freep(&msg);
    }
    SDL_UnlockMutex(q->mutex);

    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

//消息队列终止
void msg_queue_abort(MessageQueue *q){
    SDL_LockMutex(q->mutex);
    q->abort_request=1;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}

//启用消息队列
void msg_queue_start(MessageQueue *q){
    SDL_LockMutex(q->mutex);
    q->abort_request=0;

    //插入消息
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what=FFP_MSG_FLUSH;
    msg_queue_put_private(q, &msg);
    SDL_UnlockMutex(q->mutex);
}

//消息删除 将队列中同一类型的消息全删除
void msg_queue_remove(MessageQueue *q, int what) {
    AVMessage **p_msg, *last_msg, *msg;

    // 锁定互斥锁，确保对队列的访问是线程安全的
    SDL_LockMutex(q->mutex);
    last_msg = q->first_msg;

    // 如果没有中止请求且队列不为空
    if (!q->abort_request && q->first_msg) {
        p_msg = &q->first_msg;
        while (*p_msg) {
            msg = *p_msg;
            // 如果消息类型匹配
            if (msg->what == what) {
                *p_msg = msg->next; // 从链表中移除该消息
                msg_free_res(msg); // 释放消息资源
                msg->next = q->recycle_msg; // 将消息体加入回收链表
                q->recycle_msg = msg;
                q->recycle_count--;
            } else {
                last_msg = msg;
                p_msg = &msg->next; // 移动到下一个消息
            }
        }

        // 更新队列的最后一个消息指针
        if (q->first_msg) {
            q->last_msg = last_msg;
        } else {
            q->last_msg = NULL;
        }
    }

    // 解锁互斥锁，允许其他线程访问队列
    SDL_UnlockMutex(q->mutex);
}


