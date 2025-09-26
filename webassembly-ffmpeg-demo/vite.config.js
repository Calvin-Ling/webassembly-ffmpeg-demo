
import { defineConfig } from 'vite';

export default defineConfig({
  server: {
    open: true
  },
  optimizeDeps: {
    exclude: ['../wasm/dist/ffmpeg-wrapper.js']
  },
  assetsInclude: ['**/*.wasm']
});
