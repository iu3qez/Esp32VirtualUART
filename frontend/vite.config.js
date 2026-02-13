import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig({
  plugins: [svelte()],
  build: {
    outDir: '../data/www',
    emptyOutDir: true,
    // Keep output small for ESP32 LittleFS (64KB partition)
    minify: 'esbuild',
    rollupOptions: {
      output: {
        manualChunks: undefined, // Single bundle for small SPA
      }
    }
  },
  server: {
    proxy: {
      '/api': 'http://192.168.4.1',
      '/ws': {
        target: 'ws://192.168.4.1',
        ws: true,
      }
    }
  }
})
