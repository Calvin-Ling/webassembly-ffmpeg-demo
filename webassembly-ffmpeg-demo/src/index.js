
import { decodeOpusToWavBlob, decodeMp4ToWebmBlob } from './ffmpeg.js';

// Audio
const audioFileEl = document.getElementById('audioFile');
const decodeAudioBtn = document.getElementById('decodeAudioBtn');
const audioLog = document.getElementById('audioLog');
const wasmAudio = document.getElementById('wasmAudio');

let audioFile = null;
audioFileEl.addEventListener('change', e => {
  audioFile = e.target.files[0];
  decodeAudioBtn.disabled = !audioFile;
});

decodeAudioBtn.addEventListener('click', async () => {
  if (!audioFile) return;
  audioLog.textContent = '正在使用 FFmpeg (WASM) 解码音频...';
  const t0 = performance.now();
  try {
    const wavBlob = await decodeOpusToWavBlob(audioFile);
    const t1 = performance.now();
    wasmAudio.src = URL.createObjectURL(wavBlob);
    audioLog.textContent = `解码完成，耗时 ${(t1 - t0).toFixed(0)} ms`;
  } catch (error) {
    audioLog.textContent = `解码失败: ${error.message}`;
    console.error('音频解码错误:', error);
  }
});

// Video
const videoFileEl = document.getElementById('videoFile');
const decodeVideoBtn = document.getElementById('decodeVideoBtn');
const videoLog = document.getElementById('videoLog');
const nativeVideo = document.getElementById('nativeVideo');
const wasmVideo = document.getElementById('wasmVideo');

let videoFile = null;
videoFileEl.addEventListener('change', e => {
  videoFile = e.target.files[0];
  decodeVideoBtn.disabled = !videoFile;
  if (videoFile) {
    nativeVideo.src = URL.createObjectURL(videoFile);
  }
});

decodeVideoBtn.addEventListener('click', async () => {
  if (!videoFile) return;
  videoLog.textContent = '正在使用 FFmpeg (WASM) 解码视频... (这可能需要更长时间)';
  const t0 = performance.now();
  try {
    const webmBlob = await decodeMp4ToWebmBlob(videoFile);
    const t1 = performance.now();
    wasmVideo.src = URL.createObjectURL(webmBlob);
    videoLog.textContent = `视频解码完成，耗时 ${(t1 - t0).toFixed(0)} ms`;
  } catch (error) {
    videoLog.textContent = `视频解码失败: ${error.message}`;
    console.error('视频解码错误:', error);
  }
});

// 将函数暴露到全局作用域，供 HTML 中的内联脚本使用
window.decodeAudio = decodeAudioBtn.onclick;
window.decodeVideo = decodeVideoBtn.onclick;
