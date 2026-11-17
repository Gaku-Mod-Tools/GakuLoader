import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'node:path'

// Relative base so the isolated Manager prototype assets resolve under the
// WebView2 virtual-host folder mapping (https://gakuloader.invalid/) offline.
export default defineConfig({
  base: './',
  plugins: [react()],
  resolve: {
    alias: { '@': path.resolve(import.meta.dirname, './src') },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: 'chrome110',
    sourcemap: false,
  },
  server: {
    port: 5173,
    strictPort: true,
  },
})
