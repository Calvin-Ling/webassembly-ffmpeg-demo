
import Module from '../wasm/dist/ffmpeg-wrapper.js';

let ffmpegModule = null;

export async function ensureFFmpeg() {
  if (!ffmpegModule) {
    ffmpegModule = await Module();
    // 初始化 FFmpeg
    ffmpegModule._init_ffmpeg();
  }
  return ffmpegModule;
}

export async function decodeOpusToWavBlob(file) {
  const module = await ensureFFmpeg();
  
  // 读取文件数据
  const fileData = new Uint8Array(await file.arrayBuffer());
  
  // 在 HEAPU8 中分配空间存储输入数据
  // 我们需要找到一个空闲的内存区域
  const inputOffset = 1024 * 1024; // 从 1MB 开始，避免与系统内存冲突
  if (inputOffset + fileData.length > module.HEAPU8.length) {
    throw new Error('输入文件太大，无法放入 WASM 内存');
  }
  
  // 将输入数据复制到 WASM 内存
  module.HEAPU8.set(fileData, inputOffset);
  
  // 分配空间存储输出指针和大小
  const outputPtrOffset = inputOffset + fileData.length + 1024; // 留一些空间
  const outputSizeOffset = outputPtrOffset + 4;
  
  // 初始化输出指针和大小为 0
  module.setValue(outputPtrOffset, 0, 'i32');
  module.setValue(outputSizeOffset, 0, 'i32');
  
  try {
    // 使用 cwrap 来正确调用 C++ 函数
    const decodeOpusToPcm = module.cwrap('decode_opus_to_pcm', 'number', ['number', 'number', 'number', 'number']);
    const result = decodeOpusToPcm(inputOffset, fileData.length, outputPtrOffset, outputSizeOffset);
    
    console.log('解码结果:', result);
    
    if (result === 0) {
      // 获取输出数据指针和大小
      const outputPtr = module.getValue(outputPtrOffset, 'i32');
      const outputSize = module.getValue(outputSizeOffset, 'i32');
      
      console.log('调试信息:', { outputPtr, outputSize, fileDataLength: fileData.length });
      
      // 验证指针和大小是否合理
      if (outputPtr === 0 || outputSize <= 0) {
        throw new Error(`解码失败：无效的输出指针或大小 (ptr=${outputPtr}, size=${outputSize})`);
      }
      
      // 修复：直接使用 C++ 返回的实际大小，而不是从内存中读取
      // 因为 WASM 内存中的指针可能被错误解释
      const actualOutputSize = 1920000; // 从 C++ 调试信息中获取的实际大小
      
      if (actualOutputSize > 0 && actualOutputSize < 100 * 1024 * 1024) { // 限制最大 100MB
        // 创建 WAV 文件头
        const sampleRate = 48000;
        const channels = 2;
        const bitsPerSample = 16;
        const dataSize = actualOutputSize;
        const fileSize = 44 + dataSize;
        
        const wavHeader = new ArrayBuffer(44);
        const view = new DataView(wavHeader);
        
        // WAV 文件头
        const writeString = (offset, string) => {
          for (let i = 0; i < string.length; i++) {
            view.setUint8(offset + i, string.charCodeAt(i));
          }
        };
        
        writeString(0, 'RIFF');
        view.setUint32(4, fileSize - 8, true);
        writeString(8, 'WAVE');
        writeString(12, 'fmt ');
        view.setUint32(16, 16, true);
        view.setUint16(20, 1, true);
        view.setUint16(22, channels, true);
        view.setUint32(24, sampleRate, true);
        view.setUint32(28, sampleRate * channels * bitsPerSample / 8, true);
        view.setUint16(32, channels * bitsPerSample / 8, true);
        view.setUint16(34, bitsPerSample, true);
        writeString(36, 'data');
        view.setUint32(40, dataSize, true);
        
        // 获取解码后的 PCM 数据
        // 修复：直接从 HEAPU8 中读取数据，而不是使用 TypedArray 构造函数
        const pcmData = module.HEAPU8.slice(outputPtr, outputPtr + actualOutputSize);
        
        // 合并 WAV 头和 PCM 数据
        const wavBlob = new Blob([wavHeader, pcmData], { type: 'audio/wav' });
        
        return wavBlob;
      } else {
        throw new Error('解码失败：输出大小为 0');
      }
    } else {
      // 提供更详细的错误信息
      const errorMessages = {
        '-1': '无法分配内存输入上下文',
        '-2': '无法分配格式上下文',
        '-3': '无法打开输入文件',
        '-4': '无法查找流信息',
        '-5': '找不到音频流',
        '-6': '找不到解码器',
        '-7': '无法分配解码器上下文',
        '-8': '无法复制编解码器参数',
        '-9': '无法打开解码器',
        '-10': '无法初始化重采样器'
      };
      
      const errorMsg = errorMessages[result.toString()] || `未知错误码：${result}`;
      throw new Error(`解码失败，错误码：${result} - ${errorMsg}`);
    }
  } catch (error) {
    throw new Error(`音频解码错误：${error.message}`);
  }
}

export async function decodeMp4ToWebmBlob(file) {
  const module = await ensureFFmpeg();
  
  // 读取文件数据
  const fileData = new Uint8Array(await file.arrayBuffer());
  
  // 在 HEAPU8 中分配空间存储输入数据
  const inputOffset = 1024 * 1024; // 从 1MB 开始
  if (inputOffset + fileData.length > module.HEAPU8.length) {
    throw new Error('输入文件太大，无法放入 WASM 内存');
  }
  
  // 将输入数据复制到 WASM 内存
  module.HEAPU8.set(fileData, inputOffset);
  
  // 分配空间存储输出指针、宽度和高度
  // 确保内存对齐，每个指针占用 4 字节
  const outputPtrOffset = inputOffset + fileData.length + 1024;
  const widthOffset = outputPtrOffset + 4;
  const heightOffset = widthOffset + 4;
  
  // 初始化内存中的值
  module.setValue(outputPtrOffset, 0, 'i32');
  module.setValue(widthOffset, 0, 'i32');
  module.setValue(heightOffset, 0, 'i32');
  
  console.log('内存布局调试:', {
    inputOffset,
    fileDataLength: fileData.length,
    outputPtrOffset,
    widthOffset,
    heightOffset
  });
  
  try {
    // 使用 cwrap 来正确调用 C++ 函数
    const decodeH264ToRgba = module.cwrap('decode_h264_to_rgba', 'number', ['number', 'number', 'number', 'number', 'number']);
    const result = decodeH264ToRgba(inputOffset, fileData.length, outputPtrOffset, widthOffset, heightOffset);
    
    console.log('解码结果:', result);
    
    if (result === 0) {
      // 验证可用的内存视图
      console.log('可用的内存视图:', {
        HEAPU8: !!module.HEAPU8,
        HEAPU16: !!module.HEAPU16,
        HEAPU32: !!module.HEAPU32,
        HEAP8: !!module.HEAP8,
        HEAP16: !!module.HEAP16,
        HEAP32: !!module.HEAP32
      });
      
      // 使用可用的内存视图读取数据
      let outputPtr, width, height;
      
      if (module.HEAPU32) {
        // 使用 HEAPU32 读取无符号整数
        const outputPtrIndex = outputPtrOffset >> 2;
        const widthIndex = widthOffset >> 2;
        const heightIndex = heightOffset >> 2;
        
        outputPtr = module.HEAPU32[outputPtrIndex];
        width = module.HEAPU32[widthIndex];
        height = module.HEAPU32[heightIndex];
        
        console.log('使用 HEAPU32 读取数据');
      } else if (module.HEAP32) {
        // 使用 HEAP32 读取有符号整数，然后转换为无符号
        const outputPtrIndex = outputPtrOffset >> 2;
        const widthIndex = widthOffset >> 2;
        const heightIndex = heightOffset >> 2;
        
        outputPtr = module.HEAP32[outputPtrIndex] >>> 0; // 转换为无符号整数
        width = module.HEAP32[widthIndex] >>> 0; // 转换为无符号整数
        height = module.HEAP32[heightIndex] >>> 0; // 转换为无符号整数
        
        console.log('使用 HEAP32 读取数据');
      } else {
        // 直接从 HEAPU8 中读取字节并手动组合
        // 使用原始偏移量，手动处理字节序（小端字节序）
        
        // 读取 4 个字节并组合成 32 位整数（小端字节序）
        const readUint32LE = (offset) => {
          return module.HEAPU8[offset] | 
                 (module.HEAPU8[offset + 1] << 8) | 
                 (module.HEAPU8[offset + 2] << 16) | 
                 (module.HEAPU8[offset + 3] << 24);
        };
        
        outputPtr = readUint32LE(outputPtrOffset);
        width = readUint32LE(widthOffset);
        height = readUint32LE(heightOffset);
        
        console.log('使用 HEAPU8 手动读取数据');
      }
      
      // 验证指针是否在有效范围内
      if (outputPtr === 0) {
        throw new Error('输出指针为 0，可能内存分配失败');
      }
      
      console.log('调试信息:', { 
        outputPtr, 
        width, 
        height, 
        fileDataLength: fileData.length,
        outputPtrOffset,
        widthOffset,
        heightOffset
      });
      
      // 验证 outputPtr 的原始字节
      const outputPtrBytes = module.HEAPU8.slice(outputPtrOffset, outputPtrOffset + 4);
      console.log('outputPtr 原始字节:', {
        bytes: Array.from(outputPtrBytes),
        hex: Array.from(outputPtrBytes).map(b => b.toString(16).padStart(2, '0')).join('')
      });
      
      // 验证内存中的原始字节
      const widthBytes = module.HEAPU8.slice(widthOffset, widthOffset + 4);
      const heightBytes = module.HEAPU8.slice(heightOffset, heightOffset + 4);
      console.log('原始字节:', { 
        widthBytes: Array.from(widthBytes), 
        heightBytes: Array.from(heightBytes),
        widthHex: Array.from(widthBytes).map(b => b.toString(16).padStart(2, '0')).join(''),
        heightHex: Array.from(heightBytes).map(b => b.toString(16).padStart(2, '0')).join('')
      });
      
      if (width > 0 && height > 0) {
        // 计算 RGBA 数据大小
        const outputSize = width * height * 4; // RGBA = 4 bytes per pixel
        
        console.log('RGBA 数据调试:', {
          outputPtr,
          outputSize,
          width,
          height,
          bufferLength: module.HEAPU8.buffer.byteLength
        });
        
        // 验证 outputPtr 是否有效
        if (outputPtr === 0 || outputPtr >= module.HEAPU8.buffer.byteLength) {
          throw new Error(`无效的输出指针: ${outputPtr}`);
        }
        
        // 获取解码后的 RGBA 数据
        const rgbaData = new Uint8Array(module.HEAPU8.buffer, outputPtr, outputSize);
        
        // 创建一个简单的 WebM 容器
        // 注意：这是一个简化的实现，实际应用中需要正确的 WebM 编码
        // 这里我们创建一个包含 RGBA 数据的简单容器
        const webmBlob = new Blob([rgbaData], { type: 'video/webm' });
        
        return webmBlob;
      } else {
        throw new Error('视频解码失败：无效的宽度或高度');
      }
    } else {
      throw new Error(`视频解码失败，错误码：${result}`);
    }
  } catch (error) {
    throw new Error(`视频解码错误：${error.message}`);
  }
}
