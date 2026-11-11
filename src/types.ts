export interface Track {
  id: string; path: string; title: string; artist: string; album: string;
  duration: number; format: string; addedAt: number; cover?: string;
  sampleRate?: number; bitrate?: number; size?: number;
}
export interface Playlist { id: string; name: string; trackIds: string[] }
export interface Settings { volume: number; repeat: 'off' | 'all' | 'one'; shuffle: boolean; lyricOffset: number; theme: 'dark' | 'light'; titleBar: 'direct2d' | 'system' | 'webview2' }
export interface Library { version: number; tracks: Track[]; folders: string[]; favorites: string[]; playlists: Playlist[]; settings: Settings; notice?: string }
export interface LyricFile { text: string; format: string; source: string }
export interface ImportResult { library: Library; imported: number; warnings: string[] }
export interface Progress { done: number; total: number; name: string }
declare global {
  interface Window {
    music: {
      getLibrary(): Promise<Library>;
      importMusic(kind: 'folder' | 'files'): Promise<ImportResult | null>;
      rescan(): Promise<ImportResult>;
      update(data: Partial<Pick<Library, 'favorites' | 'playlists'>> & { settings?: Partial<Settings> }): Promise<Library>;
      remove(id: string): Promise<Library>;
      lyrics(id: string): Promise<LyricFile>;
      attachLyrics(id: string): Promise<LyricFile | null>;
      reveal(id: string): Promise<void>;
      openData(): Promise<void>;
      notices(): Promise<string>;
      captionLayout(data: { caption: CaptionRect; minimize: CaptionRect; maximize: CaptionRect; close: CaptionRect; dpr: number }): Promise<void>;
      windowCommand(command: 'minimize' | 'maximize' | 'close'): Promise<void>;
      appearance(data: { theme: string; dimmed: boolean }): Promise<void>;
      onProgress(callback: (p: Progress) => void): () => void;
    };
  }
}

export interface CaptionRect { x: number; y: number; width: number; height: number }
