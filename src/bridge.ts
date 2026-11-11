import type { Progress } from './types';

// Same request-id / PostWebMessage model as ../dr-dict/web/bridge.ts.
interface WebViewHost {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', listener: (event: MessageEvent) => void): void;
}
const host = (window as unknown as { chrome?: { webview?: WebViewHost } }).chrome?.webview;
let sequence = 0;
const pending = new Map<number, { resolve: (value: unknown) => void; reject: (error: Error) => void; timer: number }>();
const progressListeners = new Set<(progress: Progress) => void>();
type CaptionTooltip = { text: string; left: number; width: number };
const captionListeners = new Set<(tip: CaptionTooltip) => void>();
export function onCaptionTooltip(listener: (tip: CaptionTooltip) => void) { captionListeners.add(listener); return () => { captionListeners.delete(listener); }; }
host?.addEventListener('message', ({ data }) => {
  if (data?.event === 'library:progress') { for (const listener of progressListeners) listener(data.data); return; }
  if (data?.event === 'caption-tooltip') { for (const listener of captionListeners) listener(data.data); return; }
  const request = pending.get(data?.id);
  if (!request) return;
  clearTimeout(request.timer); pending.delete(data.id);
  if (data.error) request.reject(new Error(data.error));
  else request.resolve(data.result);
});
export function call<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
  return new Promise((resolve, reject) => {
    if (!host) { reject(new Error('请通过 C++ / WebView2 桌面程序打开：.\\scripts\\build.ps1 -Run')); return; }
    const id = ++sequence;
    // Folder scans and a user's native file dialog may legitimately take minutes.
    const timer = window.setTimeout(() => { pending.delete(id); reject(new Error('原生请求超时，请稍后重试。')); }, 30 * 60 * 1000);
    pending.set(id, { resolve: value => resolve(value as T), reject, timer });
    host.postMessage({ id, method, params });
  });
}
window.music = Object.freeze({
  getLibrary: () => call('library:get'),
  importMusic: (kind: 'folder' | 'files') => call('library:import', { kind }),
  rescan: () => call('library:rescan'),
  update: (data: Parameters<Window['music']['update']>[0]) => call('library:update', data),
  remove: (id: string) => call('library:remove', { id }),
  lyrics: (id: string) => call('lyrics:get', { id }),
  attachLyrics: (id: string) => call('lyrics:attach', { id }),
  reveal: (id: string) => call('track:reveal', { id }),
  openData: () => call('app:data'),
  notices: () => call('app:notices'),
  appearance: (data: { theme: string; dimmed: boolean }) => call('window:appearance', data),
  onProgress: (listener: (progress: Progress) => void) => { progressListeners.add(listener); return () => { progressListeners.delete(listener); }; },
}) as Window['music'];
