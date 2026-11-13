import { readdirSync, readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';

// kuromoji's IPADIC dictionary (gzipped, ~17 MB) for Japanese readings, served as /kuromoji/*.dat.gz
// next to index.html and fetched by src/lib/japanese.worker.ts the first time 音 → 罗马音 / 假名 is used.
const dictionary = path.join(path.dirname(createRequire(import.meta.url).resolve('@sglkc/kuromoji/package.json')), 'dict');
const kuromojiDictionary = (): Plugin => ({
  name: 'kuromoji-dictionary',
  configureServer(server) {
    server.middlewares.use('/kuromoji/', (request, response, next) => {
      const name = path.basename(request.url || '');
      if (!/^[\w.]+\.dat\.gz$/.test(name)) return next();
      response.setHeader('Content-Type', 'application/octet-stream');
      response.end(readFileSync(path.join(dictionary, name)));
    });
  },
  generateBundle() {
    for (const name of readdirSync(dictionary).filter(file => file.endsWith('.dat.gz')))
      this.emitFile({ type: 'asset', fileName: `kuromoji/${name}`, source: readFileSync(path.join(dictionary, name)) });
  },
});

export default defineConfig({
  base: './',
  plugins: [react(), kuromojiDictionary(), {
    name: 'development-csp',
    apply: 'serve',
    // React Refresh injects a small inline module during development only.
    transformIndexHtml: html => html.replace("script-src 'self'", "script-src 'self' 'unsafe-inline'"),
  }],
  server: { host: '127.0.0.1', port: 5173, strictPort: true, watch: { ignored: ['**/.test-data/**', '**/release/**'] } },
  build: { rollupOptions: { output: { manualChunks: { lyrics: ['@applemusic-like-lyrics/core', '@applemusic-like-lyrics/lyric'] } } } },
});
