/*
 * ff_ffplaye_def.h
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FFPLAY__FF_FFPLAY_DEF_H
#define FFPLAY__FF_FFPLAY_DEF_H

#include "ff_ffinc.h"
#include "ff_ffplay_config.h"
#include "ff_ffmsg_queue.h"

#define DEFAULT_HIGH_WATER_MARK_IN_BYTES        (256 * 1024)

/*
 * START: buffering after prepared/seeked
 * NEXT:  buffering for the second time after START
 * MAX:   ...
 */
#define DEFAULT_START_HIGH_WATER_MARK_IN_MS     (100)
#define DEFAULT_NEXT_HIGH_WATER_MARK_IN_MS      (1 * 1000)
#define DEFAULT_MAX_HIGH_WATER_MARK_IN_MS       (5 * 1000)

#define BUFFERING_CHECK_PER_BYTES               (512)
#define BUFFERING_CHECK_PER_MILLISECONDS        (500)

#define MAX_QUEUE_SIZE (10 * 1024 * 1024)
#define MIN_FRAMES 50000

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 100.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
// 172.0 fps at most (High Level 5.2 1,920×1,080@172.0)
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#ifdef FFP_MERGE
#define CURSOR_HIDE_DELAY 1000000

static int64_t sws_flags = SWS_BICUBIC;
#endif

typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;

typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
    int abort_request;
    int serial;
    SDL_mutex *mutex;
    SDL_cond *cond;
    MyAVPacketList *recycle_pkt;
    int recycle_count;
    int alloc_count;
} PacketQueue;

// #define VIDEO_PICTURE_QUEUE_SIZE 3
#define VIDEO_PICTURE_QUEUE_SIZE_MIN        (3)
#define VIDEO_PICTURE_QUEUE_SIZE_MAX        (16)
#define VIDEO_PICTURE_QUEUE_SIZE_DEFAULT    (VIDEO_PICTURE_QUEUE_SIZE_MIN)
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE_MAX, SUBPICTURE_QUEUE_SIZE))

#define VIDEO_MAX_FPS_DEFAULT 30

typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

typedef struct Clock {
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame *frame;
    AVSubtitle sub;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    SDL_VoutOverlay *bmp;
    int allocated;
    int reallocate;
    int width;
    int height;
    AVRational sar;
} Frame;

typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;
} FrameQueue;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
    AVPacket pkt;
    AVPacket pkt_temp;
    PacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    int flushed;
    int packet_pending;
    SDL_cond *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
} Decoder;

typedef struct VideoState {
    SDL_Thread *read_tid;
    SDL_Thread _read_tid;
    SDL_Thread *video_tid;
    SDL_Thread _video_tid;
    SDL_Thread *audio_tid;
    SDL_Thread _audio_tid;
    AVInputFormat *iformat;
    int no_background;
    int abort_request;
    int force_refresh;
    int paused;
    int last_paused;
    int queue_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
#ifdef FFP_MERGE
    int read_pause_return;
#endif
    AVFormatContext *ic;
    int realtime;

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;
#ifdef FFP_MERGE
    FrameQueue subpq;
#endif
    FrameQueue sampq;

    Decoder auddec;
    Decoder viddec;
#ifdef FFP_MERGE
    Decoder subdec;
#endif

    int audio_stream;

    int av_sync_type;

    double audio_clock;
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int64_t audioq_duration;
    int audio_hw_buf_size;
    uint8_t silence_buf[SDL_AUDIO_MIN_BUFFER_SIZE];
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    struct AudioParams audio_src;
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
    struct AudioParams audio_tgt;
    struct SwrContext *swr_ctx;
    int frame_drops_early;
    int frame_drops_late;
    int continuous_frame_drops_early;

    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
#ifdef FFP_MERGE
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
#endif
    double last_vis_time;

#ifdef FFP_MERGE
    SDL_Thread *subtitle_tid;
    int subtitle_stream;
    AVStream *subtitle_st;
    PacketQueue subtitleq;
#endif

    double frame_timer;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    int64_t videoq_duration;
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
#if !CONFIG_AVFILTER
    struct SwsContext *img_convert_ctx;
#endif
#ifdef FFP_MERGE
    SDL_Rect last_display_rect;
#endif

    char filename[1024];
    int width, height, xleft, ytop;
    int step;

#if CONFIG_AVFILTER
    int vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph *agraph;              // audio filter graph
#endif

    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SDL_cond *continue_read_thread;

    /* extra fields */
    SDL_mutex  *play_mutex; // only guard state, do not block any long operation
    SDL_Thread *video_refresh_tid;
    SDL_Thread _video_refresh_tid;

    int buffering_on;
    int pause_req;

    int dropping_frame;
    int is_video_high_fps; // above 30fps
    int is_video_high_res; // above 1080p
} VideoState;

/* options specified by the user */
#ifdef FFP_MERGE
static AVInputFormat *file_iformat;
static const char *input_filename;
static const char *window_title;
static int fs_screen_width;
static int fs_screen_height;
static int default_width  = 640;
static int default_height = 480;
static int screen_width  = 0;
static int screen_height = 0;
static int audio_disable;
static int video_disable;
static int subtitle_disable;
static int wanted_stream[AVMEDIA_TYPE_NB] = {
    [AVMEDIA_TYPE_AUDIO]    = -1,
    [AVMEDIA_TYPE_VIDEO]    = -1,
    [AVMEDIA_TYPE_SUBTITLE] = -1,
};
static int seek_by_bytes = -1;
static int display_disable;
static int show_status = 1;
static int av_sync_type = AV_SYNC_AUDIO_MASTER;
static int64_t start_time = AV_NOPTS_VALUE;
static int64_t duration = AV_NOPTS_VALUE;
static int fast = 0;
static int genpts = 0;
static int lowres = 0;
static int decoder_reorder_pts = -1;
static int autoexit;
static int exit_on_keydown;
static int exit_on_mousedown;
static int loop = 1;
static int framedrop = -1;
static int infinite_buffer = -1;
static enum ShowMode show_mode = SHOW_MODE_NONE;
static const char *audio_codec_name;
static const char *subtitle_codec_name;
static const char *video_codec_name;
double rdftspeed = 0.02;
static int64_t cursor_last_shown;
static int cursor_hidden = 0;
#if CONFIG_AVFILTER
static const char **vfilters_list = NULL;
static int nb_vfilters = 0;
static char *afilters = NULL;
#endif
static int autorotate = 1;

/* current context */
static int is_full_screen;
static int64_t audio_callback_time;

static AVPacket flush_pkt;
static AVPacket eof_pkt;

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static SDL_Surface *screen;
#endif

/*****************************************************************************
 * end at line 330 in ffplay.c
 * near packet_queue_put
 ****************************************************************************/

/* ffplayer */
typedef struct IJKFF_Pipeline IJKFF_Pipeline;
typedef struct FFPlayer {
    /* ffplay context */
    VideoState *is;

    /* format/codec options */
    AVDictionary *format_opts;
    AVDictionary *codec_opts;
    AVDictionary *sws_opts;

    /* ffplay options specified by the user */
#ifdef FFP_MERGE
    AVInputFormat *file_iformat;
#endif
    char *input_filename;
#ifdef FFP_MERGE
    const char *window_title;
    int fs_screen_width;
    int fs_screen_height;
    int default_width;
    int default_height;
    int screen_width;
    int screen_height;
#endif
    int audio_disable;
    int video_disable;
#ifdef FFP_MERGE
    int subtitle_disable;
#endif
    int wanted_stream[AVMEDIA_TYPE_NB];
    int seek_by_bytes;
    int display_disable;
    int show_status;
    int av_sync_type;
    int64_t start_time;
    int64_t duration;
    int fast;
    int genpts;
    int lowres;
    int decoder_reorder_pts;
    int autoexit;
#ifdef FFP_MERGE
    int exit_on_keydown;
    int exit_on_mousedown;
#endif
    int loop;
    int framedrop;
    int infinite_buffer;
    enum ShowMode show_mode;
    char *audio_codec_name;
#ifdef FFP_MERGE
    char *subtitle_codec_name;
#endif
    char *video_codec_name;
    double rdftspeed;
#ifdef FFP_MERGE
    int64_t cursor_last_shown;
    int cursor_hidden;
#endif
#if CONFIG_AVFILTER
    const char **vfilters_list;
    int nb_vfilters;
    char *afilters;
#endif
    int autorotate;

    int sws_flags;

    /* current context */
#ifdef FFP_MERGE
    int is_full_screen;
#endif
    int64_t audio_callback_time;
#ifdef FFP_MERGE
    SDL_Surface *screen;
#endif

    /* extra fields */
    SDL_Aout *aout;
    SDL_Vout *vout;
    IJKFF_Pipeline *pipeline;
    int sar_num;
    int sar_den;

    Uint32 overlay_format;

    int last_error;
    int prepared;
    int auto_start;
    int error;
    int error_count;

    MessageQueue msg_queue;

    int max_buffer_size;
    int high_water_mark_in_bytes;

    int start_high_water_mark_in_ms;
    int next_high_water_mark_in_ms;
    int max_high_water_mark_in_ms;
    int current_high_water_mark_in_ms;

    int64_t playable_duration_ms;

    int pictq_size;
    int max_fps;

    ijk_format_control_message format_control_message;
    void *format_control_opaque;
} FFPlayer;

#define fftime_to_milliseconds(ts) (av_rescale(ts, 1000, AV_TIME_BASE));
#define milliseconds_to_fftime(ms) (av_rescale(ms, AV_TIME_BASE, 1000));

inline static void ffp_reset_internal(FFPlayer *ffp)
{
    /* ffp->is closed in stream_close() */

    /* format/codec options */
    av_dict_free(&ffp->format_opts);
    av_dict_free(&ffp->codec_opts);
    av_dict_free(&ffp->sws_opts);

    /* ffplay options specified by the user */
    av_freep(&ffp->input_filename);
    ffp->audio_disable          = 0;
    ffp->video_disable          = 0;
    ffp->wanted_stream[AVMEDIA_TYPE_AUDIO]      = -1;
    ffp->wanted_stream[AVMEDIA_TYPE_VIDEO]      = -1;
    ffp->wanted_stream[AVMEDIA_TYPE_SUBTITLE]   = -1;
    ffp->seek_by_bytes          = -1;
    ffp->display_disable        = 0;
    ffp->show_status            = 0;
    ffp->av_sync_type           = AV_SYNC_AUDIO_MASTER;
    ffp->start_time             = AV_NOPTS_VALUE;
    ffp->duration               = AV_NOPTS_VALUE;
    ffp->fast                   = 1;
    ffp->genpts                 = 0;
    ffp->lowres                 = 0;
    ffp->decoder_reorder_pts    = -1;
    ffp->autoexit               = 0;
    ffp->loop                   = 1;
    ffp->framedrop              = 0;
    ffp->infinite_buffer        = -1;
    ffp->show_mode              = SHOW_MODE_NONE;
    av_freep(&ffp->audio_codec_name);
    av_freep(&ffp->video_codec_name);
    ffp->rdftspeed              = 0.02;
#if CONFIG_AVFILTER
    ffp->vfilters_list          = NULL;
    ffp->nb_vfilters            = 0;
    ffp->afilters               = NULL;
#endif
    ffp->autorotate             = 1;

    // ffp->sws_flags              = SWS_BICUBIC;
    ffp->sws_flags              = SWS_FAST_BILINEAR;

    /* current context */
    ffp->audio_callback_time    = 0;

    /* extra fields */
    ffp->aout                   = NULL; /* reset outside */
    ffp->vout                   = NULL; /* reset outside */
    ffp->pipeline               = NULL;
    ffp->sar_num                = 0;
    ffp->sar_den                = 0;

    // ffp->overlay_format         = SDL_FCC_YV12;
    ffp->overlay_format         = SDL_FCC_RV32;
    // ffp->overlay_format         = SDL_FCC_RV16;

    ffp->last_error             = 0;
    ffp->prepared               = 0;
    ffp->auto_start             = 0;
    ffp->error                  = 0;
    ffp->error_count            = 0;

    ffp->max_buffer_size                = MAX_QUEUE_SIZE;
    ffp->high_water_mark_in_bytes       = DEFAULT_HIGH_WATER_MARK_IN_BYTES;

    ffp->start_high_water_mark_in_ms    = DEFAULT_START_HIGH_WATER_MARK_IN_MS;
    ffp->next_high_water_mark_in_ms     = DEFAULT_NEXT_HIGH_WATER_MARK_IN_MS;
    ffp->max_high_water_mark_in_ms      = DEFAULT_MAX_HIGH_WATER_MARK_IN_MS;
    ffp->current_high_water_mark_in_ms  = DEFAULT_START_HIGH_WATER_MARK_IN_MS;

    ffp->playable_duration_ms           = 0;

    ffp->pictq_size                     = VIDEO_PICTURE_QUEUE_SIZE_DEFAULT;
    ffp->max_fps                        = VIDEO_MAX_FPS_DEFAULT;

    ffp->format_control_message = NULL;
    ffp->format_control_opaque  = NULL;

    msg_queue_flush(&ffp->msg_queue);
}

inline static void ffp_notify_msg1(FFPlayer *ffp, int what) {
    msg_queue_put_simple3(&ffp->msg_queue, what, 0, 0);
}

inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1) {
    msg_queue_put_simple3(&ffp->msg_queue, what, arg1, 0);
}

inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2) {
    msg_queue_put_simple3(&ffp->msg_queue, what, arg1, arg2);
}

inline static void ffp_remove_msg(FFPlayer *ffp, int what) {
    msg_queue_remove(&ffp->msg_queue, what);
}

#define FFTRACE ALOGW

#endif
