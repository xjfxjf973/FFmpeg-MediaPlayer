#ifndef FFPLAY__FF_FFPLAY_DEF_H
#define FFPLAY__FF_FFPLAY_DEF_H

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

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
#include "SDL.h"
#include "SDL_thread.h"

#include <assert.h>

enum RET_CODER
{
    RET_ERR_UNKNOWN = -2,           //未知错误
    RET_FAIL = -1,                  //失收
    RET_OK = 0,                     //正常
    RET_ERR_OPEN_FILE,              //打开文件失败
    RET_ERR_NOT_SUPPORT,            //不支持
    RET_ERR_OUTOFMEMORY,            //没有内存
    RET_ERR_STACKOVERFLOW,          //溢出
    RET_ERR_NULLREFERENCE,          //空参考
    RET_ERR_ARGUMENTOUTOFRANGE,     //
    RET_ERR_PARAMISMATCH,           //
    RET_ERR_MISMATCH_CODE,          //没有匹配的编解码器
    RET_ERR_EAGAIN,
    RET_ERR_EOF
};

/*
 * START: buffering after prepared/seeked
 * NEXT:  buffering for the second time after START
 * MAX:   ...
 */
#define DEFAULT_FIRST_HIGH_WATER_MARK_IN_MS     (100)
#define DEFAULT_NEXT_HIGH_WATER_MARK_IN_MS      (1 * 1000)
#define DEFAULT_LAST_HIGH_WATER_MARK_IN_MS      (5 * 1000)

#define BUFFERING_CHECK_PER_BYTES               (512)
#define BUFFERING_CHECK_PER_MILLISECONDS        (500)
#define FAST_BUFFERING_CHECK_PER_MILLISECONDS   (50)
#define MAX_RETRY_CONVERT_IMAGE                 (3)

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MAX_ACCURATE_SEEK_TIMEOUT (5000)
#ifdef FFP_MERGE
#define MIN_FRAMES 25
#endif
#define DEFAULT_MIN_FRAMES  50000
#define MIN_MIN_FRAMES      2
#define MAX_MIN_FRAMES      50000
#define MIN_FRAMES (ffp->dcc.min_frames)
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
//#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define MIN_PKT_DURATION 15

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

typedef struct MyAVPacketList {
    AVPacket pkt;       //解封装后的数据
    struct MyAVPacketList *next;    //下一个节点
    int serial;    //播放序列
} MyAVPacketList;

typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;   //队首，队尾指针
    int nb_packets;                         //包数量，也就是队列元素数量
    int size;                               //队列所有元素的数据大小总和
    int64_t duration;                       //队列所有元素的数据播放持续时间
    int abort_request;                      //用户退出请求标志
    int serial;                             //播放序列号，和MyAVPacketlist的serial作用相同，但改变的时序稍微有点不同
    SDL_mutex *mutex;                       //互斥量，用于维持多线程安全
    SDL_cond *cond;                         //条件变量，用于读、写线程的相互通知
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3          //图像帧缓存数量
#define VIDEO_PICTURE_QUEUE_SIZE_MIN        (3)
#define VIDEO_PICTURE_QUEUE_SIZE_MAX        (16)
#define VIDEO_PICTURE_QUEUE_SIZE_DEFAULT    (VIDEO_PICTURE_QUEUE_SIZE_MIN)
#define SUBPICTURE_QUEUE_SIZE 16            //字幕帧缓存数量
#define SAMPLE_QUEUE_SIZE 9                 //采样帧缓存数量
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE_MAX, SUBPICTURE_QUEUE_SIZE))


typedef struct AudioParams {
    int freq;                       //采样率
    int channels;                   //通道数
    int64_t channel_layout;         //通道布局
    enum AVSampleFormat fmt;        //采样格式
    int frame_size;                 //一个采样单元占用的字节数
    int bytes_per_sec;              //一秒时间的字节数
} AudioParams;


/* Common struct for handling all types of decoded data and allocated render buffers. */
//用于解码后的数据缓存
typedef struct Frame {
    AVFrame *frame;         //指向数据帧
    double pts;             //时间戳，单位为秒
    double duration;        //该帧持续时间，单位秒
    int width;              //图像宽度
    int height;             //图像高度
    int format;             //对于图像为(enum AVPixelFormat)
} Frame;

//循环队列，windex指首元素，rindex指尾元素
typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];      //
    int rindex;                         //读索引。待播放时读取此帧进行播放，播放后此帧成为上一帧
    int windex;                         //写索引
    int size;                           //当前总帧数
    int max_size;                       //可存储最大帧数
    //int keep_last;
    SDL_mutex *mutex;                   //互斥量
    SDL_cond *cond;                     //条件变量
    PacketQueue *pktq;                  //数据包缓存队列
} FrameQueue;

//这里讲的系统时钟 是通过av_gettime_relative()获取到的时钟，单位为微秒
typedef struct Clock{
    double pts;     //时钟基础，当前帧(待播放)显示时间戳，播放后，当前帧变成上一帧
    //当前pts与当的系统时钟的差值，audio、video    对于该值是独立的
    double pts_drift;       //clock base minus time at which we updated the'clock
    //当前时钟(如视频时钟)最后一次更新时间，也可称当前时钟时间
    double last_updated;    //最后一次更新系统时钟
} Clock;

//音视频同步方式，缺省以音频为基准
enum{
    AV_SYNC_UNKNOW_MASTER = -1,
    AV_SYNC_AUDIO_MASTER,           //以音频为基准
    AV_SYNC_VIDEO_MASTER,           //以视频为基准
//    AV_SYNC_EXTERNAL_CLOCK        //以为外部时钟为基准
};


int packet_queue_put(PacketQueue *q, AVPacket *pkt);

int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);

int packet_queue_init(PacketQueue *q);

void packet_queue_flush(PacketQueue *q);

void packet_queue_destroy(PacketQueue *q);

void packet_queue_abort(PacketQueue *q);

void packet_queue_start(PacketQueue *q);

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);

int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size/*, int keep_last */);

void frame_queue_destory(FrameQueue *f);

void frame_queue_signal(FrameQueue *f);

Frame *frame_queue_peek(FrameQueue *f);

Frame *frame_queue_peek_next(FrameQueue *f);

Frame *frame_queue_peek_last(FrameQueue *f);

Frame *frame_queue_peek_writable(FrameQueue *f);

Frame *frame_queue_peek_readable(FrameQueue *f);

void frame_queue_push(FrameQueue *f);

void frame_queue_next(FrameQueue *f);

int frame_queue_nb_remaining(FrameQueue *f);

int64_t frame_queue_last_pos(FrameQueue *f);

//时钟相关
double get_clock(Clock *c);
void set_clock_at(Clock *c,double pts,double time);
void set_clock(Clock *c,double pts);
void init_clock(Clock *c);

//typedef struct VideoState {
//    SDL_Thread *read_tid;
//    SDL_Thread _read_tid;
//    AVInputFormat *iformat;
//    int abort_request;
//    int force_refresh;
//    int paused;
//    int last_paused;
//    int queue_attachments_req;
//    int seek_req;
//    int seek_flags;
//    int64_t seek_pos;
//    int64_t seek_rel;
//#ifdef FFP_MERGE
//    int read_pause_return;
//#endif
//    AVFormatContext *ic;
//    int realtime;

//    Clock audclk;
//    Clock vidclk;
//    Clock extclk;

//    FrameQueue pictq;
//    FrameQueue subpq;
//    FrameQueue sampq;

//    Decoder auddec;
//    Decoder viddec;
//    Decoder subdec;

//    int audio_stream;

//    int av_sync_type;
//    void *handle;
//    double audio_clock;
//    int audio_clock_serial;
//    double audio_diff_cum; /* used for AV difference average computation */
//    double audio_diff_avg_coef;
//    double audio_diff_threshold;
//    int audio_diff_avg_count;
//    AVStream *audio_st;
//    PacketQueue audioq;
//    int audio_hw_buf_size;
//    uint8_t *audio_buf;
//    uint8_t *audio_buf1;
//    short *audio_new_buf;  /* for soundtouch buf */
//    unsigned int audio_buf_size; /* in bytes */
//    unsigned int audio_buf1_size;
//    unsigned int audio_new_buf_size;
//    int audio_buf_index; /* in bytes */
//    int audio_write_buf_size;
//    int audio_volume;
//    int muted;
//    struct AudioParams audio_src;
//#if CONFIG_AVFILTER
//    struct AudioParams audio_filter_src;
//#endif
//    struct AudioParams audio_tgt;
//    struct SwrContext *swr_ctx;
//    int frame_drops_early;
//    int frame_drops_late;
//    int continuous_frame_drops_early;

//    enum ShowMode {
//        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
//    } show_mode;
//    int16_t sample_array[SAMPLE_ARRAY_SIZE];
//    int sample_array_index;
//    int last_i_start;
//#ifdef FFP_MERGE
//    RDFTContext *rdft;
//    int rdft_bits;
//    FFTSample *rdft_data;
//    int xpos;
//#endif
//    double last_vis_time;
//#ifdef FFP_MERGE
//    SDL_Texture *vis_texture;
//    SDL_Texture *sub_texture;
//#endif

//    int subtitle_stream;
//    AVStream *subtitle_st;
//    PacketQueue subtitleq;

//    double frame_timer;
//    double frame_last_returned_time;
//    double frame_last_filter_delay;
//    int video_stream;
//    AVStream *video_st;
//    PacketQueue videoq;
//    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
//    struct SwsContext *img_convert_ctx;
//#ifdef FFP_SUB
//    struct SwsContext *sub_convert_ctx;
//#endif
//    int eof;

//    char *filename;
//    int width, height, xleft, ytop;
//    int step;

//#if CONFIG_AVFILTER
//    int vfilter_idx;
//    AVFilterContext *in_video_filter;   // the first filter in the video chain
//    AVFilterContext *out_video_filter;  // the last filter in the video chain
//    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
//    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
//    AVFilterGraph *agraph;              // audio filter graph
//#endif

//    int last_video_stream, last_audio_stream, last_subtitle_stream;

//    SDL_cond *continue_read_thread;

//    /* extra fields */
//    SDL_mutex  *play_mutex; // only guard state, do not block any long operation
//    SDL_Thread *video_refresh_tid;
//    SDL_Thread _video_refresh_tid;

//    int buffering_on;
//    int pause_req;

//    int dropping_frame;
//    int is_video_high_fps; // above 30fps
//    int is_video_high_res; // above 1080p

//    PacketQueue *buffer_indicator_queue;

//    volatile int latest_video_seek_load_serial;
//    volatile int latest_audio_seek_load_serial;
//    volatile int64_t latest_seek_load_start_at;

//    int drop_aframe_count;
//    int drop_vframe_count;
//    int64_t accurate_seek_start_time;
//    volatile int64_t accurate_seek_vframe_pts;
//    volatile int64_t accurate_seek_aframe_pts;
//    int audio_accurate_seek_req;
//    int video_accurate_seek_req;
//    SDL_mutex *accurate_seek_mutex;
//    SDL_cond  *video_accurate_seek_cond;
//    SDL_cond  *audio_accurate_seek_cond;
//    volatile int initialized_decoder;
//    int seek_buffering;
//} VideoState;

///* options specified by the user */
//#ifdef FFP_MERGE
//static AVInputFormat *file_iformat;
//static const char *input_filename;
//static const char *window_title;
//static int default_width  = 640;
//static int default_height = 480;
//static int screen_width  = 0;
//static int screen_height = 0;
//static int audio_disable;
//static int video_disable;
//static int subtitle_disable;
//static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
//static int seek_by_bytes = -1;
//static int display_disable;
//static int show_status = 1;
//static int av_sync_type = AV_SYNC_AUDIO_MASTER;
//static int64_t start_time = AV_NOPTS_VALUE;
//static int64_t duration = AV_NOPTS_VALUE;
//static int fast = 0;
//static int genpts = 0;
//static int lowres = 0;
//static int decoder_reorder_pts = -1;
//static int autoexit;
//static int exit_on_keydown;
//static int exit_on_mousedown;
//static int loop = 1;
//static int framedrop = -1;
//static int infinite_buffer = -1;
//static enum ShowMode show_mode = SHOW_MODE_NONE;
//static const char *audio_codec_name;
//static const char *subtitle_codec_name;
//static const char *video_codec_name;
//double rdftspeed = 0.02;
//static int64_t cursor_last_shown;
//static int cursor_hidden = 0;
//#if CONFIG_AVFILTER
//static const char **vfilters_list = NULL;
//static int nb_vfilters = 0;
//static char *afilters = NULL;
//#endif
//static int autorotate = 1;
//static int find_stream_info = 1;

///* current context */
//static int is_full_screen;
//static int64_t audio_callback_time;

//static AVPacket flush_pkt;
//static AVPacket eof_pkt;

//#define FF_ALLOC_EVENT   (SDL_USEREVENT)
//#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

//static SDL_Window *window;
//static SDL_Renderer *renderer;
//#endif

///*****************************************************************************
// * end at line 330 in ffplay.c
// * near packet_queue_put
// ****************************************************************************/
//typedef struct FFTrackCacheStatistic
//{
//    int64_t duration;
//    int64_t bytes;
//    int64_t packets;
//} FFTrackCacheStatistic;

//typedef struct FFStatistic
//{
//    int64_t vdec_type;

//    float vfps;
//    float vdps;
//    float avdelay;
//    float avdiff;
//    int64_t bit_rate;

//    FFTrackCacheStatistic video_cache;
//    FFTrackCacheStatistic audio_cache;

//    int64_t buf_backwards;
//    int64_t buf_forwards;
//    int64_t buf_capacity;
//    SDL_SpeedSampler2 tcp_read_sampler;
//    int64_t latest_seek_load_duration;
//    int64_t byte_count;
//    int64_t cache_physical_pos;
//    int64_t cache_file_forwards;
//    int64_t cache_file_pos;
//    int64_t cache_count_bytes;
//    int64_t logical_file_size;
//    int drop_frame_count;
//    int decode_frame_count;
//    float drop_frame_rate;
//} FFStatistic;

//#define FFP_TCP_READ_SAMPLE_RANGE 2000
//inline static void ffp_reset_statistic(FFStatistic *dcc)
//{
//    memset(dcc, 0, sizeof(FFStatistic));
//    SDL_SpeedSampler2Reset(&dcc->tcp_read_sampler, FFP_TCP_READ_SAMPLE_RANGE);
//}

//typedef struct FFDemuxCacheControl
//{
//    int min_frames;
//    int max_buffer_size;
//    int high_water_mark_in_bytes;

//    int first_high_water_mark_in_ms;
//    int next_high_water_mark_in_ms;
//    int last_high_water_mark_in_ms;
//    int current_high_water_mark_in_ms;
//} FFDemuxCacheControl;

//inline static void ffp_reset_demux_cache_control(FFDemuxCacheControl *dcc)
//{
//    dcc->min_frames                = DEFAULT_MIN_FRAMES;
//    dcc->max_buffer_size           = MAX_QUEUE_SIZE;
//    dcc->high_water_mark_in_bytes  = DEFAULT_HIGH_WATER_MARK_IN_BYTES;

//    dcc->first_high_water_mark_in_ms    = DEFAULT_FIRST_HIGH_WATER_MARK_IN_MS;
//    dcc->next_high_water_mark_in_ms     = DEFAULT_NEXT_HIGH_WATER_MARK_IN_MS;
//    dcc->last_high_water_mark_in_ms     = DEFAULT_LAST_HIGH_WATER_MARK_IN_MS;
//    dcc->current_high_water_mark_in_ms  = DEFAULT_FIRST_HIGH_WATER_MARK_IN_MS;
//}

//#define fftime_to_milliseconds(ts) (av_rescale(ts, 1000, AV_TIME_BASE))
//#define milliseconds_to_fftime(ms) (av_rescale(ms, AV_TIME_BASE, 1000))

//inline static void ffp_reset_internal(FFPlayer *ffp)
//{
//    /* ffp->is closed in stream_close() */
//    av_opt_free(ffp);

//    /* format/codec options */
//    av_dict_free(&ffp->format_opts);
//    av_dict_free(&ffp->codec_opts);
//    av_dict_free(&ffp->sws_dict);
//    av_dict_free(&ffp->player_opts);
//    av_dict_free(&ffp->swr_opts);
//    av_dict_free(&ffp->swr_preset_opts);

//    /* ffplay options specified by the user */
//    av_freep(&ffp->input_filename);
//    ffp->audio_disable          = 0;
//    ffp->video_disable          = 0;
//    memset(ffp->wanted_stream_spec, 0, sizeof(ffp->wanted_stream_spec));
//    ffp->seek_by_bytes          = -1;
//    ffp->display_disable        = 0;
//    ffp->show_status            = 0;
//    ffp->av_sync_type           = AV_SYNC_AUDIO_MASTER;
//    ffp->start_time             = AV_NOPTS_VALUE;
//    ffp->duration               = AV_NOPTS_VALUE;
//    ffp->fast                   = 1;
//    ffp->genpts                 = 0;
//    ffp->lowres                 = 0;
//    ffp->decoder_reorder_pts    = -1;
//    ffp->autoexit               = 0;
//    ffp->loop                   = 1;
//    ffp->framedrop              = 0; // option
//    ffp->seek_at_start          = 0;
//    ffp->infinite_buffer        = -1;
//    ffp->show_mode              = SHOW_MODE_NONE;
//    av_freep(&ffp->audio_codec_name);
//    av_freep(&ffp->video_codec_name);
//    ffp->rdftspeed              = 0.02;
//#if CONFIG_AVFILTER
//    av_freep(&ffp->vfilters_list);
//    ffp->nb_vfilters            = 0;
//    ffp->afilters               = NULL;
//    ffp->vfilter0               = NULL;
//#endif
//    ffp->autorotate             = 1;
//    ffp->find_stream_info       = 1;

//    ffp->sws_flags              = SWS_FAST_BILINEAR;

//    /* current context */
//    ffp->audio_callback_time    = 0;

//    /* extra fields */
//    ffp->aout                   = NULL; /* reset outside */
//    ffp->vout                   = NULL; /* reset outside */
//    ffp->pipeline               = NULL;
//    ffp->node_vdec              = NULL;
//    ffp->sar_num                = 0;
//    ffp->sar_den                = 0;

//    av_freep(&ffp->video_codec_info);
//    av_freep(&ffp->audio_codec_info);
//    av_freep(&ffp->subtitle_codec_info);
//    ffp->overlay_format         = SDL_FCC_RV32;

//    ffp->last_error             = 0;
//    ffp->prepared               = 0;
//    ffp->auto_resume            = 0;
//    ffp->error                  = 0;
//    ffp->error_count            = 0;
//    ffp->start_on_prepared      = 1;
//    ffp->first_video_frame_rendered = 0;
//    ffp->sync_av_start          = 1;
//    ffp->enable_accurate_seek   = 0;
//    ffp->accurate_seek_timeout  = MAX_ACCURATE_SEEK_TIMEOUT;

//    ffp->playable_duration_ms           = 0;

//    ffp->packet_buffering               = 1;
//    ffp->pictq_size                     = VIDEO_PICTURE_QUEUE_SIZE_DEFAULT; // option
//    ffp->max_fps                        = 31; // option

//    ffp->videotoolbox                   = 0; // option
//    ffp->vtb_max_frame_width            = 0; // option
//    ffp->vtb_async                      = 0; // option
//    ffp->vtb_handle_resolution_change   = 0; // option
//    ffp->vtb_wait_async                 = 0; // option

//    ffp->mediacodec_all_videos          = 0; // option
//    ffp->mediacodec_avc                 = 0; // option
//    ffp->mediacodec_hevc                = 0; // option
//    ffp->mediacodec_mpeg2               = 0; // option
//    ffp->mediacodec_handle_resolution_change = 0; // option
//    ffp->mediacodec_auto_rotate         = 0; // option

//    ffp->opensles                       = 0; // option
//    ffp->soundtouch_enable              = 0; // option

//    ffp->iformat_name                   = NULL; // option

//    ffp->no_time_adjust                 = 0; // option
//    ffp->async_init_decoder             = 0; // option
//    ffp->video_mime_type                = NULL; // option
//    ffp->mediacodec_default_name        = NULL; // option
//    ffp->ijkmeta_delay_init             = 0; // option
//    ffp->render_wait_start              = 0;

//    ijkmeta_reset(ffp->meta);

//    SDL_SpeedSamplerReset(&ffp->vfps_sampler);
//    SDL_SpeedSamplerReset(&ffp->vdps_sampler);

//    /* filters */
//    ffp->vf_changed                     = 0;
//    ffp->af_changed                     = 0;
//    ffp->pf_playback_rate               = 1.0f;
//    ffp->pf_playback_rate_changed       = 0;
//    ffp->pf_playback_volume             = 1.0f;
//    ffp->pf_playback_volume_changed     = 0;

//    av_application_closep(&ffp->app_ctx);
//    ijkio_manager_destroyp(&ffp->ijkio_manager_ctx);

//    msg_queue_flush(&ffp->msg_queue);

//    ffp->inject_opaque = NULL;
//    ffp->ijkio_inject_opaque = NULL;
//    ffp_reset_statistic(&ffp->stat);
//    ffp_reset_demux_cache_control(&ffp->dcc);
//}

//inline static const char *ffp_get_error_string(int error) {
//    switch (error) {
//        case AVERROR(ENOMEM):       return "AVERROR(ENOMEM)";       // 12
//        case AVERROR(EINVAL):       return "AVERROR(EINVAL)";       // 22
//        case AVERROR(EAGAIN):       return "AVERROR(EAGAIN)";       // 35
//        case AVERROR(ETIMEDOUT):    return "AVERROR(ETIMEDOUT)";    // 60
//        case AVERROR_EOF:           return "AVERROR_EOF";
//        case AVERROR_EXIT:          return "AVERROR_EXIT";
//    }
//    return "unknown";
//}

//#define FFTRACE ALOGW

//#define AVCODEC_MODULE_NAME    "avcodec"
//#define MEDIACODEC_MODULE_NAME "MediaCodec"

#endif
