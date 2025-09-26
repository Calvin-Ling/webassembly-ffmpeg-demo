#!/usr/bin/env bash
set -e

# ================================
# 项目路径
# ================================
ROOT_DIR="$(pwd)"
DEMO_DIR="$ROOT_DIR/webassembly-ffmpeg-demo"
FFMPEG_SRC="$ROOT_DIR/FFmpeg"
PREFIX_DIR="$ROOT_DIR/prefix"
INSTALL_DIR="$DEMO_DIR/wasm/dist"
EMSDK_DIR="$ROOT_DIR/emsdk"

mkdir -p "$PREFIX_DIR" "$INSTALL_DIR"

# ================================
# 检查 Emscripten 是否安装
# ================================
EMCC="$EMSDK_DIR/upstream/emscripten/emcc"
EMCXX="$EMSDK_DIR/upstream/emscripten/em++"
EMAR="$EMSDK_DIR/upstream/emscripten/emar"
EMRANLIB="$EMSDK_DIR/upstream/emscripten/emranlib"

if [ ! -f "$EMCC" ]; then
    echo "⬇️ Emscripten 工具链不存在，正在安装..."
    cd "$EMSDK_DIR"
    ./emsdk install latest
    ./emsdk activate latest
    cd "$ROOT_DIR"
    EMCC="$EMSDK_DIR/upstream/emscripten/emcc"
    EMCXX="$EMSDK_DIR/upstream/emscripten/em++"
    EMAR="$EMSDK_DIR/upstream/emscripten/emar"
    EMRANLIB="$EMSDK_DIR/upstream/emscripten/emranlib"
fi

echo "✅ 使用 Emscripten: $EMCC"

# ================================
# 配置 FFmpeg
# ================================
cd "$FFMPEG_SRC"
echo "⚙️ 配置 FFmpeg..."
./configure \
  --cc="$EMCC" \
  --cxx="$EMCXX" \
  --ar="$EMAR" \
  --ranlib="$EMRANLIB" \
  --prefix="$PREFIX_DIR" \
  --enable-cross-compile \
  --target-os=none \
  --arch=x86_32 \
  --cpu=generic \
  --disable-x86asm \
  --disable-inline-asm \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-stripping \
  --disable-everything \
  --enable-protocol=file \
  --enable-demuxer=ogg \
  --enable-demuxer=matroska \
  --enable-demuxer=mov \
  --enable-decoder=opus \
  --enable-decoder=h264 \
  --enable-parser=opus \
  --enable-parser=h264

# ================================
# 编译 FFmpeg
# ================================
echo "🔨 编译 FFmpeg..."
make clean
make -j$(sysctl -n hw.ncpu)
make install

# ================================
# 编译 wrapper.cpp -> wasm
# ================================
echo "🔗 构建 wrapper..."
$EMCC "$DEMO_DIR/wasm/ffmpeg_wrapper.cpp" -O3 -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
  -s ALLOW_MEMORY_GROWTH=1 -s FILESYSTEM=1 \
  -I"$PREFIX_DIR/include" -L"$PREFIX_DIR/lib" \
  -lavcodec -lavformat -lavutil -lswresample -lswscale \
  -o "$INSTALL_DIR/ffmpeg-wrapper.js" \
  -s EXPORTED_FUNCTIONS='["_init_ffmpeg","_decode_opus_to_pcm","_decode_h264_to_rgba"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8","getValue","setValue","ccall","UTF8ToString"]'

echo "✅ 构建完成，产物位于: $INSTALL_DIR"
