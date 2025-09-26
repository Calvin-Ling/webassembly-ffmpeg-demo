
# WebAssembly FFmpeg 音视频解码性能对比演示

一个基于 WebAssembly + FFmpeg 的浏览器音视频解码性能对比项目，专门针对 **Opus 音频** 和 **H.264 视频** 解码进行优化。

⚠️ 项目不包含媒体文件 — 请自行准备 `.opus` 或 `.mp4` 文件进行测试。

## 快速开始

```bash
npm install
npm run dev
```

然后在浏览器中打开 http://localhost:5173 并上传您的文件。

## 项目特性

- **Opus 音频解码**：将 `.opus` 文件解码为 WAV 格式并在浏览器中播放
- **H.264 视频解码**：将 `.mp4` H.264 视频解码为原始帧并重构为可播放的 WebM
- **性能对比**：对比 FFmpeg WASM 与浏览器原生 WebCodecs API 的解码性能
- **技术演示**：展示 WebAssembly 在音视频处理领域的应用潜力

## 技术架构详解

### 1. 整体架构

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   浏览器前端     │    │   WebAssembly    │    │   FFmpeg 库     │
│   (JavaScript)  │◄──►│   (C++ 编译)     │◄──►│   (C 库)        │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### 2. WASM 模块结构

#### 2.1 核心文件组织
```
wasm/
├── ffmpeg_wrapper.h      # C++ 头文件，定义接口
├── ffmpeg_wrapper.cpp    # C++ 实现，FFmpeg 封装
└── dist/
    ├── ffmpeg-wrapper.js # Emscripten 生成的 JS 胶水代码
    └── ffmpeg-wrapper.wasm # 编译后的 WASM 二进制文件
```

#### 2.2 关键接口定义
```cpp
// 初始化 FFmpeg 库
extern "C" void init_ffmpeg();

// Opus 音频解码接口
extern "C" int decode_opus_to_pcm(
    const uint8_t *input,    // 输入数据指针
    int input_size,          // 输入数据大小
    uint8_t **output,        // 输出 PCM 缓冲区指针
    int *output_size         // 输出数据大小
);

// H.264 视频解码接口
extern "C" int decode_h264_to_rgba(
    const uint8_t *input,    // 输入数据指针
    int input_size,          // 输入数据大小
    uint8_t **output,        // 输出 RGBA 缓冲区指针
    int *width,              // 输出图像宽度
    int *height              // 输出图像高度
);
```

### 3. WASM 内存管理机制

#### 3.1 内存布局
```
WASM 线性内存布局：
┌─────────────────┐ ← 0x00000000
│   系统保留区     │
├─────────────────┤ ← 0x00100000 (1MB)
│   输入数据区     │ ← inputOffset
├─────────────────┤
│   输出指针区     │ ← outputPtrOffset
├─────────────────┤
│   参数存储区     │ ← widthOffset, heightOffset
└─────────────────┘
```

#### 3.2 数据传递流程
```javascript
// 1. 分配 WASM 内存空间
const inputOffset = 1024 * 1024; // 从 1MB 开始
const fileData = new Uint8Array(await file.arrayBuffer());

// 2. 将文件数据复制到 WASM 内存
module.HEAPU8.set(fileData, inputOffset);

// 3. 调用 C++ 函数进行解码
const result = decodeFunction(inputOffset, fileData.length, ...);

// 4. 从 WASM 内存中读取解码结果
const outputData = module.HEAPU8.slice(outputPtr, outputPtr + outputSize);
```

### 4. 音频解码详细流程 (Opus → WAV)

#### 4.1 FFmpeg 解码管道
```
Opus 文件 → AVFormatContext → AVCodecContext → SwrContext → PCM16LE
    ↓              ↓                ↓              ↓           ↓
  Ogg容器      格式解析         Opus解码器      重采样器    16位PCM
```

#### 4.2 关键步骤详解

**步骤 1：创建内存输入上下文**
```cpp
// 分配内存缓冲区
unsigned char *buffer = av_malloc(input_size + AV_INPUT_BUFFER_PADDING_SIZE);
memcpy(buffer, input, input_size);

// 创建 AVIOContext 用于内存输入
AVIOContext *io_ctx = avio_alloc_context(buffer, input_size, 0, NULL, NULL, NULL, NULL);
```

**步骤 2：初始化格式和解码器**
```cpp
// 分配格式上下文
AVFormatContext *fmt_ctx = avformat_alloc_context();
fmt_ctx->pb = io_ctx;

// 打开输入并查找流信息
avformat_open_input(&fmt_ctx, NULL, NULL, NULL);
avformat_find_stream_info(fmt_ctx, NULL);

// 查找音频流并获取解码器
AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
```

**步骤 3：设置重采样器**
```cpp
// 配置重采样器：Opus → PCM16LE, 48kHz, 立体声
SwrContext *swr_ctx = swr_alloc();
swr_alloc_set_opts2(&swr_ctx,
    &out_ch_layout, AV_SAMPLE_FMT_S16, 48000,  // 输出：立体声，16位，48kHz
    &in_ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,  // 输入：原始格式
    0, NULL);
```

**步骤 4：解码和重采样**
```cpp
// 读取数据包并解码
while (av_read_frame(fmt_ctx, pkt) >= 0) {
    avcodec_send_packet(codec_ctx, pkt);
    while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
        // 重采样到 PCM16LE
        int converted = swr_convert(swr_ctx, out_data, out_samples, 
                                   (const uint8_t**)frame->data, frame->nb_samples);
        // 复制到输出缓冲区
        memcpy(*output + *output_size, out_data[0], pcm_size);
    }
}
```

**步骤 5：生成 WAV 文件头**
```javascript
// 创建 WAV 文件头
const wavHeader = new ArrayBuffer(44);
const view = new DataView(wavHeader);

// RIFF 头部
writeString(0, 'RIFF');
view.setUint32(4, fileSize - 8, true);
writeString(8, 'WAVE');

// fmt 子块
writeString(12, 'fmt ');
view.setUint32(16, 16, true);
view.setUint16(20, 1, true);        // PCM 格式
view.setUint16(22, channels, true); // 声道数
view.setUint32(24, sampleRate, true); // 采样率
view.setUint16(34, bitsPerSample, true); // 位深度

// data 子块
writeString(36, 'data');
view.setUint32(40, dataSize, true);
```

### 5. 视频解码详细流程 (H.264 → RGBA)

#### 5.1 FFmpeg 解码管道
```
MP4文件 → AVFormatContext → AVCodecContext → SwsContext → RGBA
    ↓              ↓                ↓              ↓        ↓
  MP4容器      格式解析         H.264解码器     颜色转换    RGBA像素
```

#### 5.2 关键步骤详解

**步骤 1：视频流解析**
```cpp
// 查找视频流
int video_stream_index = -1;
for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_index = i;
        break;
    }
}
```

**步骤 2：H.264 解码器配置**
```cpp
// 获取 H.264 解码器
AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);

// 复制编解码器参数
avcodec_parameters_to_context(codec_ctx, codecpar);
avcodec_open2(codec_ctx, codec, NULL);
```

**步骤 3：颜色空间转换**
```cpp
// 创建缩放和颜色转换上下文
struct SwsContext *sws = sws_getContext(
    frame->width, frame->height, (AVPixelFormat)frame->format,  // 输入
    frame->width, frame->height, AV_PIX_FMT_RGBA,               // 输出
    SWS_BILINEAR, NULL, NULL, NULL
);

// 执行颜色空间转换
sws_scale(sws,
    (const uint8_t * const*)frame->data, frame->linesize,  // 输入
    0, frame->height,                                      // 区域
    frameRGBA->data, frameRGBA->linesize                   // 输出
);
```

### 6. 性能对比分析

#### 6.1 原生 WebCodecs API vs WASM FFmpeg

| 特性 | 原生 WebCodecs | WASM FFmpeg |
|------|----------------|-------------|
| **硬件加速** | ✅ GPU/专用硬件 | ❌ CPU 软件解码 |
| **并行处理** | ✅ 硬件并行 | ❌ 串行处理 |
| **内存效率** | ✅ GPU 内存 | ❌ 系统内存 |
| **功耗** | ✅ 低功耗 | ❌ 高功耗 |
| **兼容性** | ❌ 浏览器限制 | ✅ 广泛兼容 |
| **格式支持** | ❌ 有限格式 | ✅ 全格式支持 |

#### 6.2 性能差异原因

**硬件加速优势：**
- VideoDecoder 使用 GPU 或专用硬件解码器
- 支持并行处理，可同时处理多个数据块
- 直接在 GPU 内存中操作，减少数据传输开销

**软件解码限制：**
- WASM 运行在 CPU 上，无法利用硬件加速
- 主要进行串行处理，效率相对较低
- 需要频繁的内存拷贝和格式转换

**典型性能表现：**
- 硬件解码通常比软件解码快 **3-10 倍**
- 对于高分辨率视频，性能差异更加明显
- 音频解码差异相对较小，但仍可达到 **2-3 倍** 提升

### 7. WASM 构建流程详解

#### 7.1 构建工具链介绍

**Emscripten 工具链：**
- **emcc (Emscripten C Compiler)**：将 C/C++ 代码编译为 WebAssembly
- **em++ (Emscripten C++ Compiler)**：C++ 编译器，支持 C++ 特性
- **emar (Emscripten Archiver)**：创建静态库文件
- **emranlib (Emscripten Ranlib)**：为静态库生成索引

**FFmpeg 源码：**
- 提供音视频编解码的核心功能
- 包含 Opus 音频解码器和 H.264 视频解码器
- 支持多种容器格式（Ogg、MP4、Matroska）

#### 7.2 构建流程步骤

**步骤 1：环境准备**
```bash
# 项目目录结构
ROOT_DIR="$(pwd)"
DEMO_DIR="$ROOT_DIR/webassembly-ffmpeg-demo"
FFMPEG_SRC="$ROOT_DIR/FFmpeg"
BUILD_DIR="$ROOT_DIR/build-ffmpeg"
PREFIX_DIR="$ROOT_DIR/prefix"
INSTALL_DIR="$DEMO_DIR/wasm/dist"
EMSDK_DIR="$ROOT_DIR/emsdk"
```

**步骤 2：Emscripten 工具链安装**
```bash
# 检查 Emscripten 是否存在
EMCC="$EMSDK_DIR/upstream/emscripten/emcc"

if [ ! -f "$EMCC" ]; then
    echo "⬇️ Emscripten 工具链不存在，正在安装..."
    cd "$EMSDK_DIR"
    ./emsdk install latest    # 安装最新版本
    ./emsdk activate latest   # 激活工具链
fi
```

**步骤 3：FFmpeg 配置**
```bash
cd "$FFMPEG_SRC"
./configure \
  --cc="$EMCC" \                    # 使用 Emscripten C 编译器
  --cxx="$EMCXX" \                  # 使用 Emscripten C++ 编译器
  --ar="$EMAR" \                    # 使用 Emscripten 归档工具
  --ranlib="$EMRANLIB" \            # 使用 Emscripten 索引工具
  --prefix="$PREFIX_DIR" \          # 安装目录
  --enable-cross-compile \          # 启用交叉编译
  --target-os=none \                # 目标操作系统（WebAssembly）
  --arch=x86_32 \                   # 目标架构
  --cpu=generic \                   # 通用 CPU 类型
  --disable-x86asm \                # 禁用 x86 汇编
  --disable-inline-asm \            # 禁用内联汇编
  --disable-programs \              # 不构建 FFmpeg 命令行工具
  --disable-doc \                   # 不构建文档
  --disable-debug \                 # 禁用调试信息
  --disable-stripping \             # 不剥离符号
  --disable-everything \            # 禁用所有功能
  --enable-protocol=file \          # 启用文件协议
  --enable-demuxer=ogg \            # 启用 Ogg 解复用器
  --enable-demuxer=matroska \       # 启用 Matroska 解复用器
  --enable-demuxer=mov \            # 启用 MP4 解复用器
  --enable-decoder=opus \           # 启用 Opus 解码器
  --enable-decoder=h264 \           # 启用 H.264 解码器
  --enable-parser=opus \            # 启用 Opus 解析器
  --enable-parser=h264              # 启用 H.264 解析器
```

**步骤 4：FFmpeg 编译**
```bash
make clean      # 清理之前的构建
make -j$(nproc) # 并行编译，使用所有 CPU 核心
make install    # 安装到指定目录
```

**步骤 5：WASM 模块生成**
```bash
$EMCC "$DEMO_DIR/wasm/ffmpeg_wrapper.cpp" \
  -O3 \                                                           # 最高优化级别
  -s WASM=1 \                                                     # 生成 WASM 文件
  -s MODULARIZE=1 \                                               # 模块化输出
  -s EXPORT_ES6=1 \                                               # ES6 模块格式
  -s ALLOW_MEMORY_GROWTH=1 \                                      # 允许内存增长
  -s FILESYSTEM=1 \                                               # 启用文件系统
  -I"$PREFIX_DIR/include" \                                       # 包含头文件路径
  -L"$PREFIX_DIR/lib" \                                           # 库文件路径
  -lavcodec -lavformat -lavutil -lswresample -lswscale \         # 链接 FFmpeg 库
  -o "$INSTALL_DIR/ffmpeg-wrapper.js" \                          # 输出文件
  -s EXPORTED_FUNCTIONS='["_init_ffmpeg","_decode_opus_to_pcm","_decode_h264_to_rgba"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8","getValue","setValue","ccall","UTF8ToString"]'
```

#### 7.3 关键配置参数解析

**编译器配置：**
- `--cc="$EMCC"`：指定使用 Emscripten C 编译器替代系统 GCC
- `--cxx="$EMCXX"`：指定使用 Emscripten C++ 编译器
- `--enable-cross-compile`：启用交叉编译模式

**目标平台配置：**
- `--target-os=none`：指定目标为 WebAssembly（无操作系统）
- `--arch=x86_32`：指定 32 位 x86 架构（WASM 兼容）
- `--cpu=generic`：使用通用 CPU 指令集

**功能裁剪：**
- `--disable-everything`：禁用所有默认功能
- `--enable-decoder=opus`：仅启用 Opus 音频解码器
- `--enable-decoder=h264`：仅启用 H.264 视频解码器
- `--enable-demuxer=ogg`：启用 Ogg 容器格式支持

**Emscripten 特定配置：**
- `-s WASM=1`：生成 WebAssembly 二进制文件
- `-s MODULARIZE=1`：生成模块化的 JavaScript 包装器
- `-s ALLOW_MEMORY_GROWTH=1`：允许 WASM 内存动态增长
- `-s EXPORTED_FUNCTIONS`：指定导出的 C 函数列表

#### 7.4 构建产物说明

**生成的文件：**
```
wasm/dist/
├── ffmpeg-wrapper.js    # JavaScript 胶水代码
├── ffmpeg-wrapper.wasm  # WebAssembly 二进制文件
└── ffmpeg-wrapper.wasm.map  # 调试映射文件（可选）
```

**JavaScript 胶水代码功能：**
- 提供 WASM 模块加载和初始化
- 实现 C/C++ 函数到 JavaScript 的绑定
- 管理 WASM 内存分配和释放
- 处理数据类型转换（字符串、数组等）

**WebAssembly 二进制文件：**
- 包含编译后的 FFmpeg 库代码
- 包含自定义的 wrapper 函数
- 经过优化，体积相对较小
- 可在现代浏览器中直接执行

#### 7.5 构建优化策略

**编译优化：**
- 使用 `-O3` 最高优化级别
- 启用死代码消除（DCE）
- 进行函数内联优化

**功能裁剪：**
- 仅编译需要的解码器
- 禁用不需要的协议和格式
- 移除调试信息和文档

**内存管理：**
- 设置合理的初始内存大小
- 启用内存增长以适应大文件
- 优化内存对齐和访问模式

### 8. 技术实现细节

#### 8.1 内存管理策略
```javascript
// 内存对齐和边界检查
const inputOffset = 1024 * 1024; // 1MB 对齐
if (inputOffset + fileData.length > module.HEAPU8.length) {
    throw new Error('输入文件太大，无法放入 WASM 内存');
}

// 安全的指针操作
const outputPtr = module.getValue(outputPtrOffset, 'i32');
if (outputPtr === 0 || outputPtr >= module.HEAPU8.buffer.byteLength) {
    throw new Error(`无效的输出指针: ${outputPtr}`);
}
```

#### 8.2 错误处理机制
```cpp
// C++ 错误码定义
#define ERROR_MEMORY_ALLOC     -1  // 内存分配失败
#define ERROR_FORMAT_CONTEXT   -2  // 格式上下文分配失败
#define ERROR_OPEN_INPUT       -3  // 打开输入失败
#define ERROR_FIND_STREAM      -4  // 查找流信息失败
#define ERROR_NO_AUDIO_STREAM  -5  // 找不到音频流
#define ERROR_NO_DECODER       -6  // 找不到解码器
#define ERROR_DECODER_CONTEXT  -7  // 解码器上下文分配失败
#define ERROR_COPY_PARAMS      -8  // 复制参数失败
#define ERROR_OPEN_DECODER     -9  // 打开解码器失败
#define ERROR_RESAMPLER        -10 // 重采样器初始化失败
```

### 9. 使用场景和限制

#### 9.1 适用场景
- **跨平台音视频处理**：需要统一的解码能力
- **格式兼容性要求**：支持更多音视频格式
- **离线处理**：不依赖网络或外部服务
- **教育演示**：展示 WebAssembly 技术能力

#### 9.2 技术限制
- **文件大小限制**：受 WASM 内存限制
- **性能开销**：软件解码性能不如硬件加速
- **浏览器兼容性**：需要支持 WebAssembly 的现代浏览器
- **内存使用**：WASM 模块占用较多内存

#### 9.3 优化建议
- **内存管理**：合理分配和释放 WASM 内存
- **错误处理**：完善的错误检测和恢复机制
- **性能监控**：实时监控解码性能
- **格式优化**：针对特定格式进行优化

## 项目结构

```
webassembly-ffmpeg-demo/
├── src/
│   ├── index.js          # 主入口文件，UI 交互逻辑
│   └── ffmpeg.js         # FFmpeg WASM 封装层
├── wasm/
│   ├── ffmpeg_wrapper.h  # C++ 头文件
│   ├── ffmpeg_wrapper.cpp # C++ 实现
│   └── dist/             # 编译输出
│       ├── ffmpeg-wrapper.js
│       └── ffmpeg-wrapper.wasm
├── public/
│   └── index.html        # 主页面
├── package.json          # 项目配置
└── README.md            # 项目文档
```

## 开发说明

这是一个技术演示项目，展示了 WebAssembly 在音视频处理领域的应用潜力。项目重点在于：

1. **技术原理展示**：详细说明 WASM 与 FFmpeg 的集成方式
2. **性能对比分析**：对比不同解码方案的性能差异
3. **实现细节解析**：深入分析内存管理和数据传递机制
4. **最佳实践示例**：提供完整的错误处理和优化策略

**注意**：此项目仅用于技术演示和学习，不建议直接用于生产环境。生产环境建议使用经过充分测试和优化的专业音视频处理库。
