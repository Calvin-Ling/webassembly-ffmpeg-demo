#ifndef FFMPEG_WRAPPER_H
#define FFMPEG_WRAPPER_H

#include <stdint.h>

// 初始化 FFmpeg 库
extern "C" void init_ffmpeg();

// 解码 Opus 音频为 PCM16LE
// 参数:
//   input: 输入数据指针
//   input_size: 输入字节数
//   output: 输出 PCM 缓冲区指针（malloc 分配，需外部 free）
//   output_size: 输出字节数
// 返回: 0 表示成功，其它为错误码
extern "C" int decode_opus_to_pcm(const uint8_t *input, int input_size,
                                  uint8_t **output, int *output_size);

// 解码 H.264 视频为 RGBA
// 参数:
//   input: 输入数据指针
//   input_size: 输入字节数
//   output: 输出 RGBA 缓冲区指针（malloc 分配，需外部 free）
//   width, height: 输出图像宽高
// 返回: 0 表示成功，其它为错误码
extern "C" int decode_h264_to_rgba(const uint8_t *input, int input_size,
                                   uint8_t **output, int *width, int *height);

#endif // FFMPEG_WRAPPER_H
