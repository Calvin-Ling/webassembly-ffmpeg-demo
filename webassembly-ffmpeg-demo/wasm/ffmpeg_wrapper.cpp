#include "ffmpeg_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// =========================
// 初始化 FFmpeg
// =========================
extern "C" void init_ffmpeg() {
    av_log_set_level(AV_LOG_ERROR); // 避免刷屏
    // 新版本 FFmpeg 中这些函数已被移除，不再需要手动注册
}

// =========================
// 解码音频 (Opus → PCM16LE)
// =========================
extern "C" int decode_opus_to_pcm(const uint8_t *input, int input_size,
                                  uint8_t **output, int *output_size) {
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    SwrContext *swr_ctx = NULL;
    
    *output = NULL;
    *output_size = 0;
    
    // 调试信息
    printf("C++ 调试: input_size=%d, output指针地址=%p, output_size指针地址=%p\n", input_size, output, output_size);

    // 创建内存输入上下文
    unsigned char *buffer = (unsigned char *)av_malloc(input_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buffer) return -1;
    
    memcpy(buffer, input, input_size);
    memset(buffer + input_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    
    AVIOContext *io_ctx = avio_alloc_context(buffer, input_size, 0, NULL, NULL, NULL, NULL);
    if (!io_ctx) {
        av_free(buffer);
        return -1;
    }

    // 分配格式上下文
    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -2;
    }
    fmt_ctx->pb = io_ctx;

    // 打开输入
    if (avformat_open_input(&fmt_ctx, NULL, NULL, NULL) < 0) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        avformat_free_context(fmt_ctx);
        return -3;
    }

    // 查找流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -4;
    }

    // 查找音频流
    int audio_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    if (audio_stream_index == -1) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -5;
    }

    // 获取解码器
    AVCodecParameters *codecpar = fmt_ctx->streams[audio_stream_index]->codecpar;
    codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -6;
    }

    // 分配解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -7;
    }

    // 复制编解码器参数
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -8;
    }

    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -9;
    }

    // 设置重采样器 (Opus → PCM16LE)
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -10;
    }
    
    // 设置输出通道布局
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_ch_layout = codec_ctx->ch_layout;
    
    if (swr_alloc_set_opts2(&swr_ctx,
        &out_ch_layout, AV_SAMPLE_FMT_S16, 48000,  // 输出
        &in_ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,  // 输入
        0, NULL) < 0) {
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -10;
    }
    
    if (swr_init(swr_ctx) < 0) {
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -10;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();

    // 读取并解码数据包
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == audio_stream_index) {
            int ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    av_packet_unref(pkt);
                    goto cleanup;
                }

                // 重采样
                uint8_t *out_data[8];
                int out_linesize;
                int out_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                int out_size = av_samples_alloc(out_data, &out_linesize, 2, out_samples, AV_SAMPLE_FMT_S16, 0);
                
                if (out_size > 0) {
                    int converted = swr_convert(swr_ctx, out_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
                    if (converted > 0) {
                        int pcm_size = converted * 2 * 2; // 2 channels, 2 bytes per sample
                        
                        // 在 WASM 环境中，使用 malloc 而不是 realloc
                        if (*output == NULL) {
                            *output = (uint8_t *)malloc(pcm_size);
                        } else {
                            *output = (uint8_t *)realloc(*output, *output_size + pcm_size);
                        }
                        
                        if (*output != NULL) {
                            memcpy(*output + *output_size, out_data[0], pcm_size);
                            *output_size += pcm_size;
                            printf("C++ 调试: 解码了 %d 字节，总大小: %d\n", pcm_size, *output_size);
                        } else {
                            printf("C++ 错误: 内存分配失败\n");
                        }
                    }
                    av_freep(&out_data[0]);
                }
            }
        }
        av_packet_unref(pkt);
    }

cleanup:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    if (io_ctx) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
    }

    // 最终调试信息
    printf("C++ 最终调试: *output=%p, *output_size=%d\n", *output, *output_size);

    return 0; // 成功
}

// =========================
// 解码视频 (MP4/H.264 → RGBA)
// =========================
extern "C" int decode_h264_to_rgba(const uint8_t *input, int input_size,
                                   uint8_t **output, int *width, int *height, int *frames) {
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    AVFrame *frameRGBA = NULL;
    struct SwsContext *sws = NULL;
    
    *output = NULL;
    *width = 0;
    *height = 0;
    if (frames) *frames = 0;
    
    // 调试信息
    printf("C++ 视频调试: input_size=%d\n", input_size);

    // 创建内存输入上下文
    unsigned char *buffer = (unsigned char *)av_malloc(input_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buffer) return -1;
    
    memcpy(buffer, input, input_size);
    memset(buffer + input_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    
    AVIOContext *io_ctx = avio_alloc_context(buffer, input_size, 0, NULL, NULL, NULL, NULL);
    if (!io_ctx) {
        av_free(buffer);
        return -1;
    }

    // 分配格式上下文
    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -2;
    }
    fmt_ctx->pb = io_ctx;

    // 打开输入
    if (avformat_open_input(&fmt_ctx, NULL, NULL, NULL) < 0) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        avformat_free_context(fmt_ctx);
        return -3;
    }

    // 查找流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -4;
    }

    // 查找视频流
    int video_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -5;
    }

    // 获取解码器
    AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -6;
    }

    // 分配解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -7;
    }

    // 复制编解码器参数
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -8;
    }

    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        return -9;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    frameRGBA = av_frame_alloc();

    // 读取并解码数据包
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    av_packet_unref(pkt);
                    goto cleanup;
                }

                // 获取第一帧的尺寸
                if (*width == 0 && *height == 0) {
                    *width = frame->width;
                    *height = frame->height;
                    
                    printf("C++ 视频调试: 解码到帧 %dx%d\n", *width, *height);
                    printf("C++ 视频调试: width指针=%p, height指针=%p\n", width, height);
                    printf("C++ 视频调试: 设置后 width=%d, height=%d\n", *width, *height);
                    
                    // 验证内存中的值
                    printf("C++ 视频调试: 内存验证 width=%d, height=%d\n", *width, *height);
                    
                    // 分配输出缓冲区（延后分配，等知道帧数后逐步扩容）
                    int frameBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, *width, *height, 1);
                    printf("C++ 视频调试: 每帧字节数: %d\n", frameBytes);
                    
                    // 不在这里分配 *output，而是在处理第一帧时分配

                    sws = sws_getContext(*width, *height, (AVPixelFormat)frame->format,
                                         *width, *height, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, NULL, NULL, NULL);
                    
                    if (!sws) {
                        printf("C++ 错误: 无法创建缩放上下文\n");
                        goto cleanup;
                    }
                }

                // 转换颜色空间
                if (sws) {
                    // 为当前帧准备临时缓冲区
                    int frameBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, *width, *height, 1);
                    uint8_t *temp = (uint8_t*)malloc(frameBytes);
                    if (!temp) { goto cleanup; }
                    
                    // 设置临时缓冲区的数据指针
                    av_image_fill_arrays(frameRGBA->data, frameRGBA->linesize,
                                         temp, AV_PIX_FMT_RGBA, *width, *height, 1);
                    
                    // 转换到临时缓冲区
                    int ret = sws_scale(sws,
                                       (const uint8_t * const*)frame->data, frame->linesize,
                                       0, *height, frameRGBA->data, frameRGBA->linesize);
                    
                    if (ret > 0) {
                        printf("C++ 视频调试: 成功转换帧到 RGBA，转换了 %d 行\n", ret);
                        
                        // 追加到输出缓冲区
                        if (*output == NULL) {
                            *output = (uint8_t*)malloc(frameBytes);
                            if (!*output) { free(temp); goto cleanup; }
                            memcpy(*output, temp, frameBytes);
                        } else {
                            size_t oldSize = (size_t)(*frames) * (size_t)frameBytes;
                            uint8_t *newBuf = (uint8_t*)realloc(*output, oldSize + frameBytes);
                            if (!newBuf) { free(temp); goto cleanup; }
                            *output = newBuf;
                            memcpy(*output + oldSize, temp, frameBytes);
                        }
                        if (frames) { (*frames)++; }
                        printf("C++ 视频调试: 已处理 %d 帧\n", *frames);
                    } else {
                        printf("C++ 错误: 颜色空间转换失败\n");
                    }
                    free(temp);
                }
            }
        }
        av_packet_unref(pkt);
    }

cleanup:
    if (sws) sws_freeContext(sws);
    av_frame_free(&frameRGBA);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    if (io_ctx) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
    }

    printf("C++ 视频最终调试: *output=%p, *width=%d, *height=%d\n", *output, *width, *height);
    printf("C++ 视频最终调试: width指针=%p, height指针=%p\n", width, height);
    printf("C++ 视频最终调试: 内存中的值 width=%d, height=%d\n", *width, *height);

    return 0; // 成功
}
