import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
export default defineConfig({
  base: './',
  plugins: [react(), {
    name: 'development-csp',
    apply: 'serve',
    // React Refresh injects a small inline module during development only.
    transformIndexHtml: html => html.replace("script-src 'self'", "script-src 'self' 'unsafe-inline'"),
  }],
  server: { host: '127.0.0.1', port: 5173, strictPort: true, watch: { ignored: ['**/.test-data/**', '**/release/**'] } },
  build: { rollupOptions: { output: { manualChunks: { lyrics: ['@applemusic-like-lyrics/core', '@applemusic-like-lyrics/lyric'] } } } },
});
