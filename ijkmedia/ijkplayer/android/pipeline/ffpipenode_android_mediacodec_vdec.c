/*
 * ffpipenode_android_mediacodec_vdec.c
 *
 * Copyright (c) 2014 Zhang Rui <bbcallen@gmail.com>
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

#include "ffpipenode_android_mediacodec_vdec.h"
#include "ffpipeline_android.h"
#include "../../ff_ffpipenode.h"
#include "../../ff_ffplay.h"
#include "ijksdl/android/ijksdl_android_jni.h"
#include "ijksdl/android/ijksdl_codec_android_mediaformat_java.h"
#include "ijksdl/android/ijksdl_codec_android_mediacodec_java.h"

typedef struct IJKFF_Pipenode_Opaque {
    IJKFF_Pipeline           *pipeline;
    FFPlayer                 *ffp;
    Decoder                  *decoder;

    const char               *codec_mime;

    SDL_AMediaFormat         *aformat;
    SDL_AMediaCodec          *acodec;

    AVCodecContext           *avctx; // not own
    AVBitStreamFilterContext *bsfc;  // own

    uint8_t                  *orig_extradata;
    int                       orig_extradata_size;

    SDL_Thread               _enqueue_thread;
    SDL_Thread               *enqueue_thread;

    int                       abort_request;
} IJKFF_Pipenode_Opaque;

static int enqueue_thread_func(void *arg)
{
    JNIEnv                *env      = NULL;
    IJKFF_Pipenode        *node     = arg;
    IJKFF_Pipenode_Opaque *opaque   = node->opaque;
    FFPlayer              *ffp      = opaque->ffp;
    IJKFF_Pipeline        *pipeline = opaque->pipeline;
    VideoState            *is       = ffp->is;
    jobject                jsurface = NULL;
    Decoder               *d        = &is->viddec;

    sdl_amedia_status_t    amc_ret  = 0;
    int                    ret      = 0;
    // AVRational             tb       = is->video_st->time_base;
    // AVRational             frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

    if (JNI_OK != SDL_JNI_SetupThreadEnv(&env)) {
        ALOGE("%s: SetupThreadEnv failed\n", __func__);
        return -1;
    }

    ffpipeline_set_surface_need_reconfigure(pipeline, true);
    while (1) {
        if (d->queue->abort_request) {
            ret = 0;
            goto fail;
        }

        if (!d->packet_pending || d->queue->serial != d->pkt_serial) {
            AVPacket pkt;
            do {
                if (d->queue->nb_packets == 0)
                    SDL_CondSignal(d->empty_queue_cond);
                if (ffp_packet_queue_get_or_buffering(ffp, d->queue, &pkt, &d->pkt_serial, &d->finished) < 0) {
                    ret = -1;
                    goto fail;
                }
                if (ffp_is_flush_packet(ffp, &pkt)) {
                    if (!ffpipeline_is_surface_need_reconfigure(pipeline))
                        SDL_AMediaCodec_flush(opaque->acodec);
                    d->finished = 0;
                    d->flushed = 1;
                    d->next_pts = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                }
            } while (ffp_is_flush_packet(ffp, &pkt) || d->queue->serial != d->pkt_serial);
            av_free_packet(&d->pkt);
            d->pkt_temp = d->pkt = pkt;
            d->packet_pending = 1;
        }

        if (d->pkt_temp.data) {
            ssize_t  input_buffer_index = 0;
            uint8_t* input_buffer_ptr   = NULL;
            size_t   input_buffer_size  = 0;
            size_t   copy_size          = 0;
            int64_t  time_stamp         = 0;

            // reconfigure surface if surface changed
            // NULL surface cause no display
            if (ffpipeline_is_surface_need_reconfigure(pipeline)) {

                SDL_JNI_DeleteLocalRefP(env, &jsurface);
                jsurface = ffpipeline_get_surface_as_local_ref(env, pipeline);

                // need lock
                if (!SDL_AMediaCodec_is_configured(opaque->acodec)) {
                    if (opaque->acodec) {
                        SDL_AMediaCodec_deleteP(&opaque->acodec);
                    }

                    opaque->acodec = SDL_AMediaCodecJava_createDecoderByType(env, opaque->codec_mime);
                    if (!opaque->acodec) {
                        ALOGE("%s:open_video_decoder: SDL_AMediaCodecJava_createDecoderByType failed\n", __func__);
                        ret = -1;
                        goto fail;
                    }
                }

                amc_ret = SDL_AMediaCodec_configure_surface(env, opaque->acodec, opaque->aformat, jsurface, NULL, 0);
                if (amc_ret != SDL_AMEDIA_OK) {
                    ret = -1;
                    goto fail;
                }
                ffpipeline_set_surface_need_reconfigure(pipeline, false);

                SDL_AMediaCodec_start(opaque->acodec);
            }

            input_buffer_index = SDL_AMediaCodec_dequeueInputBuffer(opaque->acodec, -1);
            if (input_buffer_index < 0) {
                ALOGE("%s: SDL_AMediaCodec_dequeueInputBuffer failed\n", __func__);
                ret = -1;
                goto fail;
            }

            input_buffer_ptr = SDL_AMediaCodec_getInputBuffer(opaque->acodec, input_buffer_index, &input_buffer_size);
            if (!input_buffer_ptr) {
                ALOGE("%s: SDL_AMediaCodec_getInputBuffer failed\n", __func__);
                ret = -1;
                goto fail;
            }

            copy_size = FFMIN(input_buffer_size, d->pkt_temp.size);
            memcpy(input_buffer_ptr, d->pkt_temp.data, copy_size);

            time_stamp = d->pkt_temp.pts;
            if (!time_stamp && d->pkt_temp.dts)
                time_stamp = d->pkt_temp.dts;
            amc_ret = SDL_AMediaCodec_queueInputBuffer(opaque->acodec, input_buffer_index, 0, copy_size, d->pkt_temp.dts, 0);
            if (amc_ret != SDL_AMEDIA_OK) {
                ret = -1;
                goto fail;
            }

            if (input_buffer_size < 0) {
                d->packet_pending = 0;
            } else {
                d->pkt_temp.dts =
                d->pkt_temp.pts = AV_NOPTS_VALUE;
                if (d->pkt_temp.data) {
                    d->pkt_temp.data += input_buffer_size;
                    d->pkt_temp.size -= input_buffer_size;
                    if (d->pkt_temp.size <= 0)
                        d->packet_pending = 0;
                } else {
                    // FIXME: detect if decode finished
                    // if (!got_frame) {
                        d->packet_pending = 0;
                        d->finished = d->pkt_serial;
                    // }
                }
            }
        }
    }

fail:
    SDL_AMediaCodec_stop(opaque->acodec);

    SDL_JNI_DeleteLocalRefP(env, &jsurface);
    return ret;
}

static void func_destroy(IJKFF_Pipenode *node)
{
    if (!node || !node->opaque)
        return;

    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    SDL_AMediaCodec_deleteP(&opaque->acodec);
    SDL_AMediaFormat_deleteP(&opaque->aformat);

    av_freep(&opaque->orig_extradata);

    if (opaque->bsfc) {
        av_bitstream_filter_close(opaque->bsfc);
        opaque->bsfc = NULL;
    }
}

static int func_run_sync(IJKFF_Pipenode *node)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;

    opaque->enqueue_thread = SDL_CreateThreadEx(&opaque->_enqueue_thread, enqueue_thread_func, node, "acodec_vdec_enqueue_thread");
    if (!opaque->enqueue_thread)
        goto fallback_to_ffplay;

    SDL_WaitThread(opaque->enqueue_thread, NULL);
    return 0;
fallback_to_ffplay:
    ALOGW("fallback to ffplay decoder\n");
    return ffp_video_thread(opaque->ffp);
}

IJKFF_Pipenode *ffpipenode_create_video_decoder_from_android_mediacodec(FFPlayer *ffp, IJKFF_Pipeline *pipeline)
{
    ALOGD("ffpipenode_create_video_decoder_from_android_mediacodec()\n");
    if (SDL_Android_GetApiLevel() < IJK_API_16_JELLY_BEAN)
        return NULL;

    if (!ffp || !ffp->is)
        return NULL;

    IJKFF_Pipenode *node = ffpipenode_alloc(sizeof(IJKFF_Pipenode_Opaque));
    if (!node)
        return node;

    VideoState            *is         = ffp->is;
    IJKFF_Pipenode_Opaque *opaque     = node->opaque;
    JNIEnv                *env        = NULL;

    node->func_destroy  = func_destroy;
    node->func_run_sync = func_run_sync;
    opaque->pipeline    = pipeline;
    opaque->ffp         = ffp;
    opaque->decoder     = &is->viddec;

    opaque->avctx = opaque->decoder->avctx;
    switch (opaque->avctx->codec_id) {
    case AV_CODEC_ID_H264:
        opaque->codec_mime = SDL_AMIME_VIDEO_AVC;
        break;
    default:
        ALOGE("%s:create: not H264\n", __func__);
        goto fail;
    }

    if (JNI_OK != SDL_JNI_SetupThreadEnv(&env)) {
        ALOGE("%s:create: SetupThreadEnv failed\n", __func__);
        goto fail;
    }

    opaque->acodec = SDL_AMediaCodecJava_createDecoderByType(env, opaque->codec_mime);
    if (!opaque->acodec) {
        ALOGE("%s:open_video_decoder: SDL_AMediaCodecJava_createDecoderByType(%s) failed\n", __func__, opaque->codec_mime);
        goto fail;
    }

    opaque->aformat = SDL_AMediaFormatJava_createVideoFormat(env, opaque->codec_mime, opaque->avctx->width, opaque->avctx->height);
    if (opaque->avctx->extradata && opaque->avctx->extradata_size > 0) {
        if (opaque->avctx->codec_id == AV_CODEC_ID_H264 && opaque->avctx->extradata[0] == 1) {
            opaque->bsfc = av_bitstream_filter_init("h264_mp4toannexb");

            opaque->orig_extradata_size = opaque->avctx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;
            opaque->orig_extradata      = (uint8_t*) av_mallocz(opaque->orig_extradata_size);
            if (!opaque->orig_extradata) {
                ALOGE("%s:open_video_decoder: orig_extradata alloc failed\n", __func__);
                goto fail;
            }
            memcpy(opaque->orig_extradata, opaque->avctx->extradata, opaque->avctx->extradata_size);
            SDL_AMediaFormat_setBuffer(opaque->aformat, "csd-0", opaque->orig_extradata, opaque->orig_extradata_size);
        } else {
            // Codec specific data
            SDL_AMediaFormat_setBuffer(opaque->aformat, "csd-0", opaque->avctx->extradata, opaque->avctx->extradata_size);
        }
    }

    return node;
fail:
    ffpipenode_free_p(&node);
    return NULL;
}
