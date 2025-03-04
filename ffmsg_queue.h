#ifndef FFMSG_QUEUE_H
#define FFMSG_QUEUE_H

#include "SDL.h"

typedef struct AVMessage{
    int what;       //消息类型
    int arg1;       //参数1
    int arg2;       //参数2
    void *obj;      //如果arg1 arg2不够存储消息则使用该参数
    void (*feel_l)(void *obj);  //obj对象是分配的，给出释放方式
    struct AVMessage *next; //下一个消息
} AVMessage;

typedef struct MessageQueue{    //消息队列
    AVMessage *first_msg,*last_msg;     //消息头部与尾部
    int nb_message;     //消息数量
    int abort_request;  //请求终止消息队列
    SDL_mutex *mutex;   //互斥量
    SDL_cond *cond;     //条件变量
    AVMessage *recycle_msg;  //消息循环使用
    int recycle_count;  //循环次数
    int alloc_count;    //分配次数
} MessageQueue;

//释放msg的obj资源
void msg_free_res(AVMessage *msg);
//私有插入消息
int msg_queue_put_private(MessageQueue *q,AVMessage *msg);
//插入消息
int msg_queue_put(MessageQueue *q,AVMessage *msg);
//初始化消息
void msg_init_msg(AVMessage *msg);

//插入简单消息
void msg_queue_put_simple(MessageQueue *q,int what);
void msg_queue_put_simple(MessageQueue *q,int what,int arg1);
void msg_queue_put_simple(MessageQueue *q,int what,int arg1,int arg2);
//插入消息，带obj
void msg_queue_put_simple(MessageQueue *q,int what,int arg1,int arg2,void *obj,int obj_len);

//释放msg的obj资源
void msg_obj_free_l(void *obj);
//消息队列初始化
void msg_queue_init(MessageQueue *q);
//消息队列flush，清空所有消息
void msg_queue_flush(MessageQueue *q);
//消息队列销毁
void msg_queue_destroy(MessageQueue *q);
//消息队列终止
void msg_queue_abort(MessageQueue *q);
//启用消息队列
void msg_queue_start(MessageQueue *q);
//读取消息
// return <0 if aborted, 0 if no msg, >0 if msg
int msg_queue_get(MessageQueue *q,AVMessage *msg,int block);
//消息删除 将队列中同一类型的消息全删除
void msg_queue_remove(MessageQueue *q,int what);












#endif // FFMSG_QUEUE_H
