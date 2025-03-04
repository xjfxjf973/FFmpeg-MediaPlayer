#include "ff_ffplay_def.h"

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

//#include <stdatomic.h>

#define FFP_IO_STAT_STEP (50 * 1024)

#define FFP_BUF_MSG_PERIOD (3)

static AVPacket flush_pkt;


#define IJKVERSION_GET_MAJOR(x)     ((x >> 16) & 0xFF)
#define IJKVERSION_GET_MINOR(x)     ((x >>  8) & 0xFF)
#define IJKVERSION_GET_MICRO(x)     ((x      ) & 0xFF)


static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)
       return -1;

    pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));

    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);

    q->duration += FFMAX(pkt1->pkt.duration, MIN_PKT_DURATION);

    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);
    return 0;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    SDL_UnlockMutex(q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

/* packet queue handling */
int packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}

void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);      //先清除所有节点
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;       //请求退出

    SDL_CondSignal(q->cond);    //释放条件信号

    SDL_UnlockMutex(q->mutex);
}

//数据包队列开始使用
void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);//flush_pkt
    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
//从数据包队列中获取数据包
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            q->duration -= FFMAX(pkt1->pkt.duration, MIN_PKT_DURATION);
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {    //队列中没有数据，且非阻塞调用
            ret = 0;
            break;
        } else {                //队列中有数据，且阻塞调用
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);  //释放帧
    return ret;
}

//static int packet_queue_get_or_buffering(FFPlayer *ffp, PacketQueue *q, AVPacket *pkt, int *serial, int *finished)
//{
//    assert(finished);
//    if (!ffp->packet_buffering)
//        return packet_queue_get(q, pkt, 1, serial);

//    while (1) {
//        int new_packet = packet_queue_get(q, pkt, 0, serial);
//        if (new_packet < 0)
//            return -1;
//        else if (new_packet == 0) {
//            if (q->is_buffer_indicator && !*finished)
//                ffp_toggle_buffering(ffp, 1);
//            new_packet = packet_queue_get(q, pkt, 1, serial);
//            if (new_packet < 0)
//                return -1;
//        }

//        if (*finished == *serial) {
//            av_packet_unref(pkt);
//            continue;
//        }
//        else
//            break;
//    }

//    return 1;
//}


//static int convert_image(FFPlayer *ffp, AVFrame *src_frame, int64_t src_frame_pts, int width, int height) {
//    GetImgInfo *img_info = ffp->get_img_info;
//    VideoState *is = ffp->is;
//    AVFrame *dst_frame = NULL;
//    AVPacket avpkt;
//    int got_packet = 0;
//    int dst_width = 0;
//    int dst_height = 0;
//    int bytes = 0;
//    void *buffer = NULL;
//    char file_path[1024] = {0};
//    char file_name[16] = {0};
//    int fd = -1;
//    int ret = 0;
//    int tmp = 0;
//    float origin_dar = 0;
//    float dar = 0;
//    AVRational display_aspect_ratio;
//    int file_name_length = 0;

//    if (!height || !width || !img_info->width || !img_info->height) {
//        ret = -1;
//        return ret;
//    }

//    dar = (float) img_info->width / img_info->height;

//    if (is->viddec.avctx) {
//        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
//            is->viddec.avctx->width * (int64_t)is->viddec.avctx->sample_aspect_ratio.num,
//            is->viddec.avctx->height * (int64_t)is->viddec.avctx->sample_aspect_ratio.den,
//            1024 * 1024);

//        if (!display_aspect_ratio.num || !display_aspect_ratio.den) {
//            origin_dar = (float) width / height;
//        } else {
//            origin_dar = (float) display_aspect_ratio.num / display_aspect_ratio.den;
//        }
//    } else {
//        ret = -1;
//        return ret;
//    }

//    if ((int)(origin_dar * 100) != (int)(dar * 100)) {
//        tmp = img_info->width / origin_dar;
//        if (tmp > img_info->height) {
//            img_info->width = img_info->height * origin_dar;
//        } else {
//            img_info->height = tmp;
//        }
//        av_log(NULL, AV_LOG_INFO, "%s img_info->width = %d, img_info->height = %d\n", __func__, img_info->width, img_info->height);
//    }

//    dst_width = img_info->width;
//    dst_height = img_info->height;

//    av_init_packet(&avpkt);
//    avpkt.size = 0;
//    avpkt.data = NULL;

//    if (!img_info->frame_img_convert_ctx) {
//        img_info->frame_img_convert_ctx = sws_getContext(width,
//            height,
//            src_frame->format,
//            dst_width,
//            dst_height,
//            AV_PIX_FMT_RGB24,
//            SWS_BICUBIC,
//            NULL,
//            NULL,
//            NULL);

//        if (!img_info->frame_img_convert_ctx) {
//            ret = -1;
//            av_log(NULL, AV_LOG_ERROR, "%s sws_getContext failed\n", __func__);
//            goto fail0;
//        }
//    }

//    if (!img_info->frame_img_codec_ctx) {
//        AVCodec *image_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
//        if (!image_codec) {
//            ret = -1;
//            av_log(NULL, AV_LOG_ERROR, "%s avcodec_find_encoder failed\n", __func__);
//            goto fail0;
//        }
//        img_info->frame_img_codec_ctx = avcodec_alloc_context3(image_codec);
//        if (!img_info->frame_img_codec_ctx) {
//            ret = -1;
//            av_log(NULL, AV_LOG_ERROR, "%s avcodec_alloc_context3 failed\n", __func__);
//            goto fail0;
//        }
//        img_info->frame_img_codec_ctx->bit_rate = ffp->stat.bit_rate;
//        img_info->frame_img_codec_ctx->width = dst_width;
//        img_info->frame_img_codec_ctx->height = dst_height;
//        img_info->frame_img_codec_ctx->pix_fmt = AV_PIX_FMT_RGB24;
//        img_info->frame_img_codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
//        img_info->frame_img_codec_ctx->time_base.num = ffp->is->video_st->time_base.num;
//        img_info->frame_img_codec_ctx->time_base.den = ffp->is->video_st->time_base.den;
//        avcodec_open2(img_info->frame_img_codec_ctx, image_codec, NULL);
//    }

//    dst_frame = av_frame_alloc();
//    if (!dst_frame) {
//        ret = -1;
//        av_log(NULL, AV_LOG_ERROR, "%s av_frame_alloc failed\n", __func__);
//        goto fail0;
//    }
//    bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, dst_width, dst_height, 1);
//    buffer = (uint8_t *) av_malloc(bytes * sizeof(uint8_t));
//    if (!buffer) {
//        ret = -1;
//        av_log(NULL, AV_LOG_ERROR, "%s av_image_get_buffer_size failed\n", __func__);
//        goto fail1;
//    }

//    dst_frame->format = AV_PIX_FMT_RGB24;
//    dst_frame->width = dst_width;
//    dst_frame->height = dst_height;

//    ret = av_image_fill_arrays(dst_frame->data,
//            dst_frame->linesize,
//            buffer,
//            AV_PIX_FMT_RGB24,
//            dst_width,
//            dst_height,
//            1);

//    if (ret < 0) {
//        ret = -1;
//        av_log(NULL, AV_LOG_ERROR, "%s av_image_fill_arrays failed\n", __func__);
//        goto fail2;
//    }

//    ret = sws_scale(img_info->frame_img_convert_ctx,
//            (const uint8_t * const *) src_frame->data,
//            src_frame->linesize,
//            0,
//            src_frame->height,
//            dst_frame->data,
//            dst_frame->linesize);

//    if (ret <= 0) {
//        ret = -1;
//        av_log(NULL, AV_LOG_ERROR, "%s sws_scale failed\n", __func__);
//        goto fail2;
//    }

//    ret = avcodec_encode_video2(img_info->frame_img_codec_ctx, &avpkt, dst_frame, &got_packet);

//    if (ret >= 0 && got_packet > 0) {
//        strcpy(file_path, img_info->img_path);
//        strcat(file_path, "/");
//        sprintf(file_name, "%lld", src_frame_pts);
//        strcat(file_name, ".png");
//        strcat(file_path, file_name);

//        fd = open(file_path, O_RDWR | O_TRUNC | O_CREAT, 0600);
//        if (fd < 0) {
//            ret = -1;
//            av_log(NULL, AV_LOG_ERROR, "%s open path = %s failed %s\n", __func__, file_path, strerror(errno));
//            goto fail2;
//        }
//        write(fd, avpkt.data, avpkt.size);
//        close(fd);

//        img_info->count--;

//        file_name_length = (int)strlen(file_name) + 1;

//        if (img_info->count <= 0)
//            ffp_notify_msg4(ffp, FFP_MSG_GET_IMG_STATE, (int) src_frame_pts, 1, file_name, file_name_length);
//        else
//            ffp_notify_msg4(ffp, FFP_MSG_GET_IMG_STATE, (int) src_frame_pts, 0, file_name, file_name_length);

//        ret = 0;
//    }

//fail2:
//    av_free(buffer);
//fail1:
//    av_frame_free(&dst_frame);
//fail0:
//    av_packet_unref(&avpkt);

//    return ret;
//}

static void frame_queue_unref_item(Frame *vp)
{
    av_frame_unref(vp->frame);  //释放数据`
}

//帧队列frame_queue初始化（绑定数据包队列，初始化最大值）
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size/*, int keep_last */)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    //f->keep_last = !!keep_last;
    //为队列中所有的缓存帧预先申请内存
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

//帧队列销毁
void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
//        free_picture(vp);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

//帧队列信号
void frame_queue_signal(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex ) % f->max_size];
}

Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + 1) % f->max_size];
}

//获取last_frame
Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

//获取可写指针
Frame *frame_queue_peek_writable(FrameQueue *f)
{
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {   //检查是否需要退出
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)         //检查是不是要退出
        return NULL;

    return &f->queue[f->windex];
}

//获取可读
Frame *frame_queue_peek_readable(FrameQueue *f)
{
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size <= 0 &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex) % f->max_size];
}

void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

//释放当前frame，并更新读索引rindex
void frame_queue_next(FrameQueue *f)
{
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f)
{
    return f->size;
}

/* return last shown position */
#ifdef FFP_MERGE
static int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}
#endif


// FFP_MERGE: fill_rectangle
// FFP_MERGE: fill_border
// FFP_MERGE: ALPHA_BLEND
// FFP_MERGE: RGBA_IN
// FFP_MERGE: YUVA_IN
// FFP_MERGE: YUVA_OUT
// FFP_MERGE: BPP
// FFP_MERGE: blend_subrect


// FFP_MERGE: realloc_texture
// FFP_MERGE: calculate_display_rect
// FFP_MERGE: upload_texture
// FFP_MERGE: video_image_display

static size_t parse_ass_subtitle(const char *ass, char *output)
{
    char *tok = NULL;
    tok = strchr(ass, ':'); if (tok) tok += 1; // skip event
    tok = strchr(tok, ','); if (tok) tok += 1; // skip layer
    tok = strchr(tok, ','); if (tok) tok += 1; // skip start_time
    tok = strchr(tok, ','); if (tok) tok += 1; // skip end_time
    tok = strchr(tok, ','); if (tok) tok += 1; // skip style
    tok = strchr(tok, ','); if (tok) tok += 1; // skip name
    tok = strchr(tok, ','); if (tok) tok += 1; // skip margin_l
    tok = strchr(tok, ','); if (tok) tok += 1; // skip margin_r
    tok = strchr(tok, ','); if (tok) tok += 1; // skip margin_v
    tok = strchr(tok, ','); if (tok) tok += 1; // skip effect
    if (tok) {
        char *text = tok;
        size_t idx = 0;
        do {
            char *found = strstr(text, "\\N");
            if (found) {
                size_t n = found - text;
                memcpy(output+idx, text, n);
                output[idx + n] = '\n';
                idx = n + 1;
                text = found + 2;
            }
            else {
                size_t left_text_len = strlen(text);
                memcpy(output+idx, text, left_text_len);
                if (output[idx + left_text_len - 1] == '\n')
                    output[idx + left_text_len - 1] = '\0';
                else
                    output[idx + left_text_len] = '\0';
                break;
            }
        } while(1);
        return strlen(output) + 1;
    }
    return 0;
}


//static void video_image_display2(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    Frame *vp;
//    Frame *sp = NULL;

//    vp = frame_queue_peek_last(&is->pictq);

//    if (vp->bmp) {
//        if (is->subtitle_st) {
//            if (frame_queue_nb_remaining(&is->subpq) > 0) {
//                sp = frame_queue_peek(&is->subpq);

//                if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
//                    if (!sp->uploaded) {
//                        if (sp->sub.num_rects > 0) {
//                            char buffered_text[4096];
//                            if (sp->sub.rects[0]->text) {
//                                strncpy(buffered_text, sp->sub.rects[0]->text, 4096);
//                            }
//                            else if (sp->sub.rects[0]->ass) {
//                                parse_ass_subtitle(sp->sub.rects[0]->ass, buffered_text);
//                            }
//                            ffp_notify_msg4(ffp, FFP_MSG_TIMED_TEXT, 0, 0, buffered_text, sizeof(buffered_text));
//                        }
//                        sp->uploaded = 1;
//                    }
//                }
//            }
//        }
//        if (ffp->render_wait_start && !ffp->start_on_prepared && is->pause_req) {
//            if (!ffp->first_video_frame_rendered) {
//                ffp->first_video_frame_rendered = 1;
//                ffp_notify_msg1(ffp, FFP_MSG_VIDEO_RENDERING_START);
//            }
//            while (is->pause_req && !is->abort_request) {
//                SDL_Delay(20);
//            }
//        }
//        SDL_VoutDisplayYUVOverlay(ffp->vout, vp->bmp);
//        ffp->stat.vfps = SDL_SpeedSamplerAdd(&ffp->vfps_sampler, FFP_SHOW_VFPS_FFPLAY, "vfps[ffplay]");
//        if (!ffp->first_video_frame_rendered) {
//            ffp->first_video_frame_rendered = 1;
//            ffp_notify_msg1(ffp, FFP_MSG_VIDEO_RENDERING_START);
//        }

//        if (is->latest_video_seek_load_serial == vp->serial) {
//            int latest_video_seek_load_serial = __atomic_exchange_n(&(is->latest_video_seek_load_serial), -1, memory_order_seq_cst);
//            if (latest_video_seek_load_serial == vp->serial) {
//                ffp->stat.latest_seek_load_duration = (av_gettime() - is->latest_seek_load_start_at) / 1000;
//                if (ffp->av_sync_type == AV_SYNC_VIDEO_MASTER) {
//                    ffp_notify_msg2(ffp, FFP_MSG_VIDEO_SEEK_RENDERING_START, 1);
//                } else {
//                    ffp_notify_msg2(ffp, FFP_MSG_VIDEO_SEEK_RENDERING_START, 0);
//                }
//            }
//        }
//    }
//}


// FFP_MERGE: do_exit
// FFP_MERGE: sigterm_handler
// FFP_MERGE: video_open
// FFP_MERGE: video_display

///* display the current picture, if any */
//static void video_display2(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    if (is->video_st)
//        video_image_display2(ffp);
//}

//static double get_clock(Clock *c)
//{
//    if (*c->queue_serial != c->serial)
//        return NAN;
//    if (c->paused) {
//        return c->pts;
//    } else {
//        double time = av_gettime_relative() / 1000000.0;
//        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
//    }
//}

//static void set_clock_at(Clock *c, double pts, int serial, double time)
//{
//    c->pts = pts;
//    c->last_updated = time;
//    c->pts_drift = c->pts - time;
//    c->serial = serial;
//}

//static void set_clock(Clock *c, double pts, int serial)
//{
//    double time = av_gettime_relative() / 1000000.0;
//    set_clock_at(c, pts, serial, time);
//}

//static void set_clock_speed(Clock *c, double speed)
//{
//    set_clock(c, get_clock(c), c->serial);
//    c->speed = speed;
//}

//static void init_clock(Clock *c, int *queue_serial)
//{
//    c->speed = 1.0;
//    c->paused = 0;
//    c->queue_serial = queue_serial;
//    set_clock(c, NAN, -1);
//}

//static void sync_clock_to_slave(Clock *c, Clock *slave)
//{
//    double clock = get_clock(c);
//    double slave_clock = get_clock(slave);
//    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
//        set_clock(c, slave_clock, slave->serial);
//}

//static int get_master_sync_type(VideoState *is) {
//    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
//        if (is->video_st)
//            return AV_SYNC_VIDEO_MASTER;
//        else
//            return AV_SYNC_AUDIO_MASTER;
//    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
//        if (is->audio_st)
//            return AV_SYNC_AUDIO_MASTER;
//        else
//            return AV_SYNC_EXTERNAL_CLOCK;
//    } else {
//        return AV_SYNC_EXTERNAL_CLOCK;
//    }
//}

///* get the current master clock value */
//static double get_master_clock(VideoState *is)
//{
//    double val;

//    switch (get_master_sync_type(is)) {
//        case AV_SYNC_VIDEO_MASTER:
//            val = get_clock(&is->vidclk);
//            break;
//        case AV_SYNC_AUDIO_MASTER:
//            val = get_clock(&is->audclk);
//            break;
//        default:
//            val = get_clock(&is->extclk);
//            break;
//    }
//    return val;
//}

//static void check_external_clock_speed(VideoState *is) {
//   if ((is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
//       (is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)) {
//       set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
//   } else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
//              (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
//       set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
//   } else {
//       double speed = is->extclk.speed;
//       if (speed != 1.0)
//           set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
//   }
//}

///* seek in the stream */
//static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
//{
//    if (!is->seek_req) {
//        is->seek_pos = pos;
//        is->seek_rel = rel;
//        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
//        if (seek_by_bytes)
//            is->seek_flags |= AVSEEK_FLAG_BYTE;
//        is->seek_req = 1;
//        SDL_CondSignal(is->continue_read_thread);
//    }
//}

///* pause or resume the video */
//static void stream_toggle_pause_l(FFPlayer *ffp, int pause_on)
//{
//    VideoState *is = ffp->is;
//    if (is->paused && !pause_on) {
//        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;

//#ifdef FFP_MERGE
//        if (is->read_pause_return != AVERROR(ENOSYS)) {
//            is->vidclk.paused = 0;
//        }
//#endif
//        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
//        set_clock(&is->audclk, get_clock(&is->audclk), is->audclk.serial);
//    } else {
//    }
//    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
//    if (is->step && (is->pause_req || is->buffering_on)) {
//        is->paused = is->vidclk.paused = is->extclk.paused = pause_on;
//    } else {
//        is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = pause_on;
//        SDL_AoutPauseAudio(ffp->aout, pause_on);
//    }
//}

//static void stream_update_pause_l(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    if (!is->step && (is->pause_req || is->buffering_on)) {
//        stream_toggle_pause_l(ffp, 1);
//    } else {
//        stream_toggle_pause_l(ffp, 0);
//    }
//}

//static void toggle_pause_l(FFPlayer *ffp, int pause_on)
//{
//    VideoState *is = ffp->is;
//    if (is->pause_req && !pause_on) {
//        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
//        set_clock(&is->audclk, get_clock(&is->audclk), is->audclk.serial);
//    }
//    is->pause_req = pause_on;
//    ffp->auto_resume = !pause_on;
//    stream_update_pause_l(ffp);
//    is->step = 0;
//}

//static void toggle_pause(FFPlayer *ffp, int pause_on)
//{
//    SDL_LockMutex(ffp->is->play_mutex);
//    toggle_pause_l(ffp, pause_on);
//    SDL_UnlockMutex(ffp->is->play_mutex);
//}

//// FFP_MERGE: toggle_mute
//// FFP_MERGE: update_volume

//static void step_to_next_frame_l(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    is->step = 1;
//    /* if the stream is paused unpause it, then step */
//    if (is->paused)
//        stream_toggle_pause_l(ffp, 0);
//}

//static double compute_target_delay(FFPlayer *ffp, double delay, VideoState *is)
//{
//    double sync_threshold, diff = 0;

//    /* update delay to follow master synchronisation source */
//    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
//        /* if video is slave, we try to correct big delays by
//           duplicating or deleting a frame */
//        diff = get_clock(&is->vidclk) - get_master_clock(is);

//        /* skip or repeat frame. We take into account the
//           delay to compute the threshold. I still don't know
//           if it is the best guess */
//        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
//        /* -- by bbcallen: replace is->max_frame_duration with AV_NOSYNC_THRESHOLD */
//        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
//            if (diff <= -sync_threshold)
//                delay = FFMAX(0, delay + diff);
//            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
//                delay = delay + diff;
//            else if (diff >= sync_threshold)
//                delay = 2 * delay;
//        }
//    }

//    if (ffp) {
//        ffp->stat.avdelay = delay;
//        ffp->stat.avdiff  = diff;
//    }
//#ifdef FFP_SHOW_AUDIO_DELAY
//    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
//            delay, -diff);
//#endif

//    return delay;
//}

//static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
//    if (vp->serial == nextvp->serial) {
//        double duration = nextvp->pts - vp->pts;
//        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
//            return vp->duration;
//        else
//            return duration;
//    } else {
//        return 0.0;
//    }
//}

//static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
//    /* update current video pts */
//    set_clock(&is->vidclk, pts, serial);
//    sync_clock_to_slave(&is->extclk, &is->vidclk);
//}

///* called to display each frame */
//static void video_refresh(FFPlayer *opaque, double *remaining_time)
//{
//    FFPlayer *ffp = opaque;
//    VideoState *is = ffp->is;
//    double time;

//    Frame *sp, *sp2;

//    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
//        check_external_clock_speed(is);

//    if (!ffp->display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
//        time = av_gettime_relative() / 1000000.0;
//        if (is->force_refresh || is->last_vis_time + ffp->rdftspeed < time) {
//            video_display2(ffp);
//            is->last_vis_time = time;
//        }
//        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + ffp->rdftspeed - time);
//    }

//    if (is->video_st) {
//retry:
//        if (frame_queue_nb_remaining(&is->pictq) == 0) {
//            // nothing to do, no picture to display in the queue
//        } else {
//            double last_duration, duration, delay;
//            Frame *vp, *lastvp;

//            /* dequeue the picture */
//            lastvp = frame_queue_peek_last(&is->pictq);
//            vp = frame_queue_peek(&is->pictq);

//            if (vp->serial != is->videoq.serial) {
//                frame_queue_next(&is->pictq);
//                goto retry;
//            }

//            if (lastvp->serial != vp->serial)
//                is->frame_timer = av_gettime_relative() / 1000000.0;

//            if (is->paused)
//                goto display;

//            /* compute nominal last_duration */
//            last_duration = vp_duration(is, lastvp, vp);
//            delay = compute_target_delay(ffp, last_duration, is);

//            time= av_gettime_relative()/1000000.0;
//            if (isnan(is->frame_timer) || time < is->frame_timer)
//                is->frame_timer = time;
//            if (time < is->frame_timer + delay) {
//                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
//                goto display;
//            }

//            is->frame_timer += delay;
//            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
//                is->frame_timer = time;

//            SDL_LockMutex(is->pictq.mutex);
//            if (!isnan(vp->pts))
//                update_video_pts(is, vp->pts, vp->pos, vp->serial);
//            SDL_UnlockMutex(is->pictq.mutex);

//            if (frame_queue_nb_remaining(&is->pictq) > 1) {
//                Frame *nextvp = frame_queue_peek_next(&is->pictq);
//                duration = vp_duration(is, vp, nextvp);
//                if(!is->step && (ffp->framedrop > 0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration) {
//                    frame_queue_next(&is->pictq);
//                    goto retry;
//                }
//            }

//            if (is->subtitle_st) {
//                while (frame_queue_nb_remaining(&is->subpq) > 0) {
//                    sp = frame_queue_peek(&is->subpq);

//                    if (frame_queue_nb_remaining(&is->subpq) > 1)
//                        sp2 = frame_queue_peek_next(&is->subpq);
//                    else
//                        sp2 = NULL;

//                    if (sp->serial != is->subtitleq.serial
//                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
//                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
//                    {
//                        if (sp->uploaded) {
//                            ffp_notify_msg4(ffp, FFP_MSG_TIMED_TEXT, 0, 0, "", 1);
//                        }
//                        frame_queue_next(&is->subpq);
//                    } else {
//                        break;
//                    }
//                }
//            }

//            frame_queue_next(&is->pictq);
//            is->force_refresh = 1;

//            SDL_LockMutex(ffp->is->play_mutex);
//            if (is->step) {
//                is->step = 0;
//                if (!is->paused)
//                    stream_update_pause_l(ffp);
//            }
//            SDL_UnlockMutex(ffp->is->play_mutex);
//        }
//display:
//        /* display picture */
//        if (!ffp->display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown)
//            video_display2(ffp);
//    }
//    is->force_refresh = 0;
//    if (ffp->show_status) {
//        static int64_t last_time;
//        int64_t cur_time;
//        int aqsize, vqsize, sqsize __unused;
//        double av_diff;

//        cur_time = av_gettime_relative();
//        if (!last_time || (cur_time - last_time) >= 30000) {
//            aqsize = 0;
//            vqsize = 0;
//            sqsize = 0;
//            if (is->audio_st)
//                aqsize = is->audioq.size;
//            if (is->video_st)
//                vqsize = is->videoq.size;
//#ifdef FFP_MERGE
//            if (is->subtitle_st)
//                sqsize = is->subtitleq.size;
//#else
//            sqsize = 0;
//#endif
//            av_diff = 0;
//            if (is->audio_st && is->video_st)
//                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
//            else if (is->video_st)
//                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
//            else if (is->audio_st)
//                av_diff = get_master_clock(is) - get_clock(&is->audclk);
//            av_log(NULL, AV_LOG_INFO,
//                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
//                   get_master_clock(is),
//                   (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
//                   av_diff,
//                   is->frame_drops_early + is->frame_drops_late,
//                   aqsize / 1024,
//                   vqsize / 1024,
//                   sqsize,
//                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
//                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);
//            fflush(stdout);
//            last_time = cur_time;
//        }
//    }
//}

///* allocate a picture (needs to do that in main thread to avoid
//   potential locking problems */
//static void alloc_picture(FFPlayer *ffp, int frame_format)
//{
//    VideoState *is = ffp->is;
//    Frame *vp;
//#ifdef FFP_MERGE
//    int sdl_format;
//#endif

//    vp = &is->pictq.queue[is->pictq.windex];

//    free_picture(vp);

//#ifdef FFP_MERGE
//    video_open(is, vp);
//#endif

//    SDL_VoutSetOverlayFormat(ffp->vout, ffp->overlay_format);
//    vp->bmp = SDL_Vout_CreateOverlay(vp->width, vp->height,
//                                   frame_format,
//                                   ffp->vout);
//#ifdef FFP_MERGE
//    if (vp->format == AV_PIX_FMT_YUV420P)
//        sdl_format = SDL_PIXELFORMAT_YV12;
//    else
//        sdl_format = SDL_PIXELFORMAT_ARGB8888;

//    if (realloc_texture(&vp->bmp, sdl_format, vp->width, vp->height, SDL_BLENDMODE_NONE, 0) < 0) {
//#else
//    /* RV16, RV32 contains only one plane */
//    if (!vp->bmp || (!vp->bmp->is_private && vp->bmp->pitches[0] < vp->width)) {
//#endif
//        /* SDL allocates a buffer smaller than requested if the video
//         * overlay hardware is unable to support the requested size. */
//        av_log(NULL, AV_LOG_FATAL,
//               "Error: the video system does not support an image\n"
//                        "size of %dx%d pixels. Try using -lowres or -vf \"scale=w:h\"\n"
//                        "to reduce the image size.\n", vp->width, vp->height );
//        free_picture(vp);
//    }

//    SDL_LockMutex(is->pictq.mutex);
//    vp->allocated = 1;
//    SDL_CondSignal(is->pictq.cond);
//    SDL_UnlockMutex(is->pictq.mutex);
//}



//static int get_video_frame(FFPlayer *ffp, AVFrame *frame)
//{
//    VideoState *is = ffp->is;
//    int got_picture;

//    ffp_video_statistic_l(ffp);
//    if ((got_picture = decoder_decode_frame(ffp, &is->viddec, frame, NULL)) < 0)
//        return -1;

//    if (got_picture) {
//        double dpts = NAN;

//        if (frame->pts != AV_NOPTS_VALUE)
//            dpts = av_q2d(is->video_st->time_base) * frame->pts;

//        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

//        if (ffp->framedrop>0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
//            ffp->stat.decode_frame_count++;
//            if (frame->pts != AV_NOPTS_VALUE) {
//                double diff = dpts - get_master_clock(is);
//                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
//                    diff - is->frame_last_filter_delay < 0 &&
//                    is->viddec.pkt_serial == is->vidclk.serial &&
//                    is->videoq.nb_packets) {
//                    is->frame_drops_early++;
//                    is->continuous_frame_drops_early++;
//                    if (is->continuous_frame_drops_early > ffp->framedrop) {
//                        is->continuous_frame_drops_early = 0;
//                    } else {
//                        ffp->stat.drop_frame_count++;
//                        ffp->stat.drop_frame_rate = (float)(ffp->stat.drop_frame_count) / (float)(ffp->stat.decode_frame_count);
//                        av_frame_unref(frame);
//                        got_picture = 0;
//                    }
//                }
//            }
//        }
//    }

//    return got_picture;
//}

#if CONFIG_AVFILTER
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

static int configure_video_filters(FFPlayer *ffp, AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_BGRA, AV_PIX_FMT_NONE };
    char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecParameters *codecpar = is->video_st->codecpar;
    AVRational fr = av_guess_frame_rate(is->ic, is->video_st, NULL);
    AVDictionaryEntry *e = NULL;

    while ((e = av_dict_get(ffp->sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';

    graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "ffplay_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
} while (0)

    if (ffp->autorotate) {
        double theta  = get_rotation(is->video_st);

        if (fabs(theta - 90) < 1.0) {
            INSERT_FILT("transpose", "clock");
        } else if (fabs(theta - 180) < 1.0) {
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        } else if (fabs(theta - 270) < 1.0) {
            INSERT_FILT("transpose", "cclock");
        } else if (fabs(theta) > 1.0) {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }

#ifdef FFP_AVFILTER_PLAYBACK_RATE
    if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        char setpts_buf[256];
        float rate = 1.0f / ffp->pf_playback_rate;
        rate = av_clipf_c(rate, 0.5f, 2.0f);
        av_log(ffp, AV_LOG_INFO, "vf_rate=%f(1/%f)\n", ffp->pf_playback_rate, rate);
        snprintf(setpts_buf, sizeof(setpts_buf), "%f*PTS", rate);
        INSERT_FILT("setpts", setpts_buf);
    }
#endif

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;

fail:
    return ret;
}

static int configure_audio_filters(FFPlayer *ffp, const char *afilters, int force_output_format)
{
    VideoState *is = ffp->is;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    int64_t channel_layouts[2] = { 0, -1 };
    int channels[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    AVDictionaryEntry *e = NULL;
    char asrc_args[256];
    int ret;
    char afilters_args[4096];

    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    while ((e = av_dict_get(ffp->swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
                   is->audio_filter_src.channels,
                   1, is->audio_filter_src.freq);
    if (is->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%"PRIx64,  is->audio_filter_src.channel_layout);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        channel_layouts[0] = is->audio_tgt.channel_layout;
        channels       [0] = is->audio_tgt.channels;
        sample_rates   [0] = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts" , channels       ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }

    afilters_args[0] = 0;
    if (afilters)
        snprintf(afilters_args, sizeof(afilters_args), "%s", afilters);

#ifdef FFP_AVFILTER_PLAYBACK_RATE
    if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        if (afilters_args[0])
            av_strlcatf(afilters_args, sizeof(afilters_args), ",");

        av_log(ffp, AV_LOG_INFO, "af_rate=%f\n", ffp->pf_playback_rate);
        av_strlcatf(afilters_args, sizeof(afilters_args), "atempo=%f", ffp->pf_playback_rate);
    }
#endif

    if ((ret = configure_filtergraph(is->agraph, afilters_args[0] ? afilters_args : NULL, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    return ret;
}
#endif  /* CONFIG_AVFILTER */

//static int ffplay_video_thread(void *arg)
//{
//    FFPlayer *ffp = arg;
//    VideoState *is = ffp->is;
//    AVFrame *frame = av_frame_alloc();
//    double pts;
//    double duration;
//    int ret;
//    AVRational tb = is->video_st->time_base;
//    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);
//    int64_t dst_pts = -1;
//    int64_t last_dst_pts = -1;
//    int retry_convert_image = 0;
//    int convert_frame_count = 0;

//#if CONFIG_AVFILTER
//    AVFilterGraph *graph = avfilter_graph_alloc();
//    AVFilterContext *filt_out = NULL, *filt_in = NULL;
//    int last_w = 0;
//    int last_h = 0;
//    enum AVPixelFormat last_format = -2;
//    int last_serial = -1;
//    int last_vfilter_idx = 0;
//    if (!graph) {
//        av_frame_free(&frame);
//        return AVERROR(ENOMEM);
//    }

//#else
//    ffp_notify_msg2(ffp, FFP_MSG_VIDEO_ROTATION_CHANGED, ffp_get_video_rotate_degrees(ffp));
//#endif

//    if (!frame) {
//#if CONFIG_AVFILTER
//        avfilter_graph_free(&graph);
//#endif
//        return AVERROR(ENOMEM);
//    }

//    for (;;) {
//        ret = get_video_frame(ffp, frame);
//        if (ret < 0)
//            goto the_end;
//        if (!ret)
//            continue;

//        if (ffp->get_frame_mode) {
//            if (!ffp->get_img_info || ffp->get_img_info->count <= 0) {
//                av_frame_unref(frame);
//                continue;
//            }

//            last_dst_pts = dst_pts;

//            if (dst_pts < 0) {
//                dst_pts = ffp->get_img_info->start_time;
//            } else {
//                dst_pts += (ffp->get_img_info->end_time - ffp->get_img_info->start_time) / (ffp->get_img_info->num - 1);
//            }

//            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
//            pts = pts * 1000;
//            if (pts >= dst_pts) {
//                while (retry_convert_image <= MAX_RETRY_CONVERT_IMAGE) {
//                    ret = convert_image(ffp, frame, (int64_t)pts, frame->width, frame->height);
//                    if (!ret) {
//                        convert_frame_count++;
//                        break;
//                    }
//                    retry_convert_image++;
//                    av_log(NULL, AV_LOG_ERROR, "convert image error retry_convert_image = %d\n", retry_convert_image);
//                }

//                retry_convert_image = 0;
//                if (ret || ffp->get_img_info->count <= 0) {
//                    if (ret) {
//                        av_log(NULL, AV_LOG_ERROR, "convert image abort ret = %d\n", ret);
//                        ffp_notify_msg3(ffp, FFP_MSG_GET_IMG_STATE, 0, ret);
//                    } else {
//                        av_log(NULL, AV_LOG_INFO, "convert image complete convert_frame_count = %d\n", convert_frame_count);
//                    }
//                    goto the_end;
//                }
//            } else {
//                dst_pts = last_dst_pts;
//            }
//            av_frame_unref(frame);
//            continue;
//        }

//#if CONFIG_AVFILTER
//        if (   last_w != frame->width
//            || last_h != frame->height
//            || last_format != frame->format
//            || last_serial != is->viddec.pkt_serial
//            || ffp->vf_changed
//            || last_vfilter_idx != is->vfilter_idx) {
//            SDL_LockMutex(ffp->vf_mutex);
//            ffp->vf_changed = 0;
//            av_log(NULL, AV_LOG_DEBUG,
//                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
//                   last_w, last_h,
//                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
//                   frame->width, frame->height,
//                   (const char *)av_x_if_null(av_get_pix_fmt_name(frame->format), "none"), is->viddec.pkt_serial);
//            avfilter_graph_free(&graph);
//            graph = avfilter_graph_alloc();
//            if ((ret = configure_video_filters(ffp, graph, is, ffp->vfilters_list ? ffp->vfilters_list[is->vfilter_idx] : NULL, frame)) < 0) {
//                // FIXME: post error
//                SDL_UnlockMutex(ffp->vf_mutex);
//                goto the_end;
//            }
//            filt_in  = is->in_video_filter;
//            filt_out = is->out_video_filter;
//            last_w = frame->width;
//            last_h = frame->height;
//            last_format = frame->format;
//            last_serial = is->viddec.pkt_serial;
//            last_vfilter_idx = is->vfilter_idx;
//            frame_rate = av_buffersink_get_frame_rate(filt_out);
//            SDL_UnlockMutex(ffp->vf_mutex);
//        }

//        ret = av_buffersrc_add_frame(filt_in, frame);
//        if (ret < 0)
//            goto the_end;

//        while (ret >= 0) {
//            is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

//            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
//            if (ret < 0) {
//                if (ret == AVERROR_EOF)
//                    is->viddec.finished = is->viddec.pkt_serial;
//                ret = 0;
//                break;
//            }

//            is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
//            if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
//                is->frame_last_filter_delay = 0;
//            tb = av_buffersink_get_time_base(filt_out);
//#endif
//            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
//            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
//            ret = queue_picture(ffp, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
//            av_frame_unref(frame);
//#if CONFIG_AVFILTER
//        }
//#endif

//        if (ret < 0)
//            goto the_end;
//    }
// the_end:
//#if CONFIG_AVFILTER
//    avfilter_graph_free(&graph);
//#endif
//    av_log(NULL, AV_LOG_INFO, "convert image convert_frame_count = %d\n", convert_frame_count);
//    av_frame_free(&frame);
//    return 0;
//}


//static int subtitle_thread(void *arg)
//{
//    FFPlayer *ffp = arg;
//    VideoState *is = ffp->is;
//    Frame *sp;
//    int got_subtitle;
//    double pts;

//    for (;;) {
//        if (!(sp = frame_queue_peek_writable(&is->subpq)))
//            return 0;

//        if ((got_subtitle = decoder_decode_frame(ffp, &is->subdec, NULL, &sp->sub)) < 0)
//            break;

//        pts = 0;
//#ifdef FFP_MERGE
//        if (got_subtitle && sp->sub.format == 0) {
//#else
//        if (got_subtitle) {
//#endif
//            if (sp->sub.pts != AV_NOPTS_VALUE)
//                pts = sp->sub.pts / (double)AV_TIME_BASE;
//            sp->pts = pts;
//            sp->serial = is->subdec.pkt_serial;
//            sp->width = is->subdec.avctx->width;
//            sp->height = is->subdec.avctx->height;
//            sp->uploaded = 0;

//            /* now we can update the picture count */
//            frame_queue_push(&is->subpq);
//#ifdef FFP_MERGE
//        } else if (got_subtitle) {
//            avsubtitle_free(&sp->sub);
//#endif
//        }
//    }
//    return 0;
//}

///* copy samples for viewing in editor window */
//static void update_sample_display(VideoState *is, short *samples, int samples_size)
//{
//    int size, len;

//    size = samples_size / sizeof(short);
//    while (size > 0) {
//        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
//        if (len > size)
//            len = size;
//        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
//        samples += len;
//        is->sample_array_index += len;
//        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
//            is->sample_array_index = 0;
//        size -= len;
//    }
//}

///* return the wanted number of samples to get better sync if sync_type is video
// * or external master clock */
//static int synchronize_audio(VideoState *is, int nb_samples)
//{
//    int wanted_nb_samples = nb_samples;

//    /* if not master, then we try to remove or add samples to correct the clock */
//    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
//        double diff, avg_diff;
//        int min_nb_samples, max_nb_samples;

//        diff = get_clock(&is->audclk) - get_master_clock(is);

//        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
//            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
//            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
//                /* not enough measures to have a correct estimate */
//                is->audio_diff_avg_count++;
//            } else {
//                /* estimate the A-V difference */
//                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

//                if (fabs(avg_diff) >= is->audio_diff_threshold) {
//                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
//                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
//                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
//                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
//                }
//                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
//                        diff, avg_diff, wanted_nb_samples - nb_samples,
//                        is->audio_clock, is->audio_diff_threshold);
//            }
//        } else {
//            /* too big difference : may be initial PTS errors, so
//               reset A-V filter */
//            is->audio_diff_avg_count = 0;
//            is->audio_diff_cum       = 0;
//        }
//    }

//    return wanted_nb_samples;
//}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
//static int audio_decode_frame(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    int data_size, resampled_data_size;
//    int64_t dec_channel_layout;
//    av_unused double audio_clock0;
//    int wanted_nb_samples;
//    Frame *af;
//#if defined(__ANDROID__)
//    int translate_time = 1;
//#endif

//    if (is->paused || is->step)
//        return -1;

//    if (ffp->sync_av_start &&                       /* sync enabled */
//        is->video_st &&                             /* has video stream */
//        !is->viddec.first_frame_decoded &&          /* not hot */
//        is->viddec.finished != is->videoq.serial) { /* not finished */
//        /* waiting for first video frame */
//        Uint64 now = SDL_GetTickHR();
//        if (now < is->viddec.first_frame_decoded_time ||
//            now > is->viddec.first_frame_decoded_time + 2000) {
//            is->viddec.first_frame_decoded = 1;
//        } else {
//            /* video pipeline is not ready yet */
//            return -1;
//        }
//    }
//reload:
//    do {
//#if defined(_WIN32) || defined(__APPLE__)
//        while (frame_queue_nb_remaining(&is->sampq) == 0) {
//            if ((av_gettime_relative() - ffp->audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
//                return -1;
//            av_usleep (1000);
//        }
//#endif
//        if (!(af = frame_queue_peek_readable(&is->sampq)))
//            return -1;
//        frame_queue_next(&is->sampq);
//    } while (af->serial != is->audioq.serial);

//    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
//                                           af->frame->nb_samples,
//                                           af->frame->format, 1);

//    dec_channel_layout =
//        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
//        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
//    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

//    if (af->frame->format        != is->audio_src.fmt            ||
//        dec_channel_layout       != is->audio_src.channel_layout ||
//        af->frame->sample_rate   != is->audio_src.freq           ||
//        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
//        AVDictionary *swr_opts = NULL;
//        swr_free(&is->swr_ctx);
//        is->swr_ctx = swr_alloc_set_opts(NULL,
//                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
//                                         dec_channel_layout,           af->frame->format, af->frame->sample_rate,
//                                         0, NULL);
//        if (!is->swr_ctx) {
//            av_log(NULL, AV_LOG_ERROR,
//                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
//                    af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
//                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
//            return -1;
//        }
//        av_dict_copy(&swr_opts, ffp->swr_opts, 0);
//        if (af->frame->channel_layout == AV_CH_LAYOUT_5POINT1_BACK)
//            av_opt_set_double(is->swr_ctx, "center_mix_level", ffp->preset_5_1_center_mix_level, 0);
//        av_opt_set_dict(is->swr_ctx, &swr_opts);
//        av_dict_free(&swr_opts);

//        if (swr_init(is->swr_ctx) < 0) {
//            av_log(NULL, AV_LOG_ERROR,
//                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
//                    af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
//                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
//            swr_free(&is->swr_ctx);
//            return -1;
//        }
//        is->audio_src.channel_layout = dec_channel_layout;
//        is->audio_src.channels       = af->frame->channels;
//        is->audio_src.freq = af->frame->sample_rate;
//        is->audio_src.fmt = af->frame->format;
//    }

//    if (is->swr_ctx) {
//        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
//        uint8_t **out = &is->audio_buf1;
//        int out_count = (int)((int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256);
//        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
//        int len2;
//        if (out_size < 0) {
//            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
//            return -1;
//        }
//        if (wanted_nb_samples != af->frame->nb_samples) {
//            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
//                                        wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
//                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
//                return -1;
//            }
//        }
//        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);

//        if (!is->audio_buf1)
//            return AVERROR(ENOMEM);
//        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
//        if (len2 < 0) {
//            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
//            return -1;
//        }
//        if (len2 == out_count) {
//            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
//            if (swr_init(is->swr_ctx) < 0)
//                swr_free(&is->swr_ctx);
//        }
//        is->audio_buf = is->audio_buf1;
//        int bytes_per_sample = av_get_bytes_per_sample(is->audio_tgt.fmt);
//        resampled_data_size = len2 * is->audio_tgt.channels * bytes_per_sample;
//#if defined(__ANDROID__)
//        if (ffp->soundtouch_enable && ffp->pf_playback_rate != 1.0f && !is->abort_request) {
//            av_fast_malloc(&is->audio_new_buf, &is->audio_new_buf_size, out_size * translate_time);
//            for (int i = 0; i < (resampled_data_size / 2); i++)
//            {
//                is->audio_new_buf[i] = (is->audio_buf1[i * 2] | (is->audio_buf1[i * 2 + 1] << 8));
//            }

//            int ret_len = ijk_soundtouch_translate(is->handle, is->audio_new_buf, (float)(ffp->pf_playback_rate), (float)(1.0f/ffp->pf_playback_rate),
//                    resampled_data_size / 2, bytes_per_sample, is->audio_tgt.channels, af->frame->sample_rate);
//            if (ret_len > 0) {
//                is->audio_buf = (uint8_t*)is->audio_new_buf;
//                resampled_data_size = ret_len;
//            } else {
//                translate_time++;
//                goto reload;
//            }
//        }
//#endif
//    } else {
//        is->audio_buf = af->frame->data[0];
//        resampled_data_size = data_size;
//    }

//    audio_clock0 = is->audio_clock;
//    /* update the audio clock with the pts */
//    if (!isnan(af->pts))
//        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
//    else
//        is->audio_clock = NAN;
//    is->audio_clock_serial = af->serial;
//#ifdef FFP_SHOW_AUDIO_DELAY
//    {
//        static double last_clock;
//        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
//               is->audio_clock - last_clock,
//               is->audio_clock, audio_clock0);
//        last_clock = is->audio_clock;
//    }
//#endif
//    if (!is->auddec.first_frame_decoded) {
//        ALOGD("avcodec/Audio: first frame decoded\n");
//        ffp_notify_msg1(ffp, FFP_MSG_AUDIO_DECODED_START);
//        is->auddec.first_frame_decoded_time = SDL_GetTickHR();
//        is->auddec.first_frame_decoded = 1;
//    }
//    return resampled_data_size;
//}


//static int decode_interrupt_cb(void *ctx)
//{
//    VideoState *is = ctx;
//    return is->abort_request;
//}

//static int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue, int min_frames) {
//    return stream_id < 0 ||
//           queue->abort_request ||
//           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
//#ifdef FFP_MERGE
//           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
//#endif
//           queue->nb_packets > min_frames;
//}

//static int is_realtime(AVFormatContext *s)
//{
//    if(   !strcmp(s->iformat->name, "rtp")
//       || !strcmp(s->iformat->name, "rtsp")
//       || !strcmp(s->iformat->name, "sdp")
//    )
//        return 1;

//    if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
//                 || !strncmp(s->filename, "udp:", 4)
//                )
//    )
//        return 1;
//    return 0;
//}


//static int video_refresh_thread(void *arg);
//static VideoState *stream_open(FFPlayer *ffp, const char *filename, AVInputFormat *iformat)
//{
//    assert(!ffp->is);
//    VideoState *is;

//    is = av_mallocz(sizeof(VideoState));
//    if (!is)
//        return NULL;
//    is->filename = av_strdup(filename);
//    if (!is->filename)
//        goto fail;
//    is->iformat = iformat;
//    is->ytop    = 0;
//    is->xleft   = 0;
//#if defined(__ANDROID__)
//    if (ffp->soundtouch_enable) {
//        is->handle = ijk_soundtouch_create();
//    }
//#endif

//    /* start video display */
//    if (frame_queue_init(&is->pictq, &is->videoq, ffp->pictq_size, 1) < 0)
//        goto fail;
//    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
//        goto fail;
//    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
//        goto fail;

//    if (packet_queue_init(&is->videoq) < 0 ||
//        packet_queue_init(&is->audioq) < 0 ||
//        packet_queue_init(&is->subtitleq) < 0)
//        goto fail;

//    if (!(is->continue_read_thread = SDL_CreateCond())) {
//        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
//        goto fail;
//    }

//    if (!(is->video_accurate_seek_cond = SDL_CreateCond())) {
//        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
//        ffp->enable_accurate_seek = 0;
//    }

//    if (!(is->audio_accurate_seek_cond = SDL_CreateCond())) {
//        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
//        ffp->enable_accurate_seek = 0;
//    }

//    init_clock(&is->vidclk, &is->videoq.serial);
//    init_clock(&is->audclk, &is->audioq.serial);
//    init_clock(&is->extclk, &is->extclk.serial);
//    is->audio_clock_serial = -1;
//    if (ffp->startup_volume < 0)
//        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", ffp->startup_volume);
//    if (ffp->startup_volume > 100)
//        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", ffp->startup_volume);
//    ffp->startup_volume = av_clip(ffp->startup_volume, 0, 100);
//    ffp->startup_volume = av_clip(SDL_MIX_MAXVOLUME * ffp->startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
//    is->audio_volume = ffp->startup_volume;
//    is->muted = 0;
//    is->av_sync_type = ffp->av_sync_type;

//    is->play_mutex = SDL_CreateMutex();
//    is->accurate_seek_mutex = SDL_CreateMutex();
//    ffp->is = is;
//    is->pause_req = !ffp->start_on_prepared;

//    is->video_refresh_tid = SDL_CreateThreadEx(&is->_video_refresh_tid, video_refresh_thread, ffp, "ff_vout");
//    if (!is->video_refresh_tid) {
//        av_freep(&ffp->is);
//        return NULL;
//    }

//    is->initialized_decoder = 0;
//    is->read_tid = SDL_CreateThreadEx(&is->_read_tid, read_thread, ffp, "ff_read");
//    if (!is->read_tid) {
//        av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
//        goto fail;
//    }

//    if (ffp->async_init_decoder && !ffp->video_disable && ffp->video_mime_type && strlen(ffp->video_mime_type) > 0
//                    && ffp->mediacodec_default_name && strlen(ffp->mediacodec_default_name) > 0) {
//        if (ffp->mediacodec_all_videos || ffp->mediacodec_avc || ffp->mediacodec_hevc || ffp->mediacodec_mpeg2) {
//            decoder_init(&is->viddec, NULL, &is->videoq, is->continue_read_thread);
//            ffp->node_vdec = ffpipeline_init_video_decoder(ffp->pipeline, ffp);
//        }
//    }
//    is->initialized_decoder = 1;

//    return is;
//fail:
//    is->initialized_decoder = 1;
//    is->abort_request = true;
//    if (is->video_refresh_tid)
//        SDL_WaitThread(is->video_refresh_tid, NULL);
//    stream_close(ffp);
//    return NULL;
//}

//// FFP_MERGE: stream_cycle_channel
//// FFP_MERGE: toggle_full_screen
//// FFP_MERGE: toggle_audio_display
//// FFP_MERGE: refresh_loop_wait_event
//// FFP_MERGE: event_loop
//// FFP_MERGE: opt_frame_size
//// FFP_MERGE: opt_width
//// FFP_MERGE: opt_height
//// FFP_MERGE: opt_format
//// FFP_MERGE: opt_frame_pix_fmt
//// FFP_MERGE: opt_sync
//// FFP_MERGE: opt_seek
//// FFP_MERGE: opt_duration
//// FFP_MERGE: opt_show_mode
//// FFP_MERGE: opt_input_file
//// FFP_MERGE: opt_codec
//// FFP_MERGE: dummy
//// FFP_MERGE: options
//// FFP_MERGE: show_usage
//// FFP_MERGE: show_help_default
//static int video_refresh_thread(void *arg)
//{
//    FFPlayer *ffp = arg;
//    VideoState *is = ffp->is;
//    double remaining_time = 0.0;
//    while (!is->abort_request) {
//        if (remaining_time > 0.0)
//            av_usleep((int)(int64_t)(remaining_time * 1000000.0));
//        remaining_time = REFRESH_RATE;
//        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
//            video_refresh(ffp, &remaining_time);
//    }

//    return 0;
//}

//static int lockmgr(void **mtx, enum AVLockOp op)
//{
//    switch (op) {
//    case AV_LOCK_CREATE:
//        *mtx = SDL_CreateMutex();
//        if (!*mtx) {
//            av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
//            return 1;
//        }
//        return 0;
//    case AV_LOCK_OBTAIN:
//        return !!SDL_LockMutex(*mtx);
//    case AV_LOCK_RELEASE:
//        return !!SDL_UnlockMutex(*mtx);
//    case AV_LOCK_DESTROY:
//        SDL_DestroyMutex(*mtx);
//        return 0;
//    }
//    return 1;
//}

//// FFP_MERGE: main

///*****************************************************************************
// * end last line in ffplay.c
// ****************************************************************************/

//static bool g_ffmpeg_global_inited = false;

//inline static int log_level_av_to_ijk(int av_level)
//{
//    int ijk_level = IJK_LOG_VERBOSE;
//    if      (av_level <= AV_LOG_PANIC)      ijk_level = IJK_LOG_FATAL;
//    else if (av_level <= AV_LOG_FATAL)      ijk_level = IJK_LOG_FATAL;
//    else if (av_level <= AV_LOG_ERROR)      ijk_level = IJK_LOG_ERROR;
//    else if (av_level <= AV_LOG_WARNING)    ijk_level = IJK_LOG_WARN;
//    else if (av_level <= AV_LOG_INFO)       ijk_level = IJK_LOG_INFO;
//    // AV_LOG_VERBOSE means detailed info
//    else if (av_level <= AV_LOG_VERBOSE)    ijk_level = IJK_LOG_INFO;
//    else if (av_level <= AV_LOG_DEBUG)      ijk_level = IJK_LOG_DEBUG;
//    else if (av_level <= AV_LOG_TRACE)      ijk_level = IJK_LOG_VERBOSE;
//    else                                    ijk_level = IJK_LOG_VERBOSE;
//    return ijk_level;
//}

//inline static int log_level_ijk_to_av(int ijk_level)
//{
//    int av_level = IJK_LOG_VERBOSE;
//    if      (ijk_level >= IJK_LOG_SILENT)   av_level = AV_LOG_QUIET;
//    else if (ijk_level >= IJK_LOG_FATAL)    av_level = AV_LOG_FATAL;
//    else if (ijk_level >= IJK_LOG_ERROR)    av_level = AV_LOG_ERROR;
//    else if (ijk_level >= IJK_LOG_WARN)     av_level = AV_LOG_WARNING;
//    else if (ijk_level >= IJK_LOG_INFO)     av_level = AV_LOG_INFO;
//    // AV_LOG_VERBOSE means detailed info
//    else if (ijk_level >= IJK_LOG_DEBUG)    av_level = AV_LOG_DEBUG;
//    else if (ijk_level >= IJK_LOG_VERBOSE)  av_level = AV_LOG_TRACE;
//    else if (ijk_level >= IJK_LOG_DEFAULT)  av_level = AV_LOG_TRACE;
//    else if (ijk_level >= IJK_LOG_UNKNOWN)  av_level = AV_LOG_TRACE;
//    else                                    av_level = AV_LOG_TRACE;
//    return av_level;
//}

//static void ffp_log_callback_brief(void *ptr, int level, const char *fmt, va_list vl)
//{
//    if (level > av_log_get_level())
//        return;

//    int ffplv __unused = log_level_av_to_ijk(level);
//    VLOG(ffplv, IJK_LOG_TAG, fmt, vl);
//}

//static void ffp_log_callback_report(void *ptr, int level, const char *fmt, va_list vl)
//{
//    if (level > av_log_get_level())
//        return;

//    int ffplv __unused = log_level_av_to_ijk(level);

//    va_list vl2;
//    char line[1024];
//    static int print_prefix = 1;

//    va_copy(vl2, vl);
//    // av_log_default_callback(ptr, level, fmt, vl);
//    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
//    va_end(vl2);

//    ALOG(ffplv, IJK_LOG_TAG, "%s", line);
//}

//int ijkav_register_all(void);
//void ffp_global_init()
//{
//    if (g_ffmpeg_global_inited)
//        return;

//    ALOGD("ijkmediaplayer version : %s", ijkmp_version());
//    /* register all codecs, demux and protocols */
//    avcodec_register_all();
//#if CONFIG_AVDEVICE
//    avdevice_register_all();
//#endif
//#if CONFIG_AVFILTER
//    avfilter_register_all();
//#endif
//    av_register_all();

//    ijkav_register_all();

//    avformat_network_init();

//    av_lockmgr_register(lockmgr);
//    av_log_set_callback(ffp_log_callback_brief);

//    av_init_packet(&flush_pkt);
//    flush_pkt.data = (uint8_t *)&flush_pkt;

//    g_ffmpeg_global_inited = true;
//}

//void ffp_global_uninit()
//{
//    if (!g_ffmpeg_global_inited)
//        return;

//    av_lockmgr_register(NULL);

//    // FFP_MERGE: uninit_opts

//    avformat_network_deinit();

//    g_ffmpeg_global_inited = false;
//}

//void ffp_global_set_log_report(int use_report)
//{
//    if (use_report) {
//        av_log_set_callback(ffp_log_callback_report);
//    } else {
//        av_log_set_callback(ffp_log_callback_brief);
//    }
//}

//void ffp_global_set_log_level(int log_level)
//{
//    int av_level = log_level_ijk_to_av(log_level);
//    av_log_set_level(av_level);
//}

//static ijk_inject_callback s_inject_callback;
//int inject_callback(void *opaque, int type, void *data, size_t data_size)
//{
//    if (s_inject_callback)
//        return s_inject_callback(opaque, type, data, data_size);
//    return 0;
//}

//void ffp_global_set_inject_callback(ijk_inject_callback cb)
//{
//    s_inject_callback = cb;
//}

//void ffp_io_stat_register(void (*cb)(const char *url, int type, int bytes))
//{
//    // avijk_io_stat_register(cb);
//}

//void ffp_io_stat_complete_register(void (*cb)(const char *url,
//                                              int64_t read_bytes, int64_t total_size,
//                                              int64_t elpased_time, int64_t total_duration))
//{
//    // avijk_io_stat_complete_register(cb);
//}

//static const char *ffp_context_to_name(void *ptr)
//{
//    return "FFPlayer";
//}


//static void *ffp_context_child_next(void *obj, void *prev)
//{
//    return NULL;
//}

//static const AVClass *ffp_context_child_class_next(const AVClass *prev)
//{
//    return NULL;
//}

//const AVClass ffp_context_class = {
//    .class_name       = "FFPlayer",
//    .item_name        = ffp_context_to_name,
//    .option           = ffp_context_options,
//    .version          = LIBAVUTIL_VERSION_INT,
//    .child_next       = ffp_context_child_next,
//    .child_class_next = ffp_context_child_class_next,
//};

//static const char *ijk_version_info()
//{
//    return IJKPLAYER_VERSION;
//}

//FFPlayer *ffp_create()
//{
//    av_log(NULL, AV_LOG_INFO, "av_version_info: %s\n", av_version_info());
//    av_log(NULL, AV_LOG_INFO, "ijk_version_info: %s\n", ijk_version_info());

//    FFPlayer* ffp = (FFPlayer*) av_mallocz(sizeof(FFPlayer));
//    if (!ffp)
//        return NULL;

//    msg_queue_init(&ffp->msg_queue);
//    ffp->af_mutex = SDL_CreateMutex();
//    ffp->vf_mutex = SDL_CreateMutex();

//    ffp_reset_internal(ffp);
//    ffp->av_class = &ffp_context_class;
//    ffp->meta = ijkmeta_create();

//    av_opt_set_defaults(ffp);
//    return ffp;
//}

//void ffp_destroy(FFPlayer *ffp)
//{
//    if (!ffp)
//        return;

//    if (ffp->is) {
//        av_log(NULL, AV_LOG_WARNING, "ffp_destroy_ffplayer: force stream_close()");
//        stream_close(ffp);
//        ffp->is = NULL;
//    }

//    SDL_VoutFreeP(&ffp->vout);
//    SDL_AoutFreeP(&ffp->aout);
//    ffpipenode_free_p(&ffp->node_vdec);
//    ffpipeline_free_p(&ffp->pipeline);
//    ijkmeta_destroy_p(&ffp->meta);
//    ffp_reset_internal(ffp);

//    SDL_DestroyMutexP(&ffp->af_mutex);
//    SDL_DestroyMutexP(&ffp->vf_mutex);

//    msg_queue_destroy(&ffp->msg_queue);


//    av_free(ffp);
//}

//void ffp_destroy_p(FFPlayer **pffp)
//{
//    if (!pffp)
//        return;

//    ffp_destroy(*pffp);
//    *pffp = NULL;
//}

//static AVDictionary **ffp_get_opt_dict(FFPlayer *ffp, int opt_category)
//{
//    assert(ffp);

//    switch (opt_category) {
//        case FFP_OPT_CATEGORY_FORMAT:   return &ffp->format_opts;
//        case FFP_OPT_CATEGORY_CODEC:    return &ffp->codec_opts;
//        case FFP_OPT_CATEGORY_SWS:      return &ffp->sws_dict;
//        case FFP_OPT_CATEGORY_PLAYER:   return &ffp->player_opts;
//        case FFP_OPT_CATEGORY_SWR:      return &ffp->swr_opts;
//        default:
//            av_log(ffp, AV_LOG_ERROR, "unknown option category %d\n", opt_category);
//            return NULL;
//    }
//}

//static int app_func_event(AVApplicationContext *h, int message ,void *data, size_t size)
//{
//    if (!h || !h->opaque || !data)
//        return 0;

//    FFPlayer *ffp = (FFPlayer *)h->opaque;
//    if (!ffp->inject_opaque)
//        return 0;
//    if (message == AVAPP_EVENT_IO_TRAFFIC && sizeof(AVAppIOTraffic) == size) {
//        AVAppIOTraffic *event = (AVAppIOTraffic *)(intptr_t)data;
//        if (event->bytes > 0) {
//            ffp->stat.byte_count += event->bytes;
//            SDL_SpeedSampler2Add(&ffp->stat.tcp_read_sampler, event->bytes);
//        }
//    } else if (message == AVAPP_EVENT_ASYNC_STATISTIC && sizeof(AVAppAsyncStatistic) == size) {
//        AVAppAsyncStatistic *statistic =  (AVAppAsyncStatistic *) (intptr_t)data;
//        ffp->stat.buf_backwards = statistic->buf_backwards;
//        ffp->stat.buf_forwards = statistic->buf_forwards;
//        ffp->stat.buf_capacity = statistic->buf_capacity;
//    }
//    return inject_callback(ffp->inject_opaque, message , data, size);
//}

//static int ijkio_app_func_event(IjkIOApplicationContext *h, int message ,void *data, size_t size)
//{
//    if (!h || !h->opaque || !data)
//        return 0;

//    FFPlayer *ffp = (FFPlayer *)h->opaque;
//    if (!ffp->ijkio_inject_opaque)
//        return 0;

//    if (message == IJKIOAPP_EVENT_CACHE_STATISTIC && sizeof(IjkIOAppCacheStatistic) == size) {
//        IjkIOAppCacheStatistic *statistic =  (IjkIOAppCacheStatistic *) (intptr_t)data;
//        ffp->stat.cache_physical_pos      = statistic->cache_physical_pos;
//        ffp->stat.cache_file_forwards     = statistic->cache_file_forwards;
//        ffp->stat.cache_file_pos          = statistic->cache_file_pos;
//        ffp->stat.cache_count_bytes       = statistic->cache_count_bytes;
//        ffp->stat.logical_file_size       = statistic->logical_file_size;
//    }

//    return 0;
//}

//void ffp_set_frame_at_time(FFPlayer *ffp, const char *path, int64_t start_time, int64_t end_time, int num, int definition) {
//    if (!ffp->get_img_info) {
//        ffp->get_img_info = av_mallocz(sizeof(GetImgInfo));
//        if (!ffp->get_img_info) {
//            ffp_notify_msg3(ffp, FFP_MSG_GET_IMG_STATE, 0, -1);
//            return;
//        }
//    }

//    if (start_time >= 0 && num > 0 && end_time >= 0 && end_time >= start_time) {
//        ffp->get_img_info->img_path   = av_strdup(path);
//        ffp->get_img_info->start_time = start_time;
//        ffp->get_img_info->end_time   = end_time;
//        ffp->get_img_info->num        = num;
//        ffp->get_img_info->count      = num;
//        if (definition== HD_IMAGE) {
//            ffp->get_img_info->width  = 640;
//            ffp->get_img_info->height = 360;
//        } else if (definition == SD_IMAGE) {
//            ffp->get_img_info->width  = 320;
//            ffp->get_img_info->height = 180;
//        } else {
//            ffp->get_img_info->width  = 160;
//            ffp->get_img_info->height = 90;
//        }
//    } else {
//        ffp->get_img_info->count = 0;
//        ffp_notify_msg3(ffp, FFP_MSG_GET_IMG_STATE, 0, -1);
//    }
//}

//void *ffp_set_ijkio_inject_opaque(FFPlayer *ffp, void *opaque)
//{
//    if (!ffp)
//        return NULL;
//    void *prev_weak_thiz = ffp->ijkio_inject_opaque;
//    ffp->ijkio_inject_opaque = opaque;

//    ijkio_manager_destroyp(&ffp->ijkio_manager_ctx);
//    ijkio_manager_create(&ffp->ijkio_manager_ctx, ffp);
//    ijkio_manager_set_callback(ffp->ijkio_manager_ctx, ijkio_app_func_event);
//    ffp_set_option_intptr(ffp, FFP_OPT_CATEGORY_FORMAT, "ijkiomanager", (uintptr_t)ffp->ijkio_manager_ctx);

//    return prev_weak_thiz;
//}

//void *ffp_set_inject_opaque(FFPlayer *ffp, void *opaque)
//{
//    if (!ffp)
//        return NULL;
//    void *prev_weak_thiz = ffp->inject_opaque;
//    ffp->inject_opaque = opaque;

//    av_application_closep(&ffp->app_ctx);
//    av_application_open(&ffp->app_ctx, ffp);
//    ffp_set_option_intptr(ffp, FFP_OPT_CATEGORY_FORMAT, "ijkapplication", (uint64_t)(intptr_t)ffp->app_ctx);

//    ffp->app_ctx->func_on_app_event = app_func_event;
//    return prev_weak_thiz;
//}

//void ffp_set_option(FFPlayer *ffp, int opt_category, const char *name, const char *value)
//{
//    if (!ffp)
//        return;

//    AVDictionary **dict = ffp_get_opt_dict(ffp, opt_category);
//    av_dict_set(dict, name, value, 0);
//}

//void ffp_set_option_int(FFPlayer *ffp, int opt_category, const char *name, int64_t value)
//{
//    if (!ffp)
//        return;

//    AVDictionary **dict = ffp_get_opt_dict(ffp, opt_category);
//    av_dict_set_int(dict, name, value, 0);
//}

//void ffp_set_option_intptr(FFPlayer *ffp, int opt_category, const char *name, uintptr_t value)
//{
//    if (!ffp)
//        return;

//    AVDictionary **dict = ffp_get_opt_dict(ffp, opt_category);
//    av_dict_set_intptr(dict, name, value, 0);
//}

//void ffp_set_overlay_format(FFPlayer *ffp, int chroma_fourcc)
//{
//    switch (chroma_fourcc) {
//        case SDL_FCC__GLES2:
//        case SDL_FCC_I420:
//        case SDL_FCC_YV12:
//        case SDL_FCC_RV16:
//        case SDL_FCC_RV24:
//        case SDL_FCC_RV32:
//            ffp->overlay_format = chroma_fourcc;
//            break;
//#ifdef __APPLE__
//        case SDL_FCC_I444P10LE:
//            ffp->overlay_format = chroma_fourcc;
//            break;
//#endif
//        default:
//            av_log(ffp, AV_LOG_ERROR, "ffp_set_overlay_format: unknown chroma fourcc: %d\n", chroma_fourcc);
//            break;
//    }
//}

//int ffp_get_video_codec_info(FFPlayer *ffp, char **codec_info)
//{
//    if (!codec_info)
//        return -1;

//    // FIXME: not thread-safe
//    if (ffp->video_codec_info) {
//        *codec_info = strdup(ffp->video_codec_info);
//    } else {
//        *codec_info = NULL;
//    }
//    return 0;
//}

//int ffp_get_audio_codec_info(FFPlayer *ffp, char **codec_info)
//{
//    if (!codec_info)
//        return -1;

//    // FIXME: not thread-safe
//    if (ffp->audio_codec_info) {
//        *codec_info = strdup(ffp->audio_codec_info);
//    } else {
//        *codec_info = NULL;
//    }
//    return 0;
//}

//static void ffp_show_dict(FFPlayer *ffp, const char *tag, AVDictionary *dict)
//{
//    AVDictionaryEntry *t = NULL;

//    while ((t = av_dict_get(dict, "", t, AV_DICT_IGNORE_SUFFIX))) {
//        av_log(ffp, AV_LOG_INFO, "%-*s: %-*s = %s\n", 12, tag, 28, t->key, t->value);
//    }
//}

//#define FFP_VERSION_MODULE_NAME_LENGTH 13
//static void ffp_show_version_str(FFPlayer *ffp, const char *module, const char *version)
//{
//        av_log(ffp, AV_LOG_INFO, "%-*s: %s\n", FFP_VERSION_MODULE_NAME_LENGTH, module, version);
//}

//static void ffp_show_version_int(FFPlayer *ffp, const char *module, unsigned version)
//{
//    av_log(ffp, AV_LOG_INFO, "%-*s: %u.%u.%u\n",
//           FFP_VERSION_MODULE_NAME_LENGTH, module,
//           (unsigned int)IJKVERSION_GET_MAJOR(version),
//           (unsigned int)IJKVERSION_GET_MINOR(version),
//           (unsigned int)IJKVERSION_GET_MICRO(version));
//}

//int ffp_prepare_async_l(FFPlayer *ffp, const char *file_name)
//{
//    assert(ffp);
//    assert(!ffp->is);
//    assert(file_name);

//    if (av_stristart(file_name, "rtmp", NULL) ||
//        av_stristart(file_name, "rtsp", NULL)) {
//        // There is total different meaning for 'timeout' option in rtmp
//        av_log(ffp, AV_LOG_WARNING, "remove 'timeout' option for rtmp.\n");
//        av_dict_set(&ffp->format_opts, "timeout", NULL, 0);
//    }

//    /* there is a length limit in avformat */
//    if (strlen(file_name) + 1 > 1024) {
//        av_log(ffp, AV_LOG_ERROR, "%s too long url\n", __func__);
//        if (avio_find_protocol_name("ijklongurl:")) {
//            av_dict_set(&ffp->format_opts, "ijklongurl-url", file_name, 0);
//            file_name = "ijklongurl:";
//        }
//    }

//    av_log(NULL, AV_LOG_INFO, "===== versions =====\n");
//    ffp_show_version_str(ffp, "ijkplayer",      ijk_version_info());
//    ffp_show_version_str(ffp, "FFmpeg",         av_version_info());
//    ffp_show_version_int(ffp, "libavutil",      avutil_version());
//    ffp_show_version_int(ffp, "libavcodec",     avcodec_version());
//    ffp_show_version_int(ffp, "libavformat",    avformat_version());
//    ffp_show_version_int(ffp, "libswscale",     swscale_version());
//    ffp_show_version_int(ffp, "libswresample",  swresample_version());
//    av_log(NULL, AV_LOG_INFO, "===== options =====\n");
//    ffp_show_dict(ffp, "player-opts", ffp->player_opts);
//    ffp_show_dict(ffp, "format-opts", ffp->format_opts);
//    ffp_show_dict(ffp, "codec-opts ", ffp->codec_opts);
//    ffp_show_dict(ffp, "sws-opts   ", ffp->sws_dict);
//    ffp_show_dict(ffp, "swr-opts   ", ffp->swr_opts);
//    av_log(NULL, AV_LOG_INFO, "===================\n");

//    av_opt_set_dict(ffp, &ffp->player_opts);
//    if (!ffp->aout) {
//        ffp->aout = ffpipeline_open_audio_output(ffp->pipeline, ffp);
//        if (!ffp->aout)
//            return -1;
//    }

//#if CONFIG_AVFILTER
//    if (ffp->vfilter0) {
//        GROW_ARRAY(ffp->vfilters_list, ffp->nb_vfilters);
//        ffp->vfilters_list[ffp->nb_vfilters - 1] = ffp->vfilter0;
//    }
//#endif

//    VideoState *is = stream_open(ffp, file_name, NULL);
//    if (!is) {
//        av_log(NULL, AV_LOG_WARNING, "ffp_prepare_async_l: stream_open failed OOM");
//        return EIJK_OUT_OF_MEMORY;
//    }

//    ffp->is = is;
//    ffp->input_filename = av_strdup(file_name);
//    return 0;
//}

//int ffp_start_from_l(FFPlayer *ffp, long msec)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (!is)
//        return EIJK_NULL_IS_PTR;

//    ffp->auto_resume = 1;
//    ffp_toggle_buffering(ffp, 1);
//    ffp_seek_to_l(ffp, msec);
//    return 0;
//}

//int ffp_start_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (!is)
//        return EIJK_NULL_IS_PTR;

//    toggle_pause(ffp, 0);
//    return 0;
//}

//int ffp_pause_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (!is)
//        return EIJK_NULL_IS_PTR;

//    toggle_pause(ffp, 1);
//    return 0;
//}

//int ffp_is_paused_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (!is)
//        return 1;

//    return is->paused;
//}

//int ffp_stop_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (is) {
//        is->abort_request = 1;
//        toggle_pause(ffp, 1);
//    }

//    msg_queue_abort(&ffp->msg_queue);
//    if (ffp->enable_accurate_seek && is && is->accurate_seek_mutex
//        && is->audio_accurate_seek_cond && is->video_accurate_seek_cond) {
//        SDL_LockMutex(is->accurate_seek_mutex);
//        is->audio_accurate_seek_req = 0;
//        is->video_accurate_seek_req = 0;
//        SDL_CondSignal(is->audio_accurate_seek_cond);
//        SDL_CondSignal(is->video_accurate_seek_cond);
//        SDL_UnlockMutex(is->accurate_seek_mutex);
//    }
//    return 0;
//}

//int ffp_wait_stop_l(FFPlayer *ffp)
//{
//    assert(ffp);

//    if (ffp->is) {
//        ffp_stop_l(ffp);
//        stream_close(ffp);
//        ffp->is = NULL;
//    }
//    return 0;
//}

//int ffp_seek_to_l(FFPlayer *ffp, long msec)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    int64_t start_time = 0;
//    int64_t seek_pos = milliseconds_to_fftime(msec);
//    int64_t duration = milliseconds_to_fftime(ffp_get_duration_l(ffp));

//    if (!is)
//        return EIJK_NULL_IS_PTR;

//    if (duration > 0 && seek_pos >= duration && ffp->enable_accurate_seek) {
//        toggle_pause(ffp, 1);
//        ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
//        return 0;
//    }

//    start_time = is->ic->start_time;
//    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
//        seek_pos += start_time;

//    // FIXME: 9 seek by bytes
//    // FIXME: 9 seek out of range
//    // FIXME: 9 seekable
//    av_log(ffp, AV_LOG_DEBUG, "stream_seek %"PRId64"(%d) + %"PRId64", \n", seek_pos, (int)msec, start_time);
//    stream_seek(is, seek_pos, 0, 0);
//    return 0;
//}

//long ffp_get_current_position_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (!is || !is->ic)
//        return 0;

//    int64_t start_time = is->ic->start_time;
//    int64_t start_diff = 0;
//    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
//        start_diff = fftime_to_milliseconds(start_time);

//    int64_t pos = 0;
//    double pos_clock = get_master_clock(is);
//    if (isnan(pos_clock)) {
//        pos = fftime_to_milliseconds(is->seek_pos);
//    } else {
//        pos = pos_clock * 1000;
//    }

//    // If using REAL time and not ajusted, then return the real pos as calculated from the stream
//    // the use case for this is primarily when using a custom non-seekable data source that starts
//    // with a buffer that is NOT the start of the stream.  We want the get_current_position to
//    // return the time in the stream, and not the player's internal clock.
//    if (ffp->no_time_adjust) {
//        return (long)pos;
//    }

//    if (pos < 0 || pos < start_diff)
//        return 0;

//    int64_t adjust_pos = pos - start_diff;
//    return (long)adjust_pos;
//}

//long ffp_get_duration_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    VideoState *is = ffp->is;
//    if (!is || !is->ic)
//        return 0;

//    int64_t duration = fftime_to_milliseconds(is->ic->duration);
//    if (duration < 0)
//        return 0;

//    return (long)duration;
//}

//long ffp_get_playable_duration_l(FFPlayer *ffp)
//{
//    assert(ffp);
//    if (!ffp)
//        return 0;

//    return (long)ffp->playable_duration_ms;
//}

//void ffp_set_loop(FFPlayer *ffp, int loop)
//{
//    assert(ffp);
//    if (!ffp)
//        return;
//    ffp->loop = loop;
//}

//int ffp_get_loop(FFPlayer *ffp)
//{
//    assert(ffp);
//    if (!ffp)
//        return 1;
//    return ffp->loop;
//}

//int ffp_packet_queue_init(PacketQueue *q)
//{
//    return packet_queue_init(q);
//}

//void ffp_packet_queue_destroy(PacketQueue *q)
//{
//    return packet_queue_destroy(q);
//}

//void ffp_packet_queue_abort(PacketQueue *q)
//{
//    return packet_queue_abort(q);
//}

//void ffp_packet_queue_start(PacketQueue *q)
//{
//    return packet_queue_start(q);
//}

//void ffp_packet_queue_flush(PacketQueue *q)
//{
//    return packet_queue_flush(q);
//}

//int ffp_packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
//{
//    return packet_queue_get(q, pkt, block, serial);
//}



//int ffp_packet_queue_put(PacketQueue *q, AVPacket *pkt)
//{
//    return packet_queue_put(q, pkt);
//}

//bool ffp_is_flush_packet(AVPacket *pkt)
//{
//    if (!pkt)
//        return false;

//    return pkt->data == flush_pkt.data;
//}

//Frame *ffp_frame_queue_peek_writable(FrameQueue *f)
//{
//    return frame_queue_peek_writable(f);
//}

//void ffp_frame_queue_push(FrameQueue *f)
//{
//    return frame_queue_push(f);
//}





//int ffp_get_master_sync_type(VideoState *is)
//{
//    return get_master_sync_type(is);
//}

//double ffp_get_master_clock(VideoState *is)
//{
//    return get_master_clock(is);
//}

//void ffp_toggle_buffering_l(FFPlayer *ffp, int buffering_on)
//{
//    if (!ffp->packet_buffering)
//        return;

//    VideoState *is = ffp->is;
//    if (buffering_on && !is->buffering_on) {
//        av_log(ffp, AV_LOG_DEBUG, "ffp_toggle_buffering_l: start\n");
//        is->buffering_on = 1;
//        stream_update_pause_l(ffp);
//        if (is->seek_req) {
//            is->seek_buffering = 1;
//            ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_START, 1);
//        } else {
//            ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_START, 0);
//        }
//    } else if (!buffering_on && is->buffering_on){
//        av_log(ffp, AV_LOG_DEBUG, "ffp_toggle_buffering_l: end\n");
//        is->buffering_on = 0;
//        stream_update_pause_l(ffp);
//        if (is->seek_buffering) {
//            is->seek_buffering = 0;
//            ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_END, 1);
//        } else {
//            ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_END, 0);
//        }
//    }
//}

//void ffp_toggle_buffering(FFPlayer *ffp, int start_buffering)
//{
//    SDL_LockMutex(ffp->is->play_mutex);
//    ffp_toggle_buffering_l(ffp, start_buffering);
//    SDL_UnlockMutex(ffp->is->play_mutex);
//}

//void ffp_track_statistic_l(FFPlayer *ffp, AVStream *st, PacketQueue *q, FFTrackCacheStatistic *cache)
//{
//    assert(cache);

//    if (q) {
//        cache->bytes   = q->size;
//        cache->packets = q->nb_packets;
//    }

//    if (q && st && st->time_base.den > 0 && st->time_base.num > 0) {
//        cache->duration = q->duration * av_q2d(st->time_base) * 1000;
//    }
//}

//void ffp_audio_statistic_l(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    ffp_track_statistic_l(ffp, is->audio_st, &is->audioq, &ffp->stat.audio_cache);
//}

//void ffp_video_statistic_l(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    ffp_track_statistic_l(ffp, is->video_st, &is->videoq, &ffp->stat.video_cache);
//}

//void ffp_statistic_l(FFPlayer *ffp)
//{
//    ffp_audio_statistic_l(ffp);
//    ffp_video_statistic_l(ffp);
//}

//void ffp_check_buffering_l(FFPlayer *ffp)
//{
//    VideoState *is            = ffp->is;
//    int hwm_in_ms             = ffp->dcc.current_high_water_mark_in_ms; // use fast water mark for first loading
//    int buf_size_percent      = -1;
//    int buf_time_percent      = -1;
//    int hwm_in_bytes          = ffp->dcc.high_water_mark_in_bytes;
//    int need_start_buffering  = 0;
//    int audio_time_base_valid = 0;
//    int video_time_base_valid = 0;
//    int64_t buf_time_position = -1;

//    if(is->audio_st)
//        audio_time_base_valid = is->audio_st->time_base.den > 0 && is->audio_st->time_base.num > 0;
//    if(is->video_st)
//        video_time_base_valid = is->video_st->time_base.den > 0 && is->video_st->time_base.num > 0;

//    if (hwm_in_ms > 0) {
//        int     cached_duration_in_ms = -1;
//        int64_t audio_cached_duration = -1;
//        int64_t video_cached_duration = -1;

//        if (is->audio_st && audio_time_base_valid) {
//            audio_cached_duration = ffp->stat.audio_cache.duration;
//#ifdef FFP_SHOW_DEMUX_CACHE
//            int audio_cached_percent = (int)av_rescale(audio_cached_duration, 1005, hwm_in_ms * 10);
//            av_log(ffp, AV_LOG_DEBUG, "audio cache=%%%d milli:(%d/%d) bytes:(%d/%d) packet:(%d/%d)\n", audio_cached_percent,
//                  (int)audio_cached_duration, hwm_in_ms,
//                  is->audioq.size, hwm_in_bytes,
//                  is->audioq.nb_packets, MIN_FRAMES);
//#endif
//        }

//        if (is->video_st && video_time_base_valid) {
//            video_cached_duration = ffp->stat.video_cache.duration;
//#ifdef FFP_SHOW_DEMUX_CACHE
//            int video_cached_percent = (int)av_rescale(video_cached_duration, 1005, hwm_in_ms * 10);
//            av_log(ffp, AV_LOG_DEBUG, "video cache=%%%d milli:(%d/%d) bytes:(%d/%d) packet:(%d/%d)\n", video_cached_percent,
//                  (int)video_cached_duration, hwm_in_ms,
//                  is->videoq.size, hwm_in_bytes,
//                  is->videoq.nb_packets, MIN_FRAMES);
//#endif
//        }

//        if (video_cached_duration > 0 && audio_cached_duration > 0) {
//            cached_duration_in_ms = (int)IJKMIN(video_cached_duration, audio_cached_duration);
//        } else if (video_cached_duration > 0) {
//            cached_duration_in_ms = (int)video_cached_duration;
//        } else if (audio_cached_duration > 0) {
//            cached_duration_in_ms = (int)audio_cached_duration;
//        }

//        if (cached_duration_in_ms >= 0) {
//            buf_time_position = ffp_get_current_position_l(ffp) + cached_duration_in_ms;
//            ffp->playable_duration_ms = buf_time_position;

//            buf_time_percent = (int)av_rescale(cached_duration_in_ms, 1005, hwm_in_ms * 10);
//#ifdef FFP_SHOW_DEMUX_CACHE
//            av_log(ffp, AV_LOG_DEBUG, "time cache=%%%d (%d/%d)\n", buf_time_percent, cached_duration_in_ms, hwm_in_ms);
//#endif
//#ifdef FFP_NOTIFY_BUF_TIME
//            ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_TIME_UPDATE, cached_duration_in_ms, hwm_in_ms);
//#endif
//        }
//    }

//    int cached_size = is->audioq.size + is->videoq.size;
//    if (hwm_in_bytes > 0) {
//        buf_size_percent = (int)av_rescale(cached_size, 1005, hwm_in_bytes * 10);
//#ifdef FFP_SHOW_DEMUX_CACHE
//        av_log(ffp, AV_LOG_DEBUG, "size cache=%%%d (%d/%d)\n", buf_size_percent, cached_size, hwm_in_bytes);
//#endif
//#ifdef FFP_NOTIFY_BUF_BYTES
//        ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_BYTES_UPDATE, cached_size, hwm_in_bytes);
//#endif
//    }

//    int buf_percent = -1;
//    if (buf_time_percent >= 0) {
//        // alwas depend on cache duration if valid
//        if (buf_time_percent >= 100)
//            need_start_buffering = 1;
//        buf_percent = buf_time_percent;
//    } else {
//        if (buf_size_percent >= 100)
//            need_start_buffering = 1;
//        buf_percent = buf_size_percent;
//    }

//    if (buf_time_percent >= 0 && buf_size_percent >= 0) {
//        buf_percent = FFMIN(buf_time_percent, buf_size_percent);
//    }
//    if (buf_percent) {
//#ifdef FFP_SHOW_BUF_POS
//        av_log(ffp, AV_LOG_DEBUG, "buf pos=%"PRId64", %%%d\n", buf_time_position, buf_percent);
//#endif
//        ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, (int)buf_time_position, buf_percent);
//    }

//    if (need_start_buffering) {
//        if (hwm_in_ms < ffp->dcc.next_high_water_mark_in_ms) {
//            hwm_in_ms = ffp->dcc.next_high_water_mark_in_ms;
//        } else {
//            hwm_in_ms *= 2;
//        }

//        if (hwm_in_ms > ffp->dcc.last_high_water_mark_in_ms)
//            hwm_in_ms = ffp->dcc.last_high_water_mark_in_ms;

//        ffp->dcc.current_high_water_mark_in_ms = hwm_in_ms;

//        if (is->buffer_indicator_queue && is->buffer_indicator_queue->nb_packets > 0) {
//            if (   (is->audioq.nb_packets >= MIN_MIN_FRAMES || is->audio_stream < 0 || is->audioq.abort_request)
//                && (is->videoq.nb_packets >= MIN_MIN_FRAMES || is->video_stream < 0 || is->videoq.abort_request)) {
//                ffp_toggle_buffering(ffp, 0);
//            }
//        }
//    }
//}

//int ffp_video_thread(FFPlayer *ffp)
//{
//    return ffplay_video_thread(ffp);
//}

//void ffp_set_video_codec_info(FFPlayer *ffp, const char *module, const char *codec)
//{
//    av_freep(&ffp->video_codec_info);
//    ffp->video_codec_info = av_asprintf("%s, %s", module ? module : "", codec ? codec : "");
//    av_log(ffp, AV_LOG_INFO, "VideoCodec: %s\n", ffp->video_codec_info);
//}

//void ffp_set_audio_codec_info(FFPlayer *ffp, const char *module, const char *codec)
//{
//    av_freep(&ffp->audio_codec_info);
//    ffp->audio_codec_info = av_asprintf("%s, %s", module ? module : "", codec ? codec : "");
//    av_log(ffp, AV_LOG_INFO, "AudioCodec: %s\n", ffp->audio_codec_info);
//}

//void ffp_set_subtitle_codec_info(FFPlayer *ffp, const char *module, const char *codec)
//{
//    av_freep(&ffp->subtitle_codec_info);
//    ffp->subtitle_codec_info = av_asprintf("%s, %s", module ? module : "", codec ? codec : "");
//    av_log(ffp, AV_LOG_INFO, "SubtitleCodec: %s\n", ffp->subtitle_codec_info);
//}

//void ffp_set_playback_rate(FFPlayer *ffp, float rate)
//{
//    if (!ffp)
//        return;

//    av_log(ffp, AV_LOG_INFO, "Playback rate: %f\n", rate);
//    ffp->pf_playback_rate = rate;
//    ffp->pf_playback_rate_changed = 1;
//}

//void ffp_set_playback_volume(FFPlayer *ffp, float volume)
//{
//    if (!ffp)
//        return;
//    ffp->pf_playback_volume = volume;
//    ffp->pf_playback_volume_changed = 1;
//}

//int ffp_get_video_rotate_degrees(FFPlayer *ffp)
//{
//    VideoState *is = ffp->is;
//    if (!is)
//        return 0;

//    int theta  = abs((int)((int64_t)round(fabs(get_rotation(is->video_st))) % 360));
//    switch (theta) {
//        case 0:
//        case 90:
//        case 180:
//        case 270:
//            break;
//        case 360:
//            theta = 0;
//            break;
//        default:
//            ALOGW("Unknown rotate degress: %d\n", theta);
//            theta = 0;
//            break;
//    }

//    return theta;
//}

//int ffp_set_stream_selected(FFPlayer *ffp, int stream, int selected)
//{
//    VideoState        *is = ffp->is;
//    AVFormatContext   *ic = NULL;
//    AVCodecParameters *codecpar = NULL;
//    if (!is)
//        return -1;
//    ic = is->ic;
//    if (!ic)
//        return -1;

//    if (stream < 0 || stream >= ic->nb_streams) {
//        av_log(ffp, AV_LOG_ERROR, "invalid stream index %d >= stream number (%d)\n", stream, ic->nb_streams);
//        return -1;
//    }

//    codecpar = ic->streams[stream]->codecpar;

//    if (selected) {
//        switch (codecpar->codec_type) {
//            case AVMEDIA_TYPE_VIDEO:
//                if (stream != is->video_stream && is->video_stream >= 0)
//                    stream_component_close(ffp, is->video_stream);
//                break;
//            case AVMEDIA_TYPE_AUDIO:
//                if (stream != is->audio_stream && is->audio_stream >= 0)
//                    stream_component_close(ffp, is->audio_stream);
//                break;
//            case AVMEDIA_TYPE_SUBTITLE:
//                if (stream != is->subtitle_stream && is->subtitle_stream >= 0)
//                    stream_component_close(ffp, is->subtitle_stream);
//                break;
//            default:
//                av_log(ffp, AV_LOG_ERROR, "select invalid stream %d of video type %d\n", stream, codecpar->codec_type);
//                return -1;
//        }
//        return stream_component_open(ffp, stream);
//    } else {
//        switch (codecpar->codec_type) {
//            case AVMEDIA_TYPE_VIDEO:
//                if (stream == is->video_stream)
//                    stream_component_close(ffp, is->video_stream);
//                break;
//            case AVMEDIA_TYPE_AUDIO:
//                if (stream == is->audio_stream)
//                    stream_component_close(ffp, is->audio_stream);
//                break;
//            case AVMEDIA_TYPE_SUBTITLE:
//                if (stream == is->subtitle_stream)
//                    stream_component_close(ffp, is->subtitle_stream);
//                break;
//            default:
//                av_log(ffp, AV_LOG_ERROR, "select invalid stream %d of audio type %d\n", stream, codecpar->codec_type);
//                return -1;
//        }
//        return 0;
//    }
//}

//float ffp_get_property_float(FFPlayer *ffp, int id, float default_value)
//{
//    switch (id) {
//        case FFP_PROP_FLOAT_VIDEO_DECODE_FRAMES_PER_SECOND:
//            return ffp ? ffp->stat.vdps : default_value;
//        case FFP_PROP_FLOAT_VIDEO_OUTPUT_FRAMES_PER_SECOND:
//            return ffp ? ffp->stat.vfps : default_value;
//        case FFP_PROP_FLOAT_PLAYBACK_RATE:
//            return ffp ? ffp->pf_playback_rate : default_value;
//        case FFP_PROP_FLOAT_AVDELAY:
//            return ffp ? ffp->stat.avdelay : default_value;
//        case FFP_PROP_FLOAT_AVDIFF:
//            return ffp ? ffp->stat.avdiff : default_value;
//        case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
//            return ffp ? ffp->pf_playback_volume : default_value;
//        case FFP_PROP_FLOAT_DROP_FRAME_RATE:
//            return ffp ? ffp->stat.drop_frame_rate : default_value;
//        default:
//            return default_value;
//    }
//}

//void ffp_set_property_float(FFPlayer *ffp, int id, float value)
//{
//    switch (id) {
//        case FFP_PROP_FLOAT_PLAYBACK_RATE:
//            ffp_set_playback_rate(ffp, value);
//            break;
//        case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
//            ffp_set_playback_volume(ffp, value);
//            break;
//        default:
//            return;
//    }
//}

//int64_t ffp_get_property_int64(FFPlayer *ffp, int id, int64_t default_value)
//{
//    switch (id) {
//        case FFP_PROP_INT64_SELECTED_VIDEO_STREAM:
//            if (!ffp || !ffp->is)
//                return default_value;
//            return ffp->is->video_stream;
//        case FFP_PROP_INT64_SELECTED_AUDIO_STREAM:
//            if (!ffp || !ffp->is)
//                return default_value;
//            return ffp->is->audio_stream;
//        case FFP_PROP_INT64_SELECTED_TIMEDTEXT_STREAM:
//            if (!ffp || !ffp->is)
//                return default_value;
//            return ffp->is->subtitle_stream;
//        case FFP_PROP_INT64_VIDEO_DECODER:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.vdec_type;
//        case FFP_PROP_INT64_AUDIO_DECODER:
//            return FFP_PROPV_DECODER_AVCODEC;

//        case FFP_PROP_INT64_VIDEO_CACHED_DURATION:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.video_cache.duration;
//        case FFP_PROP_INT64_AUDIO_CACHED_DURATION:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.audio_cache.duration;
//        case FFP_PROP_INT64_VIDEO_CACHED_BYTES:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.video_cache.bytes;
//        case FFP_PROP_INT64_AUDIO_CACHED_BYTES:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.audio_cache.bytes;
//        case FFP_PROP_INT64_VIDEO_CACHED_PACKETS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.video_cache.packets;
//        case FFP_PROP_INT64_AUDIO_CACHED_PACKETS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.audio_cache.packets;
//        case FFP_PROP_INT64_BIT_RATE:
//            return ffp ? ffp->stat.bit_rate : default_value;
//        case FFP_PROP_INT64_TCP_SPEED:
//            return ffp ? SDL_SpeedSampler2GetSpeed(&ffp->stat.tcp_read_sampler) : default_value;
//        case FFP_PROP_INT64_ASYNC_STATISTIC_BUF_BACKWARDS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.buf_backwards;
//        case FFP_PROP_INT64_ASYNC_STATISTIC_BUF_FORWARDS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.buf_forwards;
//        case FFP_PROP_INT64_ASYNC_STATISTIC_BUF_CAPACITY:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.buf_capacity;
//        case FFP_PROP_INT64_LATEST_SEEK_LOAD_DURATION:
//            return ffp ? ffp->stat.latest_seek_load_duration : default_value;
//        case FFP_PROP_INT64_TRAFFIC_STATISTIC_BYTE_COUNT:
//            return ffp ? ffp->stat.byte_count : default_value;
//        case FFP_PROP_INT64_CACHE_STATISTIC_PHYSICAL_POS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.cache_physical_pos;
//       case FFP_PROP_INT64_CACHE_STATISTIC_FILE_FORWARDS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.cache_file_forwards;
//       case FFP_PROP_INT64_CACHE_STATISTIC_FILE_POS:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.cache_file_pos;
//       case FFP_PROP_INT64_CACHE_STATISTIC_COUNT_BYTES:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.cache_count_bytes;
//       case FFP_PROP_INT64_LOGICAL_FILE_SIZE:
//            if (!ffp)
//                return default_value;
//            return ffp->stat.logical_file_size;
//        default:
//            return default_value;
//    }
//}

//void ffp_set_property_int64(FFPlayer *ffp, int id, int64_t value)
//{
//    switch (id) {
//        // case FFP_PROP_INT64_SELECTED_VIDEO_STREAM:
//        // case FFP_PROP_INT64_SELECTED_AUDIO_STREAM:
//        case FFP_PROP_INT64_SHARE_CACHE_DATA:
//            if (ffp) {
//                if (value) {
//                    ijkio_manager_will_share_cache_map(ffp->ijkio_manager_ctx);
//                } else {
//                    ijkio_manager_did_share_cache_map(ffp->ijkio_manager_ctx);
//                }
//            }
//            break;
//        case FFP_PROP_INT64_IMMEDIATE_RECONNECT:
//            if (ffp) {
//                ijkio_manager_immediate_reconnect(ffp->ijkio_manager_ctx);
//            }
//        default:
//            break;
//    }
//}

//IjkMediaMeta *ffp_get_meta_l(FFPlayer *ffp)
//{
//    if (!ffp)
//        return NULL;

//    return ffp->meta;
//}
