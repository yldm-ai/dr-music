import type { Progress, Track, PlaybackState, PlaybackCommand } from './types';

// Same request-id / PostWebMessage model as ../dr-dict/web/bridge.ts.
interface WebViewHost {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', listener: (event: MessageEvent) => void): void;
}
const host = (window as unknown as { chrome?: { webview?: WebViewHost } }).chrome?.webview;
let sequence = 0;
const pending = new Map<number, { resolve: (value: unknown) => void; reject: (error: Error) => void; timer: number }>();
const progressListeners = new Set<(progress: Progress) => void>();
const metadataListeners = new Set<(tracks: Track[]) => void>();
const metadataErrorListeners = new Set<(error: string) => void>();
const playbackListeners = new Set<(state: PlaybackState) => void>();
const playbackSession = crypto.randomUUID();
const playbackActions = new Set<(action: { action: string; position?: number }) => void>();
type CaptionTooltip = { text: string; left: number; width: number };
type CaptionRect = { x: number; y: number; width: number; height: number };
type CaptionLayout = { caption: CaptionRect; minimize: CaptionRect; maximize: CaptionRect; close: CaptionRect; dpr: number };
type CaptionButtonEvent = 'enter' | 'leave' | 'down' | 'up';
const captionListeners = new Set<(tip: CaptionTooltip) => void>();
const captionButtonListeners = new Set<(event: CaptionButtonEvent) => void>();
const windowListeners = new Set<(maximized: boolean) => void>();
export function onWindowState(listener: (maximized: boolean) => void) { windowListeners.add(listener); return () => { windowListeners.delete(listener); }; }
export function onCaptionTooltip(listener: (tip: CaptionTooltip) => void) { captionListeners.add(listener); return () => { captionListeners.delete(listener); }; }
export function onCaptionButton(listener: (event: CaptionButtonEvent) => void) { captionButtonListeners.add(listener); return () => { captionButtonListeners.delete(listener); }; }
host?.addEventListener('message', ({ data }) => {
  if (data?.event === 'playback:action') { for (const listener of playbackActions) listener(data.data); return; }
  if (data?.event === 'playback:state') { if (data.data?.session === playbackSession) for (const listener of playbackListeners) listener(data.data); return; }
  if (data?.event === 'window-state') { for (const listener of windowListeners) listener(!!data.data?.maximized); return; }
  if (data?.event === 'library:progress') { for (const listener of progressListeners) listener(data.data); return; }
  if (data?.event === 'library:metadata') { for (const listener of metadataListeners) listener(data.data); return; }
  if (data?.event === 'library:metadata-error') { for (const listener of metadataErrorListeners) listener(data.data); return; }
  if (data?.event === 'caption-tooltip') { for (const listener of captionListeners) listener(data.data); return; }
  if (data?.event === 'caption-button') { for (const listener of captionButtonListeners) listener(data.data?.event); return; }
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
  playback: (command: PlaybackCommand) => call('playback:command', { ...command, session: playbackSession }),
  onPlayback: (listener: (state: PlaybackState) => void) => { playbackListeners.add(listener); return () => { playbackListeners.delete(listener); }; },
  onPlaybackAction: (listener: (action: { action: string; position?: number }) => void) => { playbackActions.add(listener); return () => { playbackActions.delete(listener); }; },
  getLibrary: () => call('library:get'),
  importMusic: (kind: 'folder' | 'files') => call('library:import', { kind }),
  rescan: () => call('library:rescan'),
  update: (data: Parameters<Window['music']['update']>[0]) => call('library:update', data),
  remove: (id: string) => call('library:remove', { id }),
  removeFolder: (folder: string) => call('library:remove-folder', { folder }),
  lyrics: (id: string) => call('lyrics:get', { id }),
  attachLyrics: (id: string) => call('lyrics:attach', { id }),
  reveal: (id: string) => call('track:reveal', { id }),
  openData: () => call('app:data'),
  notices: () => call('app:notices'),
  fonts: () => call('app:fonts'),
  chineseStatus: () => call('chinese:status'),
  simplify: (texts: string[]) => call('chinese:simplify', { texts }),
  chineseQuery: (query: string) => call('chinese:query', { query }),
  captionLayout: (data: CaptionLayout) => call('window:caption-layout', data),
  windowCommand: (command: 'minimize' | 'maximize' | 'close') => call('window:command', { command }),
  appearance: (data: { theme: string; dimmed: boolean }) => call('window:appearance', data),
  onProgress: (listener: (progress: Progress) => void) => { progressListeners.add(listener); return () => { progressListeners.delete(listener); }; },
  onMetadata: (listener: (tracks: Track[]) => void) => { metadataListeners.add(listener); return () => { metadataListeners.delete(listener); }; },
  onMetadataError: (listener: (error: string) => void) => { metadataErrorListeners.add(listener); return () => { metadataErrorListeners.delete(listener); }; },
}) as Window['music'];
