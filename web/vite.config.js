import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { mockApi } from './mock-api.js';

// Build output feeds the SPIFFS image (`pio run -t uploadfs`).
export default defineConfig({
  plugins: [preact(), ...(process.env.DEVICE_URL ? [] : [mockApi()])],
  build: {
    outDir: '../data',
    emptyOutDir: true,
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        chunkFileNames: 'app-[name].js',
        assetFileNames: '[name][extname]',
      },
    },
  },
  server: {
    // Set DEVICE_URL=https://<device-ip> to develop against real hardware.
    proxy: process.env.DEVICE_URL
      ? { '/api': { target: process.env.DEVICE_URL, secure: false, changeOrigin: true } }
      : undefined,
  },
});
