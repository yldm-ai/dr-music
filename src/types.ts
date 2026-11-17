export interface Track {
  id: string; path: string; title: string; artist: string; album: string;
  duration: number; format: string; addedAt: number; cover?: string;
  /** The file's last write time (ms): 修改日期 sorting. */
  modifiedAt?: number;
  /** The tags' title while 歌曲名称 shows file names (front-end only). */
  tagTitle?: string;
  /** A track of a whole-CD rip: its range of the file and the .cue it came from. */
  cue?: { start: number; end: number; sheet: string };
  sampleRate?: number; bitrate?: number; size?: number;
  metadataPending?: boolean; metadataRevision?: number; metadataError?: string;
  albumArtist?: string; trackNumber?: number; discNumber?: number; year?: number;
  coverRevision?: number; coverHash?: string; coverSource?: string; coverError?: string;
  /** Lyric source shown when several exist: 'local' or an online provider id. */
  lyricPreference?: string;
  /** Per-song pronunciation choice; undefined follows the lyric defaults. */
  lyricRuby?: '' | import('./lib/ruby').RubyChoice;
}
export interface Playlist { id: string; name: string; trackIds: string[] }
export interface Settings { volume: number; repeat: 'off' | 'all' | 'one'; shuffle: boolean; lyricOffset: number; nowLayout?: 'default' | 'lyrics' | 'cover'; heroSheen?: boolean; startupTrack?: 'first' | 'random'; theme: 'dark' | 'light'; titleBar: 'direct2d' | 'system' | 'webview2'; lyricFont?: string; lyricWeight?: number; lyricScale?: number; chineseSearch?: boolean; lyricScript?: 'o' | 's' | 't'; lyricVariant?: 't' | 'tw' | 'twp' | 'hk'; lyricRuby?: '' | 'jyutping'; lyricRubyBelow?: boolean; lyricTranslate?: '' | 'zh' | 'en'; translateModel?: string; translateProvider?: 'bilibili' | 'niutrans'; onlineLyrics?: boolean; onlineTranslation?: boolean; onlineRomaji?: boolean; onlineJyutping?: boolean; lyricSource?: string; lyricProviders?: string[]; lyricProviderOrder?: string[]; amllMirror?: string; librarySort?: import('./lib/library').TrackSort; titleSource?: 'tag' | 'file' }
export interface FontFamily { family: string; name: string }
export interface Library { version: number; tracks: Track[]; folders: string[]; favorites: string[]; playlists: Playlist[]; settings: Settings; notice?: string }
/** roman / translation (+ format): the provider's own romanization (NetEase romalrc, QQ contentroma) and translation (NetEase tlyric, QQ contentts). */
export interface LyricFile { text: string; format: string; source: string; roman?: string; romanFormat?: string; translation?: string; translationFormat?: string }
/** A lyric from an online provider, matched natively; best match first. */
export interface OnlineLyricResult extends LyricFile { provider: string; title: string; artists: string; album: string; score: number; reference: string }
export interface ImportResult { library: Library; imported: number; removed?: number; warnings: string[] }
export interface Progress { done: number; total: number; name: string }
export interface PlaybackState { id: string; position: number; duration: number; playing: boolean; ended: boolean; volume: number; error: string; request: number; serial: number; backend?: string }
export interface PlaybackCommand { action: 'play' | 'pause' | 'stop' | 'seek' | 'volume' | 'state'; request: number; id?: string; position?: number; volume?: number }
declare global {
  interface Window {
    music: {
      playback(command: PlaybackCommand): Promise<PlaybackState>;
      onPlayback(callback: (state: PlaybackState) => void): () => void;
      onPlaybackAction(callback: (action: { action: string; position?: number }) => void): () => void;
      getLibrary(): Promise<Library>;
      importMusic(kind: 'folder' | 'files'): Promise<ImportResult | null>;
      rescan(): Promise<ImportResult>;
      update(data: Partial<Pick<Library, 'favorites' | 'playlists'>> & { settings?: Partial<Settings> }): Promise<Library>;
      remove(id: string): Promise<Library>;
      removeFolder(folder: string): Promise<Library>;
      lyrics(id: string): Promise<LyricFile>;
      attachLyrics(id: string): Promise<LyricFile | null>;
      onlineLyrics(id: string, refresh?: boolean): Promise<Omit<OnlineLyricResult, 'source'>[]>;
      setLyricPreference(id: string, source: string): Promise<Track>;
      setLyricRuby(id: string, ruby: NonNullable<Track['lyricRuby']>): Promise<Track>;
      lyricsStatus(): Promise<{ appleToken: boolean; providers: string[]; amllDefault: string }>;
      setAppleToken(token: string): Promise<{ appleToken: boolean }>;
      reveal(id: string): Promise<void>;
      openData(): Promise<void>;
      notices(): Promise<string>;
      fonts(): Promise<FontFamily[]>;
      chineseStatus(): Promise<{ available: boolean }>;
      chineseConvert(text: string, script: 's' | 't', variant: string): Promise<string>;
      translateStatus(): Promise<import('./lib/translate').TranslatorStatus>;
      translateTest(): Promise<string[]>;
      openPage(page: 'niutrans' | 'index-translate'): Promise<void>;
      onTranslateChanged(listener: () => void): () => void;
      setNiutransKey(appId: string, apikey: string): Promise<{ configured: boolean; appId: string }>;
      translateLines(lines: string[], source: string, target: string): Promise<string[]>;
      simplify(texts: string[]): Promise<string[]>;
      chineseQuery(query: string): Promise<string[]>;
      captionLayout(data: { caption: CaptionRect; minimize: CaptionRect; maximize: CaptionRect; close: CaptionRect; dpr: number }): Promise<void>;
      windowCommand(command: 'minimize' | 'maximize' | 'close'): Promise<void>;
      appearance(data: { theme: string; dimmed: boolean }): Promise<void>;
      onProgress(callback: (p: Progress) => void): () => void;
      onMetadata(callback: (tracks: Track[]) => void): () => void;
      onMetadataError(callback: (error: string) => void): () => void;
    };
  }
}

export interface CaptionRect { x: number; y: number; width: number; height: number }
