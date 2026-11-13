import { NativePlayback } from './playback';
import { VolumeControl } from './VolumeControl';
import { ProgressSlider } from './ProgressSlider';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { AnimatePresence, motion, MotionConfig, type Transition } from 'motion/react';
import { Music2, Library as LibraryIcon, Heart, Disc3, Mic2, FolderPlus, Plus, Search, Play, Pause, SkipBack, SkipForward, Shuffle, Repeat, Repeat1, ListMusic, Maximize2, ChevronDown, ChevronLeft, ChevronRight, Settings2, MoreHorizontal, FolderOpen, RefreshCw, X, Check, FileMusic, ArrowUpRight, Headphones, Trash2, Pencil, ListPlus, AudioLines, Folder, FolderSearch, Copy, ListX, Globe, Languages } from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import type { Library, Track, Settings, LyricFile, Progress, Playlist, FontFamily, OnlineLyricResult } from './types';
import { chooseLyric, providerName, LYRIC_PROVIDERS, type LyricSource } from './lib/lyricSource';
import { formatTime, nextTrack } from './lib/queue';
import { virtualRange } from './lib/virtual';
import { withoutFolder, withMetadata, musicFolders } from './lib/library';
import { searchText, hasHan, matchesQuery } from './lib/search';
import { ContextMenu, type MenuItem } from './ContextMenu';
import { Lyrics, lyricTypographyStyle, type TranslateState } from './Lyrics';
import { parseLyrics } from './lib/lyrics';
import { hasSourceRoman, type RubyChoice } from './lib/ruby';
import { hasKana } from './lib/japanese';
import { READINESS_LABEL, sourceLanguage, translateState as translateReadiness, translatorStatus, type TranslatorStatus } from './lib/translate';
import { TranslationSettings } from './TranslationModels';
import { Select } from './Select';
import { onCaptionButton, onWindowState } from './bridge';

const media = (track: Track, kind = 'audio') => `https://media.dr-music.local/${kind}/${track.id}${kind === 'cover' && (track.coverHash || track.coverRevision !== undefined) ? `?v=${track.coverHash || track.coverRevision}` : ''}`;
const smoothEase: [number, number, number, number] = [.22, 1, .36, 1];
const textTransition: Transition = { duration: .36, ease: smoothEase };
const quickTextTransition: Transition = { duration: .24, ease: smoothEase };
const pressTransition: Transition = { duration: .2, ease: smoothEase };
const playTransition = { type: 'spring', stiffness: 340, damping: 32, mass: .78 } as const;
const layoutTransition = { type: 'spring', stiffness: 250, damping: 30, mass: .9 } as const;
const QUEUE_ROW_HEIGHT = 60;
// Traditional standards for the lyric 繁 button (same choices as ../dr-dict).
const TRADITIONAL_NAMES = { t: '繁体（OpenCC 标准）', tw: '台湾正体', twp: '台湾正体及台湾用词', hk: '香港繁体' } as const;
const QUEUE_OVERSCAN = 4;
type MotionButtonProps = React.ComponentProps<typeof motion.button>;

function PressableButton({ whileHover, whileTap, transition, ...props }: MotionButtonProps) {
  return <motion.button
    whileHover={whileHover}
    whileTap={whileTap}
    transition={transition ?? pressTransition}
    {...props}
  />;
}

function NavButton({ selected = false, children, className = '', ...props }: Omit<MotionButtonProps, 'children'> & { selected?: boolean; children: React.ReactNode }) {
  return <PressableButton className={`nav-item ${selected ? 'selected' : ''} ${className}`} {...props}>
    {children}
  </PressableButton>;
}

function Cover({ track, className = '', enableLayout = true, loadImage = true }: { track?: Track; className?: string; enableLayout?: boolean; loadImage?: boolean }) {
  const [failedSource, setFailedSource] = useState<string>();
  const source = track?.cover ? media(track, 'cover') : undefined;
  useEffect(() => { if (!track?.metadataPending) setFailedSource(undefined); }, [source, track?.metadataPending, track?.metadataRevision]);
  return <motion.div layout={enableLayout ? 'position' : false} initial={false} className={`cover ${className}`} style={{ '--cover-hue': String(parseInt(track?.id.slice(0, 3) || '320', 16) % 360) } as React.CSSProperties}>
    <AnimatePresence mode="sync" initial={false}>
      {track && source && failedSource !== source && loadImage
        ? <motion.img key={source} src={source} alt={`${track.album} 封面`} loading="lazy" decoding="async" initial={{ opacity: 0, scale: 1.08 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0, scale: .96 }} transition={{ duration: .24 }} onError={() => setFailedSource(source)}/>
        : <motion.span key={`${track?.id || 'empty'}:fallback`} className="cover-fallback" initial={{ opacity: 0, rotate: -8 }} animate={{ opacity: 1, rotate: 0 }} exit={{ opacity: 0, rotate: 8 }} transition={{ duration: .22 }}><span className="record"><i/></span><Music2 className="cover-note"/></motion.span>}
    </AnimatePresence>
  </motion.div>;
}

function IconButton({ icon: Icon, label, active, ...props }: { icon: LucideIcon; label: string; active?: boolean } & Omit<MotionButtonProps, 'children'>) {
  return <PressableButton type="button" className={`icon-button ${active ? 'active' : ''}`} data-tooltip={label} aria-label={label} whileHover={{ opacity: .92 }} whileTap={{ opacity: .7 }} {...props}>
    <motion.span className="icon-button-glyph" animate={{ rotate: active ? -2 : 0 }} transition={pressTransition}><Icon size={19}/></motion.span>
  </PressableButton>;
}

export function App() {
  const [library, setLibrary] = useState<Library>();
  const [page, setPage] = useState('songs');
  const [group, setGroup] = useState('');
  const [query, setQuery] = useState('');
  const [sort, setSort] = useState('added');
  const [limit, setLimit] = useState(100);
  const [currentId, setCurrentId] = useState<string>();
  const [queue, setQueue] = useState<string[]>([]);
  const [playing, setPlaying] = useState(false);
  const [position, setPosition] = useState(0);
  const [duration, setDuration] = useState(0);
  const [nowPlaying, setNowPlaying] = useState(false);
  const [queueOpen, setQueueOpen] = useState(false);
  const [queueContentReady, setQueueContentReady] = useState(false);
  const [queueImagesReady, setQueueImagesReady] = useState(false);
  const [queueViewport, setQueueViewport] = useState({ scrollTop: 0, height: 0 });
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [settingsTab, setSettingsTab] = useState('appearance');
  const [maximized, setMaximized] = useState(false);
  const [captionHot, setCaptionHot] = useState(false);
  const [captionActive, setCaptionActive] = useState(false);
  useEffect(() => onWindowState(setMaximized), []);
  useEffect(() => onCaptionButton(event => {
    if (event === 'enter') setCaptionHot(true);
    else if (event === 'leave') { setCaptionHot(false); setCaptionActive(false); }
    else if (event === 'down') setCaptionActive(true);
    else if (event === 'up') setCaptionActive(false);
  }), []);
  useEffect(() => {
    if (!settingsOpen) return;
    const previous = document.activeElement as HTMLElement | null;
    const dialog = document.querySelector<HTMLElement>('.settings-modal');
    const frame = requestAnimationFrame(() => dialog?.querySelector<HTMLButtonElement>('[role=tab][aria-selected=true]')?.focus());
    const trap = (event: KeyboardEvent) => {
      if (event.key !== 'Tab' || document.querySelector('.select-popover')) return;
      const elements = Array.from(dialog?.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),[tabindex="0"]') || []).filter(element => element.getClientRects().length > 0 && !element.closest('[hidden]'));
      const first = elements[0], last = elements[elements.length - 1];
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
    };
    document.addEventListener('keydown', trap);
    return () => { cancelAnimationFrame(frame); document.removeEventListener('keydown', trap); previous?.focus({ preventScroll: true }); };
  }, [settingsOpen]);
  const [noticeText, setNoticeText] = useState('');
  const [toast, setToast] = useState('');
  const [fonts, setFonts] = useState<FontFamily[]>();
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<Progress>();
  const [lyricFile, setLyricFile] = useState<LyricFile>({ text: '', source: '', format: 'lrc' });
  const [lyricLoading, setLyricLoading] = useState(false);
  // AMLL TTML DB lyric for the current song: undefined while looking, null when there is none.
  const [onlineLyrics, setOnlineLyrics] = useState<OnlineLyricResult[] | null>();
  const [lyricMenu, setLyricMenu] = useState<{ x: number; y: number }>();
  const [appleToken, setAppleToken] = useState<{ set: boolean; draft: string }>({ set: false, draft: '' });
  const [mirrorDraft, setMirrorDraft] = useState<string>();
  const onlineRequest = useRef(0);
  const [menu, setMenu] = useState<{ point: { x: number; y: number }; ids: string[]; playlistsOnly?: boolean }>();
  const [selection, setSelection] = useState<string[]>([]);
  const selectionAnchor = useRef<string | undefined>(undefined);
  // The sidebar folder list starts collapsed every launch.
  const [foldersOpen, setFoldersOpen] = useState(false);
  const [playlistDialog, setPlaylistDialog] = useState<{ id?: string; name: string; trackIds?: string[] }>();
  const [deletePlaylist, setDeletePlaylist] = useState<Playlist>();
  const [audio] = useState(() => new NativePlayback());
  const stateRef = useRef({ library, queue, currentId }); stateRef.current = { library, queue, currentId };
  const searchRef = useRef<HTMLInputElement>(null);
  const queueListRef = useRef<HTMLDivElement>(null);
  const lyricRequest = useRef(0);
  const persistence = useRef(Promise.resolve());
  const pendingWrites = useRef<{ patch: Parameters<Window['music']['update']>[0] }[]>([]);
  const removingFolder = useRef<string | undefined>(undefined);
  const projectLibrary = useCallback((data: Library) => {
    for (const { patch } of pendingWrites.current) data = { ...data, ...patch, settings: { ...data.settings, ...patch.settings } };
    return removingFolder.current ? withoutFolder(data, removingFolder.current) : data;
  }, []);
  const notify = useCallback((message: string) => setToast(message), []);
  useEffect(() => { if (settingsOpen && window.music) window.music.lyricsStatus().then(status => setAppleToken(t => ({ ...t, set: status.appleToken }))).catch(() => {}); }, [settingsOpen]);
  const saveAppleToken = (token: string) => void window.music.setAppleToken(token.trim())
    .then(status => { setAppleToken({ set: status.appleToken, draft: '' }); notify(status.appleToken ? '已保存 Apple Music 令牌' : '已清除 Apple Music 令牌'); })
    .catch(error => notify(String(error)));
  const saveMirror = () => {
    const url = (mirrorDraft ?? settings?.amllMirror ?? '').trim().replace(/\/+$/, '');
    if (url && !/^https:\/\/[A-Za-z0-9.-]+(:\d+)?(\/[^\s?#"'<>\\]*)?$/.test(url)) { notify('镜像地址需要是不带参数的 https:// 地址'); return; }
    setSettings({ amllMirror: url }); setMirrorDraft(undefined); notify(url ? '已使用 AMLL TTML DB 镜像' : '已恢复默认地址');
  };
  // System fonts are enumerated natively (DirectWrite) the first time the appearance tab is shown.
  useEffect(() => {
    if (!settingsOpen || settingsTab !== 'appearance' || fonts || !window.music) return;
    let alive = true;
    window.music.fonts().then(list => { if (alive) setFonts([...list].sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'))); }).catch(error => { if (alive) { setFonts([]); notify(String(error)); } });
    return () => { alive = false; };
  }, [settingsOpen, settingsTab, fonts, notify]);
  useEffect(() => { if (toast) { const timer = setTimeout(() => setToast(''), 5000); return () => clearTimeout(timer); } }, [toast]);
  useEffect(() => {
    if (!window.music) { notify('请通过 C++ / WebView2 桌面程序打开'); return; }
    window.music.getLibrary().then(data => { setLibrary(projectLibrary(data)); if (data.notice) notify(data.notice); }).catch(e => notify(String(e)));
    return window.music.onProgress(setProgress);
  }, [notify, projectLibrary]);
  useEffect(() => {
    const pending = new Map<string, Track>();
    let frame = 0;
    const off = window.music.onMetadata(tracks => {
      for (const track of tracks) pending.set(track.id, track);
      if (!frame) frame = requestAnimationFrame(() => {
        const updates = [...pending.values()]; pending.clear(); frame = 0;
        setLibrary(previous => previous ? projectLibrary(withMetadata(previous, updates)) : previous);
      });
    });
    const offError = window.music.onMetadataError(error => notify(`歌曲信息保存失败：${error}`));
    return () => { off(); offError(); cancelAnimationFrame(frame); };
  }, [notify, projectLibrary]);
  useEffect(() => { document.documentElement.dataset.theme = library?.settings.theme || 'dark'; }, [library?.settings.theme]);
  useEffect(() => { audio.volume = library?.settings.volume ?? 0.7; }, [audio, library?.settings.volume]);
  useEffect(() => { setLimit(100); setMenu(undefined); setSelection([]); selectionAnchor.current = undefined; }, [query, page, group]);
  useEffect(() => {
    if (!queueOpen) return;
    const contentTimer = window.setTimeout(() => setQueueContentReady(true), 120);
    const imageTimer = window.setTimeout(() => setQueueImagesReady(true), 320);
    return () => {
      window.clearTimeout(contentTimer);
      window.clearTimeout(imageTimer);
    };
  }, [queueOpen]);
  useEffect(() => {
    if (!queueOpen || !queueContentReady) return;
    const list = queueListRef.current;
    if (!list) return;
    let frame = 0;
    const update = () => {
      const next = { scrollTop: list.scrollTop, height: list.clientHeight };
      setQueueViewport(previous => previous.scrollTop === next.scrollTop && previous.height === next.height ? previous : next);
    };
    const onScroll = () => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(update);
    };
    update();
    const observer = new ResizeObserver(update);
    observer.observe(list);
    list.addEventListener('scroll', onScroll, { passive: true });
    return () => {
      cancelAnimationFrame(frame);
      observer.disconnect();
      list.removeEventListener('scroll', onScroll);
    };
  }, [queueOpen, queueContentReady]);

  // Player-page scale: 1 up to a 1440×900 window, then growing with the smaller of width / height
  // (so a short wide window does not overflow), up to 1.6. The cover, column gap, title, buttons,
  // progress bar and lyrics are all sized from it in style.css.
  useEffect(() => {
    const update = () => document.documentElement.style.setProperty('--np-s', String(Math.round(Math.min(1.6, Math.max(1, Math.min(innerWidth / 1440, innerHeight / 900))) * 1000) / 1000));
    update(); window.addEventListener('resize', update); return () => window.removeEventListener('resize', update);
  }, []);
  const trackById = useMemo(() => new Map((library?.tracks || []).map(track => [track.id, track])), [library?.tracks]);
  const current = currentId ? trackById.get(currentId) : undefined;
  const lyric = chooseLyric(lyricFile, onlineLyrics, current?.lyricPreference);
  const queueWindow = useMemo(() => virtualRange(queue.length, QUEUE_ROW_HEIGHT, queueViewport.scrollTop, queueViewport.height, QUEUE_OVERSCAN), [queue.length, queueViewport]);
  const settings = library?.settings;
  const lookingOnline = !lyricFile.text && onlineLyrics === undefined && settings?.onlineLyrics !== false;
  useEffect(() => { if (settings?.titleBar !== 'webview2') { setCaptionHot(false); setCaptionActive(false); } }, [settings?.titleBar]);
  useEffect(() => {
    if (settings?.titleBar !== 'webview2' || !window.music) return;
    let frame = 0;
    const report = () => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => {
        const caption = document.querySelector<HTMLElement>('.web-caption');
        const buttons = caption?.querySelectorAll<HTMLButtonElement>('button');
        if (!caption || buttons?.length !== 3) return;
        const rect = (element: Element) => {
          const r = element.getBoundingClientRect();
          return { x: r.x, y: r.y, width: r.width, height: r.height };
        };
        void window.music.captionLayout({
          caption: rect(caption), minimize: rect(buttons[0]), maximize: rect(buttons[1]), close: rect(buttons[2]),
          dpr: window.devicePixelRatio,
        }).catch(() => {});
      });
    };
    report();
    window.addEventListener('resize', report);
    const observer = new ResizeObserver(report);
    const caption = document.querySelector<HTMLElement>('.web-caption');
    if (caption) {
      observer.observe(caption);
      for (const button of caption.querySelectorAll<HTMLButtonElement>('button')) observer.observe(button);
    }
    return () => { cancelAnimationFrame(frame); window.removeEventListener('resize', report); observer.disconnect(); };
  }, [settings?.titleBar, maximized]);
  useEffect(() => {
    if (!settings) return;
    void window.music.appearance({
      theme: nowPlaying ? 'dark' : settings.theme,
      dimmed: settingsOpen || !!playlistDialog || !!deletePlaylist || !!noticeText,
    }).catch(error => notify(String(error)));
  }, [settings?.theme, nowPlaying, settingsOpen, playlistDialog, deletePlaylist, noticeText, notify]);
  const persist = useCallback((patch: Parameters<Window['music']['update']>[0]) => {
    const write = { patch };
    pendingWrites.current.push(write);
    setLibrary(previous => previous ? projectLibrary({ ...previous, ...patch, settings: { ...previous.settings, ...patch.settings } }) : previous);
    const forget = () => { pendingWrites.current = pendingWrites.current.filter(item => item !== write); };
    persistence.current = persistence.current.then(async () => {
      try { await window.music.update(patch); }
      catch (error) {
        forget(); notify(`保存失败：${error}`);
        try { setLibrary(projectLibrary(await window.music.getLibrary())); } catch { /* retain current state while the host is unavailable */ }
      } finally { forget(); }
    });
  }, [notify, projectLibrary]);
  const setSettings = useCallback((patch: Partial<Settings>) => persist({ settings: patch }), [persist]);
  const favorite = useCallback((id: string) => {
    const ids = stateRef.current.library?.favorites || [];
    persist({ favorites: ids.includes(id) ? ids.filter(x => x !== id) : [...ids, id] });
  }, [persist]);
  const seek = useCallback((seconds: number) => {
    if (!audio.src || !Number.isFinite(audio.duration)) return;
    audio.currentTime = Math.max(0, Math.min(audio.duration, seconds)); setPosition(audio.currentTime);
  }, [audio]);
  const playTrack = useCallback((id: string, ids?: string[]) => {
    const track = stateRef.current.library?.tracks.find(t => t.id === id);
    if (!track) return;
    if (ids) setQueue(ids);
    if (stateRef.current.currentId !== id || audio.error || !audio.src) {
      audio.src = media(track); setCurrentId(id); setPosition(0); setDuration(track.duration);
    } else if (audio.ended) audio.currentTime = 0;
    void audio.play().catch(error => { if (error.name !== 'AbortError') notify(`无法播放 ${track.title}：文件可能已移动，或格式无法解码。`); });
  }, [audio, notify]);
  const togglePlay = useCallback(() => {
    if (!audio.src) {
      const first = stateRef.current.library?.tracks[0];
      if (first) playTrack(first.id, stateRef.current.library!.tracks.map(t => t.id));
    } else if (audio.paused) void audio.play().catch(() => notify('无法播放，请检查文件。'));
    else audio.pause();
  }, [audio, playTrack, notify]);
  const advance = useCallback((direction: 1 | -1, ended = false) => {
    if (direction === -1 && audio.currentTime > 3) { seek(0); return; }
    const { queue, currentId, library } = stateRef.current;
    const s = library?.settings;
    const id = nextTrack(queue, currentId, direction, s?.repeat || 'off', s?.shuffle || false, ended);
    if (id) { if (id === currentId) seek(0); playTrack(id); }
    else { audio.pause(); setPlaying(false); }
  }, [audio, playTrack, seek]);
  useEffect(() => window.music.onPlaybackAction(({ action, position }) => {
    if (action === 'play') void audio.play().catch(error => notify(String(error)));
    else if (action === 'pause') audio.pause();
    else if (action === 'next') advance(1);
    else if (action === 'previous') advance(-1);
    else if (action === 'seek' && position !== undefined) seek(position);
  }), [audio, advance, seek, notify]);
  useEffect(() => {
    const time = () => setPosition(audio.currentTime);
    const loaded = () => { setDuration(Number.isFinite(audio.duration) ? audio.duration : 0); setCurrentId(audio.src || undefined); };
    const play = () => setPlaying(true); const pause = () => setPlaying(false);
    const ended = () => advance(1, true);
    const error = () => { setPlaying(false); notify('播放失败：文件可能已移动、已损坏，或当前系统不支持此音频编码。'); };
    audio.addEventListener('timeupdate', time); audio.addEventListener('loadedmetadata', loaded); audio.addEventListener('play', play); audio.addEventListener('pause', pause); audio.addEventListener('ended', ended); audio.addEventListener('error', error);
    return () => { audio.removeEventListener('timeupdate', time); audio.removeEventListener('loadedmetadata', loaded); audio.removeEventListener('play', play); audio.removeEventListener('pause', pause); audio.removeEventListener('ended', ended); audio.removeEventListener('error', error); };
  }, [audio, advance, notify]);
  useEffect(() => {
    const token = ++lyricRequest.current;
    setLyricFile({ text: '', source: '', format: 'lrc' });
    if (!currentId) { setLyricLoading(false); return; }
    setLyricLoading(true);
    window.music.lyrics(currentId).then(file => { if (token === lyricRequest.current) setLyricFile(file); }).catch(e => { if (token === lyricRequest.current) notify(String(e)); }).finally(() => { if (token === lyricRequest.current) setLyricLoading(false); });
  }, [currentId, current?.metadataPending, notify]);
  const loadOnlineLyrics = useCallback((id: string, refresh: boolean) => {
    const token = ++onlineRequest.current;
    setOnlineLyrics(undefined);
    window.music.onlineLyrics(id, refresh)
      .then(results => { if (token === onlineRequest.current) setOnlineLyrics(results.map(r => ({ ...r, source: providerName(r.provider) }))); })
      .catch(() => { if (token === onlineRequest.current) setOnlineLyrics(null); });
  }, []);
  const providerKey = (settings?.lyricProviders || []).join(",") + "|" + (settings?.amllMirror || "");
  useEffect(() => {
    if (!currentId || settings?.onlineLyrics === false || !window.music) { ++onlineRequest.current; setOnlineLyrics(null); return; }
    // Matching uses tag metadata, so wait for it rather than search by file name.
    if (current?.metadataPending) { ++onlineRequest.current; setOnlineLyrics(undefined); return; }
    loadOnlineLyrics(currentId, false);
  }, [currentId, current?.title, current?.artist, current?.album, current?.metadataPending, settings?.onlineLyrics, providerKey, loadOnlineLyrics]);
  const chooseLyricSource = (source: LyricSource) => {
    if (!currentId) return;
    const id = currentId;
    setLibrary(previous => previous && { ...previous, tracks: previous.tracks.map(track => track.id === id ? { ...track, lyricPreference: source } : track) });
    void window.music.setLyricPreference(id, source).catch(error => notify(String(error)));
  };
  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      const element = event.target as HTMLElement;
      const editing = ['INPUT', 'TEXTAREA', 'SELECT'].includes(element.tagName) || element.isContentEditable;
      if (event.key === 'Escape') { setNowPlaying(false); setSettingsOpen(false); setQueueOpen(false); setMenu(undefined); setSelection([]); setPlaylistDialog(undefined); setDeletePlaylist(undefined); return; }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f') { event.preventDefault(); setNowPlaying(false); searchRef.current?.focus(); return; }
      if (editing || playlistDialog || deletePlaylist || settingsOpen) return;
      if (event.code === 'Space') { event.preventDefault(); togglePlay(); }
      // ← / → move through the current song by 5 seconds (menus, lists and sliders keep their own arrows).
      if ((event.key === 'ArrowLeft' || event.key === 'ArrowRight') && !event.ctrlKey && !event.metaKey && !event.altKey && !event.shiftKey && currentId
        && !element.closest('[role=menu],[role=listbox],[role=slider],[role=tablist],[role=radiogroup]')) {
        event.preventDefault();
        seek(audio.currentTime + (event.key === 'ArrowRight' ? 5 : -5));
        return;
      }
      if (event.ctrlKey || event.metaKey) {
        if (event.key === 'ArrowRight') { event.preventDefault(); advance(1); }
        if (event.key === 'ArrowLeft') { event.preventDefault(); advance(-1); }
        if (event.key.toLowerCase() === 'l' && currentId) { event.preventDefault(); setNowPlaying(v => !v); }
        if (event.key.toLowerCase() === 'd' && currentId) { event.preventDefault(); favorite(currentId); }
      }
    };
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key);
  }, [advance, togglePlay, seek, audio, currentId, favorite, playlistDialog, deletePlaylist, settingsOpen]);
  useEffect(() => {
    if (!('mediaSession' in navigator)) return;
    navigator.mediaSession.metadata = current ? new MediaMetadata({ title: current.title, artist: current.artist, album: current.album }) : null;
    const handlers: [MediaSessionAction, MediaSessionActionHandler][] = [['play', () => { if (audio.paused) togglePlay(); }], ['pause', () => audio.pause()], ['previoustrack', () => advance(-1)], ['nexttrack', () => advance(1)], ['seekto', e => { if (e.seekTime !== undefined) seek(e.seekTime); }]];
    for (const [action, fn] of handlers) { try { navigator.mediaSession.setActionHandler(action, fn); } catch { /* optional system capability */ } }
    navigator.mediaSession.playbackState = playing ? 'playing' : 'paused';
    return () => { for (const [action] of handlers) navigator.mediaSession.setActionHandler(action, null); };
  }, [current, playing, audio, togglePlay, advance, seek]);

  const runImport = async (kind: 'folder' | 'files' | 'rescan') => {
    setBusy(true); setProgress(undefined);
    try {
      await persistence.current;
      const result = await (kind === 'rescan' ? window.music.rescan() : window.music.importMusic(kind));
      if (result) {
        setLibrary(projectLibrary(result.library));
        // A rescan removes songs whose files moved or were deleted; keep queue and playback consistent.
        if (result.removed) {
          const valid = new Set(result.library.tracks.map(track => track.id));
          setQueue(q => q.filter(id => valid.has(id)));
          setSelection(s => s.filter(id => valid.has(id)));
          const current = stateRef.current.currentId;
          if (current && !valid.has(current)) { ++lyricRequest.current; audio.pause(); audio.removeAttribute('src'); audio.load(); setCurrentId(undefined); setPlaying(false); setPosition(0); setDuration(0); setNowPlaying(false); setLyricFile({ text: '', source: '', format: 'lrc' }); }
        }
        const summary = kind === 'rescan' ? `重新扫描完成，共 ${result.imported} 首音乐${result.removed ? `，已移除 ${result.removed} 首不存在的歌曲` : ''}` : `已加入 ${result.imported} 首音乐`;
        notify(`${summary}，歌曲信息将在后台补齐${result.warnings.length ? `，${result.warnings.length} 个文件存在读取问题：${result.warnings.slice(0, 2).join('；')}` : ''}`);
      }
    } catch (error) { notify(`导入失败：${error}`); }
    finally { setBusy(false); setProgress(undefined); }
  };
  const attachLyrics = async () => {
    if (!currentId) return;
    const id = currentId; const token = ++lyricRequest.current;
    try { const file = await window.music.attachLyrics(id); if (file && stateRef.current.currentId === id && token === lyricRequest.current) { setLyricFile(file); chooseLyricSource('local'); notify(`已载入 ${file.source}`); } }
    catch (error) { notify(String(error)); }
  };
  const toggleQueue = () => {
    if (queueOpen) setQueueOpen(false);
    else {
      setQueueContentReady(false);
      setQueueImagesReady(false);
      setQueueViewport({ scrollTop: 0, height: 0 });
      queueListRef.current?.scrollTo({ top: 0 });
      setQueueOpen(true);
    }
  };
  const navigate = (next: string) => { setPage(next); setGroup(''); setQuery(''); setNowPlaying(false); };
  // Simplified/traditional search: track text is folded to simplified natively (OpenCC) and cached
  // per search text; the query is expanded to its simplified writings while it contains Han characters.
  // Directories that hold music inside the folders added in settings act as read-only playlists.
  const folders = useMemo(() => library ? musicFolders(library) : [], [library?.tracks, library?.folders]);
  const [chineseAvailable, setChineseAvailable] = useState(false);
  useEffect(() => { window.music?.chineseStatus().then(status => setChineseAvailable(status.available)).catch(() => setChineseAvailable(false)); }, []);
  const chineseSearch = chineseAvailable && !!settings?.chineseSearch;
  // 原 / 简 / 繁 for the lyric shown (as ../dr-dict's script switch): the whole lyric file is
  // converted natively by OpenCC; markup and timestamps carry no Han characters, so they stay intact.
  const lyricScript = chineseAvailable ? settings?.lyricScript || 'o' : 'o';
  const lyricVariant = settings?.lyricVariant || 't';
  const lyricHasHan = useMemo(() => !!lyric.file && hasHan(lyric.file.text), [lyric.file]);
  const [scripted, setScripted] = useState<{ key: string; source: string; text: string }>();
  const scriptKey = lyric.file ? `${lyricScript}:${lyricVariant}:${lyric.file.text}` : '';
  useEffect(() => {
    if (!lyric.file || !lyricHasHan || lyricScript === 'o' || scripted?.key === scriptKey) return;
    let live = true;
    const key = scriptKey, source = lyric.file.text;
    window.music.chineseConvert(lyric.file.text, lyricScript, lyricVariant).then(text => { if (live) setScripted({ key, source, text }); }).catch(() => {});
    return () => { live = false; };
  }, [scriptKey, lyricHasHan]);
  // While 简 ⇄ 繁 converts, the previous conversion of the same lyric stays up (no flash of 原).
  const scriptedText = lyric.file && lyricScript !== 'o' && scripted?.source === lyric.file.text ? scripted.text : undefined;
  const shownLyric = useMemo(() => lyric.file && scriptedText !== undefined ? { ...lyric.file, text: scriptedText } : lyric.file, [lyric.file, scriptedText]);
  const [scriptMenu, setScriptMenu] = useState<{ x: number; y: number }>();
  const [rubyMenu, setRubyMenu] = useState<{ x: number; y: number }>();
  // 译: the local Hunyuan model (Ollama). The menu checks, each time it opens, whether Ollama runs and has the model.
  const [translateMenu, setTranslateMenu] = useState<{ x: number; y: number; status?: TranslatorStatus }>();
  const [translateState, setTranslateState] = useState<TranslateState>({ state: 'idle' });
  const lyricSource = useMemo(() => lyric.file ? sourceLanguage(lyric.file.text) : 'en', [lyric.file]);
  const openTranslateMenu = (box: DOMRect) => {
    if (translateMenu) { setTranslateMenu(undefined); return; }
    const point = { x: box.left, y: box.bottom + 6 };
    setTranslateMenu(point);
    void translatorStatus().then(status => setTranslateMenu(menu => menu && menu.x === point.x ? { ...menu, status } : menu), () => {});
  };
  const translateError = useRef('');
  const onTranslateState = useCallback((state: TranslateState) => {
    setTranslateState(state);
    // The same failure (e.g. no model on this device) is reported once, not for every song.
    if (state.state === 'error' && translateError.current !== state.message) notify(`翻译失败：${state.message}`);
    if (state.state === 'error') translateError.current = state.message; else if (state.state === 'done') translateError.current = '';
  }, [notify]);
  // 注音 is shown for lyrics with Han characters only; the choice itself is kept for the next song.
  // 音 / 译 apply to the song being played only: a different song starts with neither.
  const [lyricTools, setLyricTools] = useState<{ id?: string; ruby: '' | RubyChoice; translate: '' | 'zh' | 'en' }>({ ruby: '', translate: '' });
  const rubyChoice = lyricTools.id === current?.id ? lyricTools.ruby : '';
  const translateChoice = lyricTools.id === current?.id ? lyricTools.translate : '';
  const setLyricTool = (patch: Partial<{ ruby: '' | RubyChoice; translate: '' | 'zh' | 'en' }>) => setLyricTools(tools => ({ ...(tools.id === current?.id ? tools : { ruby: '', translate: '' }), ...patch, id: current?.id }));
  // Which guides fit this lyric: 粤语 for Chinese (Han without kana), 罗马音 / 假名 for Japanese, and the
  // lyric's own romanization (TTML, NetEase, QQ Music, Kugou) when it has one.
  const lyricHasKana = useMemo(() => !!lyric.file && hasKana(lyric.file.text), [lyric.file]);
  const lyricHasRoman = useMemo(() => { try { return !!lyric.file?.text && hasSourceRoman(parseLyrics(lyric.file, 0).lines); } catch { return false; } }, [lyric.file]);
  const rubyFits = (choice: RubyChoice) => choice === 'jyutping' ? lyricHasHan && !lyricHasKana : choice === 'romaji-online' ? lyricHasRoman : lyricHasKana;
  const lyricRuby = useMemo(() => rubyChoice && rubyFits(rubyChoice) ? { kind: rubyChoice, below: !!settings?.lyricRubyBelow } : undefined, [rubyChoice, settings?.lyricRubyBelow, lyricHasHan, lyricHasKana, lyricHasRoman]);
  const rubyAvailable = rubyFits('jyutping') || lyricHasKana || lyricHasRoman;
  const rubyError = useRef('');
  const onRubyError = useCallback((message: string) => { if (rubyError.current !== message) notify(`注音失败：${message}`); rubyError.current = message; }, [notify]);
  const folded = useRef(new Map<string, { key: string; text: string }>());
  const [foldedVersion, setFoldedVersion] = useState(0);
  useEffect(() => {
    if (!chineseSearch || !library) return;
    // Tracks are keyed by id, folder names by "folder:<path>".
    const items = [...library.tracks.map(track => ({ id: track.id, text: searchText(track) })), ...folders.map(folder => ({ id: `folder:${folder.path}`, text: folder.name }))];
    const stale = items.filter(item => folded.current.get(item.id)?.key !== item.text);
    if (!stale.length) return;
    for (const item of stale) if (!hasHan(item.text)) folded.current.set(item.id, { key: item.text, text: item.text });
    const pending = stale.filter(item => hasHan(item.text));
    let alive = true;
    void (async () => {
      for (let start = 0; start < pending.length && alive; start += 2000) {
        const batch = pending.slice(start, start + 2000), texts = batch.map(item => item.text);
        const simplified = await window.music.simplify(texts);
        if (!alive) return;
        batch.forEach((item, index) => folded.current.set(item.id, { key: texts[index], text: simplified[index] ?? texts[index] }));
      }
      if (alive) setFoldedVersion(version => version + 1);
    })().catch(error => notify(`简繁转换失败：${error}`));
    if (!pending.length) setFoldedVersion(version => version + 1);
    return () => { alive = false; };
  }, [chineseSearch, library?.tracks, folders, notify]);
  const [queryForms, setQueryForms] = useState<{ query: string; forms: string[] }>();
  useEffect(() => {
    const trimmed = query.trim();
    if (!chineseSearch || !hasHan(trimmed)) { setQueryForms(undefined); return; }
    let alive = true;
    const timer = window.setTimeout(() => void window.music.chineseQuery(trimmed).then(forms => { if (alive) setQueryForms({ query: trimmed, forms }); }).catch(() => {}), 80);
    return () => { alive = false; clearTimeout(timer); };
  }, [query, chineseSearch]);
  // The written forms a query is matched by: itself, plus its simplified writings with 简繁通搜.
  const searchForms = useMemo(() => {
    const trimmed = query.trim();
    if (!trimmed) return [];
    return chineseSearch && queryForms?.query === trimmed ? queryForms.forms : [trimmed];
  }, [query, chineseSearch, queryForms]);
  const selectedPlaylist = library?.playlists.find(p => page === `playlist:${p.id}`);
  const selectedFolder = folders.find(f => page === `folder:${f.path}`);
  const collectionPage = !!(selectedPlaylist || selectedFolder);
  const folderCover = selectedFolder?.trackIds.map(id => trackById.get(id)).find(track => track?.cover);
  const playlistCover = selectedPlaylist?.trackIds.map(id => trackById.get(id)).find(track => track?.cover);
  const filtered = useMemo(() => {
    let tracks = library?.tracks || [];
    if (page === 'favorites') tracks = tracks.filter(t => library!.favorites.includes(t.id));
    const ordered = selectedPlaylist?.trackIds || selectedFolder?.trackIds;
    if (ordered) { const map = new Map(tracks.map(t => [t.id, t])); tracks = ordered.map(id => map.get(id)).filter((t): t is Track => !!t); }
    if (group) tracks = tracks.filter(t => (page === 'artists' ? t.artist : t.album) === group);
    const q = query.trim().toLocaleLowerCase();
    if (q) tracks = tracks.filter(t => matchesQuery(searchText(t), chineseSearch ? folded.current.get(t.id)?.text : undefined, searchForms));
    return [...tracks].sort((a, b) => sort === 'title' ? a.title.localeCompare(b.title, 'zh-CN') : sort === 'artist' ? a.artist.localeCompare(b.artist, 'zh-CN') : selectedPlaylist || selectedFolder ? 0 : b.addedAt - a.addedAt);
  }, [library, page, group, query, sort, selectedPlaylist, selectedFolder, chineseSearch, searchForms, foldedVersion]);
  // Folder names are searchable too; matches are offered as shortcuts above the song results.
  const matchedFolders = useMemo(() => searchForms.length && !collectionPage
    ? folders.filter(folder => matchesQuery(folder.name, chineseSearch ? folded.current.get(`folder:${folder.path}`)?.text : undefined, searchForms))
    : [], [folders, searchForms, collectionPage, chineseSearch, foldedVersion]);
  const groups = useMemo(() => {
    const result = new Map<string, Track[]>();
    for (const track of filtered) {
      const key = page === 'artists' ? track.artist : track.album;
      const tracks = result.get(key);
      if (tracks) tracks.push(track); else result.set(key, [track]);
    }
    return [...result];
  }, [filtered, page]);
  const totalDuration = filtered.reduce((n, t) => n + t.duration, 0);
  const heading = query ? '搜索结果' : group || selectedPlaylist?.name || selectedFolder?.name || ({ songs: '你的音乐，随时在场。', favorites: '我喜欢的音乐', albums: '专辑收藏', artists: '艺术家' }[page] || '音乐库');
  const addToPlaylist = (playlist: Playlist, ids: string[]) => {
    const added = ids.filter(id => !playlist.trackIds.includes(id));
    if (!added.length) { notify(ids.length > 1 ? '所选歌曲都已在歌单中' : '这首歌已在歌单中'); return; }
    persist({ playlists: library!.playlists.map(p => p.id === playlist.id ? { ...p, trackIds: [...p.trackIds, ...added] } : p) });
    notify(ids.length > 1 ? `已添加 ${added.length} 首到「${playlist.name}」` : `已添加到「${playlist.name}」`);
  };
  const removeFromPlaylist = (playlist: Playlist, ids: string[]) => {
    const removed = new Set(ids);
    persist({ playlists: library!.playlists.map(p => p.id === playlist.id ? { ...p, trackIds: p.trackIds.filter(id => !removed.has(id)) } : p) });
    setSelection([]);
    if (ids.length > 1) notify(`已从「${playlist.name}」移除 ${ids.length} 首`);
  };
  const setFavorites = (ids: string[], on: boolean) => {
    const current = library?.favorites || [];
    persist({ favorites: on ? [...current, ...ids.filter(id => !current.includes(id))] : current.filter(id => !ids.includes(id)) });
  };
  const playNext = (ids: string[]) => {
    setQueue(q => { const copy = q.filter(id => !ids.includes(id)); const at = copy.indexOf(currentId || ''); copy.splice(at < 0 ? copy.length : at + 1, 0, ...ids); return copy; });
    notify(ids.length > 1 ? `已将 ${ids.length} 首加入下一首播放` : '已加入下一首播放');
  };
  const removeTracks = async (ids: string[]) => {
    try {
      await persistence.current;
      let data: Library | undefined;
      for (const id of ids) data = await window.music.remove(id);
      if (data) setLibrary(data);
      setQueue(q => q.filter(x => !ids.includes(x))); setSelection([]);
      if (currentId && ids.includes(currentId)) { audio.pause(); audio.removeAttribute('src'); audio.load(); setCurrentId(undefined); setPosition(0); setDuration(0); setNowPlaying(false); }
      notify(ids.length > 1 ? `已从音乐库移除 ${ids.length} 首，磁盘文件已保留` : '已从音乐库移除，磁盘文件已保留');
    } catch (error) { notify(String(error)); }
  };
  const removeFolder = async (folder: string) => {
    if (busy || removingFolder.current || !library) return;
    const before = library;
    const next = withoutFolder(before, folder);
    const valid = new Set(next.tracks.map(track => track.id));
    const nextQueue = queue.filter(id => valid.has(id));
    const removedCurrent = !!currentId && !valid.has(currentId);
    removingFolder.current = folder;
    stateRef.current = { library: next, queue: nextQueue, currentId: removedCurrent ? undefined : currentId };
    setLibrary(next); setQueue(nextQueue); setMenu(undefined); setSelection([]);
    if (removedCurrent) {
      ++lyricRequest.current;
      audio.pause(); audio.removeAttribute('src'); audio.load();
      setCurrentId(undefined); setPlaying(false); setPosition(0); setDuration(0); setNowPlaying(false);
      setLyricFile({ text: '', source: '', format: 'lrc' }); setLyricLoading(false);
    }
    setBusy(true);
    // Queue the mutation with settings/favorites writes, while the UI is
    // already cleared. Later writes must not race ahead of the removal.
    const removal = persistence.current.then(async () => {
      try {
        await window.music.removeFolder(folder);
        notify('已移除文件夹，磁盘音乐已保留');
      } catch (error) {
        const restored = await window.music.getLibrary().catch(() => before);
        removingFolder.current = undefined;
        setLibrary(projectLibrary(restored));
        setQueue(q => q.length === nextQueue.length && q.every((id, index) => id === nextQueue[index]) ? queue : [...q, ...queue.filter(id => !q.includes(id))]);
        notify(`移除失败，已恢复音乐库：${error}`);
      } finally {
        removingFolder.current = undefined;
        setBusy(false);
      }
    });
    persistence.current = removal;
    await removal;
  };
  const savePlaylist = (event: React.FormEvent) => {
    event.preventDefault(); if (!playlistDialog?.name.trim() || !library) return;
    const playlists = playlistDialog.id ? library.playlists.map(p => p.id === playlistDialog.id ? { ...p, name: playlistDialog.name.trim() } : p) : [...library.playlists, { id: crypto.randomUUID(), name: playlistDialog.name.trim(), trackIds: playlistDialog.trackIds || [] }];
    persist({ playlists }); setPlaylistDialog(undefined);
    if (!playlistDialog.id && playlistDialog.trackIds?.length) notify(`已创建「${playlistDialog.name.trim()}」并加入 ${playlistDialog.trackIds.length} 首`);
  };
  const renderTransport = (extra?: React.ReactNode) => <div className="transport">
    <IconButton icon={Shuffle} label="随机播放" active={settings?.shuffle} onClick={() => setSettings({ shuffle: !settings?.shuffle })}/>
    <IconButton icon={SkipBack} label="上一首" disabled={!current} onClick={() => advance(-1)}/>
    <PressableButton className="play-button" aria-label={playing ? '暂停' : '播放'} disabled={!library?.tracks.length} whileHover={{ scale: 1.08 }} whileTap={{ scale: .9 }} transition={playTransition} onClick={togglePlay}>
      <AnimatePresence mode="sync" initial={false}>
        {playing
          ? <motion.span key="pause" className="transport-state-icon" initial={{ opacity: 0, scale: .82, rotate: -7 }} animate={{ opacity: 1, scale: 1, rotate: 0 }} exit={{ opacity: 0, scale: .82, rotate: 7 }} transition={playTransition}><Pause size={21} fill="currentColor"/></motion.span>
          : <motion.span key="play" className="transport-state-icon" initial={{ opacity: 0, scale: .82, rotate: 7 }} animate={{ opacity: 1, scale: 1, rotate: 0 }} exit={{ opacity: 0, scale: .82, rotate: -7 }} transition={playTransition}><Play size={21} fill="currentColor"/></motion.span>}
      </AnimatePresence>
    </PressableButton>
    <IconButton icon={SkipForward} label="下一首" disabled={!current} onClick={() => advance(1)}/>
    <IconButton icon={settings?.repeat === 'one' ? Repeat1 : Repeat} label={`循环模式：${settings?.repeat === 'all' ? '列表循环' : settings?.repeat === 'one' ? '单曲循环' : '关闭'}`} active={settings?.repeat !== 'off'} onClick={() => setSettings({ repeat: settings?.repeat === 'off' ? 'all' : settings?.repeat === 'all' ? 'one' : 'off' })}/>
    {extra}
  </div>;
  const renderProgress = () => <div className="progress-control">
    <span>{formatTime(position)}</span>
    <ProgressSlider position={position} duration={duration} disabled={!current || !duration} onSeek={seek}/>
    <span>{formatTime(duration)}</span>
  </div>;
  const copySongText = async (text: string) => {
    try { await navigator.clipboard.writeText(text); notify('已复制'); }
    catch { notify('复制失败，请重试'); }
  };
  const selectedSet = useMemo(() => new Set(selection), [selection]);
  // Batch actions always follow the visible list order, not click order.
  const orderedSelection = useMemo(() => filtered.filter(track => selectedSet.has(track.id)).map(track => track.id), [filtered, selectedSet]);
  const selectRow = (event: React.MouseEvent, id: string) => {
    if ((event.target as Element).closest('button, a, input')) return;
    const ids = filtered.slice(0, limit).map(track => track.id);
    if (event.shiftKey && selectionAnchor.current && ids.includes(selectionAnchor.current)) {
      const [a, b] = [ids.indexOf(selectionAnchor.current), ids.indexOf(id)].sort((x, y) => x - y);
      const range = ids.slice(a, b + 1);
      setSelection(event.ctrlKey || event.metaKey ? [...new Set([...selection, ...range])] : range);
      return;
    }
    selectionAnchor.current = id;
    if (event.ctrlKey || event.metaKey) setSelection(selectedSet.has(id) ? selection.filter(x => x !== id) : [...selection, id]);
    else setSelection([id]);
  };
  const openMenu = (point: { x: number; y: number }, id: string) => {
    const ids = selectedSet.has(id) && orderedSelection.length > 1 ? orderedSelection : [id];
    if (!selectedSet.has(id)) { setSelection([id]); selectionAnchor.current = id; }
    setMenu({ point, ids });
  };
  const playlistMenu = (ids: string[]): MenuItem[] => [
    { label: '新建歌单…', icon: Plus, onSelect: () => setPlaylistDialog({ name: '', trackIds: ids }) },
    ...(library?.playlists.length ? [{ kind: 'separator' } as MenuItem, ...library.playlists.map((p): MenuItem => ({ label: p.name, icon: ListMusic, checked: ids.every(id => p.trackIds.includes(id)), onSelect: () => addToPlaylist(p, ids) }))] : []),
  ];
  const menuItems = (ids: string[]): MenuItem[] => {
    const tracks = ids.map(id => trackById.get(id)).filter((track): track is Track => !!track);
    if (!tracks.length || !library) return [];
    const single = tracks.length === 1 ? tracks[0] : undefined;
    const playingThis = !!single && currentId === single.id && playing;
    const allFavorite = tracks.every(track => library.favorites.includes(track.id));
    const folder = single && folders.find(f => f.trackIds.includes(single.id));
    return [
      ...(single ? [] : [{ kind: 'header', label: `已选择 ${tracks.length} 首歌曲` } as MenuItem]),
      { label: playingThis ? '暂停' : single ? '播放' : '播放所选', icon: playingThis ? Pause : Play, shortcut: 'Enter', onSelect: () => single && currentId === single.id ? togglePlay() : playTrack(ids[0], single ? filtered.map(t => t.id) : ids) },
      { label: '下一首播放', icon: ListPlus, onSelect: () => playNext(ids) },
      { label: '添加到歌单', icon: ListMusic, submenu: playlistMenu(ids) },
      { kind: 'separator' },
      { label: allFavorite ? '取消喜欢' : '喜欢', icon: Heart, onSelect: () => setFavorites(ids, !allFavorite) },
      { label: '复制歌名', icon: Copy, shortcut: 'Ctrl+C', onSelect: () => void copySongText(tracks.map(t => t.title).join('\n')) },
      ...(single ? [
        { label: '复制歌名和歌手', onSelect: () => void copySongText(`${single.title} - ${single.artist}`) },
        { kind: 'separator' },
        { label: `查看歌手：${single.artist}`, icon: Mic2, onSelect: () => { navigate('artists'); setGroup(single.artist); } },
        { label: `查看专辑：${single.album}`, icon: Disc3, onSelect: () => { navigate('albums'); setGroup(single.album); } },
        ...(folder && !selectedFolder ? [{ label: `前往文件夹：${folder.name}`, icon: Folder, onSelect: () => navigate(`folder:${folder.path}`) } as MenuItem] : []),
        { label: '在资源管理器中显示', icon: FolderOpen, onSelect: () => void window.music.reveal(single.id).catch(e => notify(String(e))) },
      ] as MenuItem[] : []),
      { kind: 'separator' },
      ...(selectedPlaylist ? [{ label: '从当前歌单移除', icon: ListX, onSelect: () => removeFromPlaylist(selectedPlaylist, ids) } as MenuItem] : []),
      { label: single ? '从音乐库移除' : `从音乐库移除 ${tracks.length} 首`, icon: Trash2, danger: true, disabled: busy, onSelect: () => void removeTracks(ids) },
    ];
  };
  // Keyboard shortcuts for the current song selection (shown in the context menu).
  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      const element = event.target as HTMLElement;
      if (['INPUT', 'TEXTAREA', 'SELECT'].includes(element.tagName) || element.isContentEditable || element.closest('[role=menu],[role=dialog],[role=slider],[role=listbox]')) return;
      if (nowPlaying || settingsOpen || playlistDialog || deletePlaylist || !document.querySelector('.track-table')) return;
      const ctrl = event.ctrlKey || event.metaKey;
      if (ctrl && event.key.toLowerCase() === 'a') { event.preventDefault(); setSelection(filtered.slice(0, limit).map(track => track.id)); return; }
      if (!orderedSelection.length) return;
      if (ctrl && event.key.toLowerCase() === 'c') { event.preventDefault(); void copySongText(orderedSelection.map(id => trackById.get(id)?.title || '').join('\n')); }
      else if (event.key === 'Enter' && !element.closest('button')) { event.preventDefault(); playTrack(orderedSelection[0], orderedSelection.length > 1 ? orderedSelection : filtered.map(t => t.id)); }
    };
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key);
  });
  const renderRows = (tracks: Track[]) => tracks.map((track, index) => <motion.div layout="position" className={`track-row ${currentId === track.id ? 'current' : ''} ${selectedSet.has(track.id) ? 'selected' : ''}`} key={track.id} role="row" aria-selected={selectedSet.has(track.id)} initial={{ opacity: 0, y: 6 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, x: -10 }} transition={{ duration: .14, ease: smoothEase }} onClick={event => selectRow(event, track.id)} onDoubleClick={event => { if (!event.shiftKey && !event.ctrlKey && !event.metaKey) playTrack(track.id, filtered.map(t => t.id)); }} onContextMenu={event => { event.preventDefault(); openMenu({ x: event.clientX, y: event.clientY }, track.id); }}>
    <span className="row-number">
      <AnimatePresence mode="sync" initial={false}>
        {currentId === track.id
          ? <motion.span key="wave" className="row-wave centered-motion-icon" data-playing={playing ? 'true' : 'false'} aria-label={playing ? '正在播放' : '已暂停'} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={quickTextTransition}><i/><i/><i/><i/></motion.span>
          : <motion.span key="number" className="row-number-content" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={quickTextTransition}><span>{String(index + 1).padStart(2, '0')}</span></motion.span>}
      </AnimatePresence>
    </span>
    <div className="track-identity">
      <PressableButton className="track-cover-play" data-state={currentId === track.id && playing ? 'playing' : 'idle'} aria-label={currentId === track.id && playing ? `暂停 ${track.title}` : `播放 ${track.title}`} onDoubleClick={event => event.stopPropagation()} onClick={event => { if (event.detail > 1) return; if (currentId === track.id) togglePlay(); else playTrack(track.id, filtered.map(t => t.id)); }}>
        <Cover track={track}/>
        <span className="track-cover-overlay" aria-hidden="true">{currentId === track.id && playing ? <Pause size={20} fill="currentColor"/> : <Play size={20} fill="currentColor"/>}</span>
      </PressableButton>
      <span><strong data-tooltip={track.title}>{track.title}</strong><small data-tooltip={track.artist}>{track.artist}</small></span>
    </div>
    <span className="album-cell" data-tooltip={track.album}>{track.album}</span><span className="format-label">{track.format}</span>
    <IconButton icon={Heart} label={`${library?.favorites.includes(track.id) ? '取消喜欢' : '喜欢'} ${track.title}`} active={library?.favorites.includes(track.id)} onClick={() => favorite(track.id)}/>
    <span className="track-duration">{formatTime(track.duration)}</span>
    <IconButton icon={MoreHorizontal} label={`${track.title} 更多操作`} active={!!menu && menu.ids.includes(track.id)} onClick={event => { const box = (event.currentTarget as HTMLElement).getBoundingClientRect(); openMenu({ x: box.right - 4, y: box.bottom + 4 }, track.id); }}/>
  </motion.div>);
  const renderSelectionBar = () => <AnimatePresence>
    {orderedSelection.length > 1 && <motion.div key="selection" className="selection-bar" role="toolbar" aria-label="批量操作" initial={{ opacity: 0, y: -6 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -6 }} transition={layoutTransition}>
      <span className="selection-count"><b>{orderedSelection.length}</b> 首已选</span>
      <span className="selection-divider"/>
      <PressableButton onClick={() => playTrack(orderedSelection[0], orderedSelection)}><Play size={15} fill="currentColor"/>播放</PressableButton>
      <PressableButton onClick={() => playNext(orderedSelection)}><ListPlus size={15}/>下一首播放</PressableButton>
      <PressableButton aria-haspopup="menu" onClick={event => { const box = (event.currentTarget as HTMLElement).getBoundingClientRect(); setMenu({ point: { x: box.left, y: box.top - 6 }, ids: orderedSelection, playlistsOnly: true }); }}><ListMusic size={15}/>添加到歌单</PressableButton>
      <PressableButton onClick={() => setFavorites(orderedSelection, !orderedSelection.every(id => library?.favorites.includes(id)))}><Heart size={15}/>{orderedSelection.every(id => library?.favorites.includes(id)) ? '取消喜欢' : '喜欢'}</PressableButton>
      {selectedPlaylist && <PressableButton onClick={() => removeFromPlaylist(selectedPlaylist, orderedSelection)}><ListX size={15}/>移出歌单</PressableButton>}
      <span className="selection-divider"/>
      <IconButton icon={X} label="取消选择" onClick={() => setSelection([])}/>
    </motion.div>}
  </AnimatePresence>;
  const viewKey = `${page}:${group}:${selectedPlaylist?.id || selectedFolder?.path || ''}`;
  const collectionKey = `${viewKey}:${query.trim()}:${sort}`;
  const renderCollection = () => {
    if (!library) return <motion.div key="library-loading" className="empty-state" initial={{ opacity: 0, y: 7 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -5 }} transition={textTransition}><AudioLines size={36}/><h3>正在打开音乐库</h3></motion.div>;
    if (!filtered.length && matchedFolders.length) return null;
    if (!filtered.length) return <motion.div key={`empty:${collectionKey}`} className="empty-state" initial={{ opacity: 0, y: 9 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -6 }} transition={layoutTransition}>
      <motion.div className="empty-icon" initial={{ rotate: -7 }} animate={{ rotate: 0 }} transition={{ ...layoutTransition, delay: .06 }}>{query ? <Search size={33}/> : page === 'favorites' ? <Heart size={33}/> : <FolderPlus size={33}/>}</motion.div>
      <h3>{query ? '还没有找到这首音乐' : selectedFolder ? '这个文件夹暂时没有音乐' : selectedPlaylist ? '让这个歌单有自己的声音' : page === 'favorites' ? '把喜欢的音乐放在这里' : '把你的音乐带进来'}</h3>
      <p>{query ? '试试歌曲名、艺术家或专辑名。' : selectedFolder ? '重新扫描音乐库后，这里会显示文件夹中的歌曲。' : selectedPlaylist ? '在歌曲的「更多操作」或右键菜单里，将音乐添加到此歌单。' : page === 'favorites' ? '点击歌曲旁的爱心，收藏每一次心动。' : '导入音乐文件夹，自动整理歌曲、专辑与封面。'}<br/>{!query && !collectionPage && page !== 'favorites' && '支持 MP3、FLAC、WAV、M4A、OGG 等常见格式'}</p>
      {!query && page !== 'favorites' && !collectionPage && <PressableButton className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={17}/>选择音乐文件夹<ArrowUpRight size={15}/></PressableButton>}
    </motion.div>;
    if ((page === 'albums' || page === 'artists') && !group) return <motion.div key={`groups:${collectionKey}`} className="album-grid" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      {groups.slice(0, limit).map(([name, tracks]) => <PressableButton
        className="album-card"
        key={name}
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        transition={{ duration: .14, ease: smoothEase }}
        onClick={() => setGroup(name)}
      ><Cover track={tracks[0]} className={page === 'artists' ? 'round' : ''}/><strong>{name}</strong><small>{tracks.length} 首歌曲{page === 'albums' ? ` · ${tracks[0].artist}` : ''}</small></PressableButton>)}
    </motion.div>;
    return <motion.div key={`tracks:${viewKey}`} className="track-table" role="table" aria-label="歌曲列表" initial={{ opacity: .6 }} animate={{ opacity: 1 }} transition={{ duration: .14 }}>
      <motion.div className="track-row table-head" role="row" layout="position"><span>#</span><span>歌曲 / 艺术家</span><span>专辑</span><span>格式</span><span/><span>时长</span><span/></motion.div>
      <AnimatePresence initial={false}>{renderRows(filtered.slice(0, limit))}</AnimatePresence>
    </motion.div>;
  };

  return <MotionConfig reducedMotion="user">
    <motion.div className={`app-shell ${settings?.titleBar === 'webview2' ? 'web-caption-mode' : ''}`} initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ duration: .42, ease: smoothEase }}>
      {settings?.titleBar === 'webview2' && <header className={`web-caption ${nowPlaying ? 'immersive' : ''}`}>
        <span>DR Music</span>
        <div>
          <button aria-label="最小化窗口" data-tooltip="最小化" onClick={() => void window.music.windowCommand('minimize').catch(error => notify(String(error)))}><span className="caption-glyph" aria-hidden="true">{'\uE921'}</span></button>
          <button className={`${captionHot ? 'hot' : ''} ${captionActive ? 'active' : ''}`} aria-label="最大化或还原窗口" data-tooltip={maximized ? '向下还原' : '最大化'} onClick={() => void window.music.windowCommand('maximize').catch(error => notify(String(error)))}><span className="caption-glyph" aria-hidden="true">{maximized ? '\uE923' : '\uE922'}</span></button>
          <button aria-label="关闭窗口" data-tooltip="关闭" onClick={() => void window.music.windowCommand('close').catch(error => notify(String(error)))}><span className="caption-glyph" aria-hidden="true">{'\uE8BB'}</span></button>
        </div>
      </header>}
      <aside className="sidebar">
        <PressableButton className="brand" onClick={() => navigate('songs')}>
          <span className="brand-icon"><AudioLines size={25}/></span>
          <span>DR Music<small>音乐，随心而动。</small></span>
        </PressableButton>
        <div className="sidebar-section">我的音乐</div>
        {([{ id: 'songs', icon: LibraryIcon, label: '音乐库', count: library?.tracks.length }, { id: 'favorites', icon: Heart, label: '我喜欢的', count: library?.favorites.length }, { id: 'albums', icon: Disc3, label: '专辑' }, { id: 'artists', icon: Mic2, label: '艺术家' }] as { id: string; icon: LucideIcon; label: string; count?: number }[]).map(({ id, icon: Icon, label, count }) =>
          <NavButton key={id} selected={page === id} onClick={() => navigate(id)}><Icon size={19}/><span>{label}</span>{count !== undefined && <motion.small key={count} initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={quickTextTransition}>{count}</motion.small>}</NavButton>
        )}
        <div className="sidebar-lists">
        <div className="sidebar-section playlist-label">我的歌单<IconButton icon={Plus} label="新建歌单" onClick={() => setPlaylistDialog({ name: '' })}/></div>
        <motion.div className="playlist-nav" layout="position">
          <AnimatePresence initial={false}>
            {library?.playlists.map(p => <NavButton key={p.id} selected={page === `playlist:${p.id}`} layout="position" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => navigate(`playlist:${p.id}`)}><Cover track={p.trackIds.map(id => trackById.get(id)).find(track => track?.cover)} className="sidebar-playlist-cover" enableLayout={false}/><span>{p.name}</span><small>{p.trackIds.length}</small></NavButton>)}
          </AnimatePresence>
          <AnimatePresence>{!library?.playlists.length && <motion.p className="sidebar-hint" initial={{ opacity: 0 }} animate={{ opacity: .6 }} exit={{ opacity: 0 }}>为不同的心情，<br/>留一张自己的歌单。</motion.p>}</AnimatePresence>
        </motion.div>
        <div className="sidebar-section playlist-label folder-label">
          <button type="button" className="section-toggle" aria-expanded={foldersOpen} aria-controls="folder-nav" onClick={() => setFoldersOpen(open => !open)}><ChevronRight size={13} className="section-chevron"/>文件夹{!foldersOpen && folders.length > 0 && <small>{folders.length}</small>}</button>
          <IconButton icon={FolderPlus} label="添加音乐文件夹" disabled={busy} onClick={() => { setFoldersOpen(true); void runImport('folder'); }}/>
        </div>
        <AnimatePresence initial={false}>
          {foldersOpen && <motion.nav key="folders" id="folder-nav" className="playlist-nav folder-nav" aria-label="文件夹" initial={{ height: 0, opacity: 0 }} animate={{ height: 'auto', opacity: 1 }} exit={{ height: 0, opacity: 0 }} transition={layoutTransition}>
            {folders.map(f => <NavButton key={f.path} selected={page === `folder:${f.path}`} onClick={() => navigate(`folder:${f.path}`)}><Folder size={17} className="folder-nav-icon"/><span data-tooltip={f.path}>{f.name}</span><small>{f.trackIds.length}</small></NavButton>)}
            {!folders.length && <p className="sidebar-hint">添加音乐文件夹后，<br/>这里会按目录整理歌曲。</p>}
          </motion.nav>}
        </AnimatePresence>
        </div>
        <div className="sidebar-bottom"><NavButton onClick={() => setSettingsOpen(true)}><Settings2 size={18}/><span>设置与关于</span><small>0.2.0</small></NavButton></div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div className="breadcrumb">我的音乐 <span>/</span> {selectedPlaylist ? '歌单' : selectedFolder ? '文件夹' : page === 'favorites' ? '我喜欢的' : page === 'albums' ? '专辑' : page === 'artists' ? '艺术家' : '音乐库'}</div>
          <motion.label className="search" layout="position">
            <Search size={17}/>
            <input ref={searchRef} placeholder="搜索歌曲、艺术家、专辑" aria-label="搜索音乐" value={query} onChange={e => setQuery(e.target.value)}/>
            <AnimatePresence mode="wait" initial={false}>{query ? <IconButton key="clear" icon={X} label="清除搜索" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => setQuery('')}/> : <motion.kbd key="shortcut" initial={{ opacity: 0 }} animate={{ opacity: .5 }} exit={{ opacity: 0 }}>Ctrl F</motion.kbd>}</AnimatePresence>
          </motion.label>
        </header>

        <div className={`page-scroll ${collectionPage ? 'playlist-page' : ''}`} onScroll={() => setMenu(undefined)} onClick={event => { if (selection.length && !(event.target as Element).closest('.track-row, button, a, input')) setSelection([]); }}>
          <motion.section className={`hero ${collectionPage ? 'playlist-hero' : ''} ${page === 'favorites' ? 'favorite-hero' : ''}`}>
            {collectionPage && <Cover track={selectedFolder ? folderCover : playlistCover} className="playlist-cover" enableLayout={false}/>}
              <motion.div key={viewKey} className="hero-copy" initial={{ opacity: .65 }} animate={{ opacity: 1 }} transition={{ duration: .14 }}>
                {group && <PressableButton className="back-link" onClick={() => setGroup('')}><ChevronLeft size={16}/>返回{page === 'artists' ? '艺术家' : '专辑'}</PressableButton>}
                {collectionPage && <div className="eyebrow">{selectedFolder ? '文件夹' : '歌单'}</div>}
                <h1>{heading}</h1>
                <p>{library?.tracks.length ? `${filtered.length} 首歌曲${totalDuration ? ` · ${Math.round(totalDuration / 60)} 分钟` : ''}${selectedFolder ? '' : ' · 只属于你的音乐时光'}` : '从一个文件夹开始，把喜欢的声音留在身边。'}</p>
                {selectedFolder && <p className="hero-path" data-tooltip={selectedFolder.path}>{selectedFolder.path}</p>}
                <div className="hero-actions">
                  <PressableButton className="primary-button" disabled={!filtered.length} onClick={() => playTrack(filtered[0].id, filtered.map(t => t.id))}><Play size={16} fill="currentColor"/>播放全部</PressableButton>
                  {collectionPage ? <PressableButton className="secondary-button" disabled={!filtered.length} onClick={() => { setSettings({ shuffle: true }); playTrack(filtered[Math.floor(Math.random() * filtered.length)].id, filtered.map(t => t.id)); }}><Shuffle size={17}/>随机播放</PressableButton> : <PressableButton className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={17}/>导入文件夹</PressableButton>}
                  <AnimatePresence>{selectedPlaylist && <motion.span className="hero-playlist-actions" initial={{ opacity: 0, x: -8 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0, x: -8 }}><IconButton icon={Pencil} label="重命名歌单" onClick={() => setPlaylistDialog({ id: selectedPlaylist.id, name: selectedPlaylist.name })}/><IconButton icon={Trash2} label="删除歌单" onClick={() => setDeletePlaylist(selectedPlaylist)}/></motion.span>}{selectedFolder && <motion.span className="hero-playlist-actions" initial={{ opacity: 0, x: -8 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0, x: -8 }}><IconButton icon={FolderSearch} label="在资源管理器中打开" disabled={!selectedFolder.trackIds.length} onClick={() => void window.music.reveal(selectedFolder.trackIds[0]).catch(e => notify(String(e)))}/></motion.span>}</AnimatePresence>
                </div>
              </motion.div>
            {!collectionPage && <motion.div className="hero-art" aria-hidden="true" initial={{ opacity: .65 }} animate={{ opacity: .85 }} transition={{ duration: .14 }}>
              <motion.div className="hero-orbit" animate={{ rotate: 360 }} transition={{ duration: 22, repeat: Infinity, ease: 'linear' }}/>
              <motion.div className="hero-vinyl" animate={{ rotate: [-18, -13, -18] }} transition={{ duration: 9, repeat: Infinity, ease: 'easeInOut' }}><div className="vinyl-label"><AudioLines size={34}/></div></motion.div>

            </motion.div>}
          </motion.section>

          <AnimatePresence initial={false}>
            {(busy || library?.tracks.some(track => track.metadataPending)) && <motion.div className="scan-status" role="status" initial={{ opacity: 0, height: 0, y: -8 }} animate={{ opacity: 1, height: 'auto', y: 0 }} exit={{ opacity: 0, height: 0, y: -8 }} transition={layoutTransition}><RefreshCw className="spin" size={16}/>{busy ? progress ? `已找到 ${progress.done} 首音乐 · ${progress.name}` : '正在选择或查找音乐…' : '音乐已加入，正在后台完善标签和封面…'}</motion.div>}
          </AnimatePresence>

          <motion.section className="collection">
            {renderSelectionBar()}
            <motion.div className="collection-toolbar">
              <div><h2>{query ? `找到 ${filtered.length} 首歌曲` : group || collectionPage ? '歌曲' : page === 'albums' ? '所有专辑' : page === 'artists' ? '所有艺术家' : '全部歌曲'}</h2><AnimatePresence mode="wait" initial={false}><motion.span key={filtered.length} initial={{ opacity: 0, y: 2 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -2 }} transition={quickTextTransition}>{filtered.length.toString().padStart(2, '0')}</motion.span></AnimatePresence></div>
              <div><PressableButton className="text-button" disabled={busy} onClick={() => void runImport('files')}><Plus size={16}/>添加音乐</PressableButton><Select label="歌曲排序" value={sort} onChange={setSort} options={[{ value: 'added', label: '最近添加' }, { value: 'title', label: '歌曲名称' }, { value: 'artist', label: '艺术家' }]}/></div>
            </motion.div>
            <AnimatePresence initial={false}>{matchedFolders.length > 0 && <motion.div key="folder-results" className="folder-results" role="group" aria-label="匹配的文件夹" initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }} transition={quickTextTransition}>
              <h3>文件夹<span>{matchedFolders.length}</span></h3>
              <div className="folder-result-list">{matchedFolders.slice(0, 12).map(folder => <PressableButton key={folder.path} className="folder-result" aria-label={`打开文件夹 ${folder.name}`} onClick={() => { navigate(`folder:${folder.path}`); setFoldersOpen(true); }}>
                <span className="folder-result-icon"><Folder size={18}/></span>
                <span className="folder-result-text"><strong>{folder.name}</strong><small data-tooltip={folder.path}>{folder.path}</small></span>
                <em>{folder.trackIds.length} 首</em>
              </PressableButton>)}</div>
            </motion.div>}</AnimatePresence>
            {renderCollection()}
            <AnimatePresence>{((page === 'albums' || page === 'artists') && !group ? groups.length : filtered.length) > limit && <motion.div key="load-more" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}><PressableButton className="load-more secondary-button" onClick={() => setLimit(n => n + 100)}>加载更多</PressableButton></motion.div>}</AnimatePresence>
          </motion.section>
        </div>
      </main>

      {menu && <ContextMenu items={menu.playlistsOnly ? playlistMenu(menu.ids) : menuItems(menu.ids)} point={menu.point} label={menu.playlistsOnly ? '添加到歌单' : menu.ids.length > 1 ? `已选 ${menu.ids.length} 首歌曲菜单` : `${trackById.get(menu.ids[0])?.title || ''} 歌曲菜单`} onClose={() => setMenu(undefined)}/>}
      {lyricMenu && current && <ContextMenu label="歌词来源" point={lyricMenu} onClose={() => setLyricMenu(undefined)} items={[
        { kind: 'header', label: '歌词来源' },
        ...(lyricFile.text ? [{ label: '本地歌词', icon: FileMusic, checked: lyric.source === 'local', onSelect: () => chooseLyricSource('local') } as MenuItem] : []),
        ...lyric.results.map((r): MenuItem => ({ label: r.source, icon: Globe, checked: lyric.source === r.provider, shortcut: `${r.score}%`, onSelect: () => chooseLyricSource(r.provider) })),
        ...(lookingOnline ? [{ kind: 'header', label: '正在查找在线歌词…' } as MenuItem] : settings?.onlineLyrics !== false && !lyric.results.length ? [{ kind: 'header', label: '没有找到在线歌词' } as MenuItem] : []),
        { kind: 'separator' },
        { label: '重新搜索在线歌词', icon: RefreshCw, disabled: settings?.onlineLyrics === false || lookingOnline, onSelect: () => loadOnlineLyrics(current.id, true) },
        { label: '关联本地歌词…', icon: Plus, onSelect: () => void attachLyrics() },
      ]}/>}
      {translateMenu && <ContextMenu label="翻译" point={translateMenu} onClose={() => setTranslateMenu(undefined)} items={[
        { kind: 'header', label: settings?.translateProvider === 'niutrans' ? '翻译（小牛翻译）' : '翻译（混元本地模型）' },
        { label: '不翻译', checked: !translateChoice, onSelect: () => setLyricTool({ translate: '' }) },
        ...(['zh', 'en'] as const).map((target): MenuItem => {
          const state = translateReadiness(translateMenu.status, lyricSource, target);
          return { label: target === 'zh' ? '翻译成中文' : '翻译成英文', checked: translateChoice === target, disabled: state !== 'ready',
            shortcut: READINESS_LABEL[state], onSelect: () => setLyricTool({ translate: target }) };
        }),
        ...(translateMenu.status && translateReadiness(translateMenu.status, 'xx', 'en') !== 'ready' ? [{ kind: 'header', label: '在 设置 → 工具 中配置翻译服务' } as MenuItem] : []),
      ]}/>}
      {rubyMenu && <ContextMenu label="注音" point={rubyMenu} onClose={() => setRubyMenu(undefined)} items={[
        { kind: 'header', label: '注音' },
        { label: '不显示', checked: !lyricRuby, onSelect: () => setLyricTool({ ruby: '' }) },
        ...(rubyFits('jyutping') ? [{ label: '粤语（粤拼）', checked: lyricRuby?.kind === 'jyutping', onSelect: () => setLyricTool({ ruby: 'jyutping' }) } as MenuItem] : []),
        // 罗马音: a submenu only when the lyric has its own romanization besides the local reading.
        ...(lyricHasKana && lyricHasRoman ? [{ label: '罗马音', checked: lyricRuby?.kind === 'romaji' || lyricRuby?.kind === 'romaji-online', submenu: [
          { label: lyric.source === 'local' ? '歌词自带' : '在线', checked: lyricRuby?.kind === 'romaji-online', onSelect: () => setLyricTool({ ruby: 'romaji-online' }) },
          { label: '本地', checked: lyricRuby?.kind === 'romaji', onSelect: () => setLyricTool({ ruby: 'romaji' }) },
        ] } as MenuItem]
          : lyricHasKana || lyricHasRoman ? [{ label: '罗马音', checked: lyricRuby?.kind === 'romaji' || lyricRuby?.kind === 'romaji-online', onSelect: () => setLyricTool({ ruby: lyricHasKana ? 'romaji' : 'romaji-online' }) } as MenuItem] : []),
        ...(lyricHasKana ? [{ label: '假名注音', checked: lyricRuby?.kind === 'kana', onSelect: () => setLyricTool({ ruby: 'kana' }) } as MenuItem] : []),
      ]}/>}
      {scriptMenu && <ContextMenu label="繁体标准" point={scriptMenu} onClose={() => setScriptMenu(undefined)} items={[
        { kind: 'header', label: '繁体标准' },
        ...(Object.keys(TRADITIONAL_NAMES) as (keyof typeof TRADITIONAL_NAMES)[]).map((variant): MenuItem => ({ label: TRADITIONAL_NAMES[variant], checked: lyricScript === 't' && lyricVariant === variant, onSelect: () => setSettings({ lyricScript: 't', lyricVariant: variant }) })),
      ]}/>}
      <motion.footer className="player-bar" layout="position">
        <div className="player-track">
          <AnimatePresence mode="wait" initial={false}>
            <motion.div key={current?.id || 'empty'} className="player-track-current" initial={{ opacity: 0, y: 3 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -3 }} transition={textTransition}>
              <PressableButton className="cover-button" aria-label="打开正在播放" disabled={!current} onClick={() => setNowPlaying(true)}><Cover track={current}/></PressableButton>
              <PressableButton className="player-title" disabled={!current} onClick={() => setNowPlaying(true)}><strong>{current?.title || '还没有正在播放的音乐'}</strong><small>{current?.artist || '选一首喜欢的，开始聆听'}</small></PressableButton>
            </motion.div>
          </AnimatePresence>
          <AnimatePresence>{current && <IconButton icon={Heart} label="喜欢当前歌曲" active={library?.favorites.includes(current.id)} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => favorite(current.id)}/>}</AnimatePresence>
        </div>
        <div className="player-center">{renderTransport()}{renderProgress()}</div>
        <div className="player-tools">
          <VolumeControl orientation="vertical" value={settings?.volume ?? .7} onChange={volume => setSettings({ volume })}/>
          <span className="tool-divider"/>
          <IconButton icon={ListMusic} label="播放队列" active={queueOpen} onClick={toggleQueue}/>
          <IconButton icon={Maximize2} label="打开歌词播放页" disabled={!current} onClick={() => setNowPlaying(true)}/>
        </div>
      </motion.footer>

      {current && <section className="now-playing" data-open={nowPlaying ? 'true' : 'false'} aria-label="正在播放" aria-hidden={!nowPlaying} inert={!nowPlaying}>
          <div className="now-bg"><AnimatePresence mode="popLayout" initial={false}>{current.cover && <motion.img key={current.id} src={media(current, 'cover')} alt="" initial={{ opacity: 0, scale: 1.05 }} animate={{ opacity: .7, scale: 1 }} exit={{ opacity: 0, scale: .98 }} transition={{ duration: .58, ease: smoothEase }}/>}</AnimatePresence></div>
          <header><IconButton icon={ChevronDown} label="收起播放页" onClick={() => setNowPlaying(false)}/><div className="now-header-tools">{(!!lyric.file?.text || rubyAvailable) && <span className="lyric-ai-tools">{!!lyric.file?.text && <button type="button" className="ruby-button translate-button" aria-label="翻译" data-tooltip={translateState.state === 'downloading' ? `正在下载翻译模型 ${Math.round(translateState.progress * 100)}%` : translateState.state === 'working' ? '正在翻译…' : '翻译'} aria-haspopup="menu" aria-expanded={!!translateMenu} aria-pressed={!!translateChoice} data-busy={translateState.state === 'working' || translateState.state === 'downloading' ? 'true' : undefined} onClick={event => openTranslateMenu(event.currentTarget.getBoundingClientRect())}>译</button>}{rubyAvailable && <button type="button" className="ruby-button" aria-label="注音" data-tooltip="注音" aria-haspopup="menu" aria-expanded={!!rubyMenu} aria-pressed={!!lyricRuby} onClick={event => { const box = event.currentTarget.getBoundingClientRect(); setRubyMenu(rubyMenu ? undefined : { x: box.left, y: box.bottom + 6 }); }}>音</button>}</span>}<IconButton icon={ListMusic} label="播放队列" onClick={toggleQueue}/></div></header>
          <div className="now-layout">
            <AnimatePresence mode="wait" initial={false}>
              <motion.div key={current.id} className="now-left" initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -3 }} transition={textTransition}>
                <Cover track={current} className="now-cover"/>
                <div className="now-title"><div><h2>{current.title}</h2><p>{current.artist} · {current.album}</p></div><IconButton icon={Heart} label="喜欢当前歌曲" active={library?.favorites.includes(current.id)} onClick={() => favorite(current.id)}/></div>
                {renderProgress()}{renderTransport(<VolumeControl className="transport-volume" orientation="horizontal" label="播放页音量" value={settings?.volume ?? .7} onChange={volume => setSettings({ volume })}/>)}
                <div className="audio-quality">{current.format}{current.sampleRate ? ` · ${(current.sampleRate / 1000).toFixed(1)} kHz` : ''}</div>
              </motion.div>
            </AnimatePresence>
            <div className="now-right">
              <AnimatePresence mode="wait" initial={false}>
                <motion.div key={lyric.file ? `lyrics:${current.id}:${lyric.source}:${lyric.file.source}` : `empty:${current.id}:${lyricLoading || lookingOnline}`} className="lyric-content" initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -3 }} transition={textTransition}>
                  {shownLyric ? <Lyrics file={shownLyric} audio={audio} offset={settings?.lyricOffset || 0} active={nowPlaying} onSeek={seek} typography={{ font: settings?.lyricFont, weight: settings?.lyricWeight, scale: settings?.lyricScale }} ruby={lyricRuby} onRubyError={onRubyError} translate={translateChoice || undefined} onTranslateState={onTranslateState}/> : <div className="no-lyrics"><AudioLines size={38}/><h3>{lyricLoading ? '正在读取歌词' : lookingOnline ? '正在查找在线歌词' : '让文字也跟着旋律'}</h3><p>自动读取歌曲旁的同名歌词文件{settings?.onlineLyrics !== false ? '，没有时从 AMLL TTML DB 查找' : ''}<br/>也可以手动关联 LRC、TTML、YRC 或 TXT</p><PressableButton className="secondary-button" onClick={() => void attachLyrics()}><Plus size={16}/>关联歌词</PressableButton></div>}
                </motion.div>
              </AnimatePresence>
              <div className="lyric-toolbar">
                {(() => { const online = lyric.source && lyric.source !== 'local' ? lyric.results.find(r => r.provider === lyric.source) : undefined; return <PressableButton className="text-button" data-tooltip={online ? `${online.source} · ${online.title} - ${online.artists}（匹配 ${online.score}%）` : undefined} onClick={() => void attachLyrics()}>{online ? <Globe size={15}/> : <FileMusic size={15}/>}{online ? online.source : lyricFile.source || '选择歌词'}</PressableButton>; })()}
                <div>
                  {lyricHasHan && chineseAvailable && <div className="script-switch" role="group" aria-label="歌词写法">{(['o', 's', 't'] as const).map(script => <button key={script} type="button" className="script-option" aria-pressed={lyricScript === script} data-tooltip={script === 'o' ? '歌词原文' : script === 's' ? '转成简体' : `转成${TRADITIONAL_NAMES[lyricVariant]}`} onClick={() => setSettings({ lyricScript: script })}>{script === 'o' ? '原' : script === 's' ? '简' : '繁'}</button>)}<button type="button" className="script-more" aria-label="选择繁体标准" aria-haspopup="menu" aria-expanded={!!scriptMenu} onClick={event => { const box = event.currentTarget.getBoundingClientRect(); setScriptMenu(scriptMenu ? undefined : { x: box.left, y: box.top - 6 }); }}><ChevronDown size={12}/></button></div>}
                  {(lyric.switchable || settings?.onlineLyrics !== false) && <button type="button" className="lyric-source-button" aria-haspopup="menu" aria-expanded={!!lyricMenu} aria-label="歌词来源" onClick={event => { const box = event.currentTarget.getBoundingClientRect(); setLyricMenu(lyricMenu ? undefined : { x: box.left, y: box.top - 6 }); }}>{lookingOnline ? '查找中…' : lyric.results.length ? `${lyric.results.length + (lyricFile.text ? 1 : 0)} 个来源` : '来源'}<ChevronDown size={13}/></button>}
                  <PressableButton data-tooltip="歌词延后 0.5 秒" onClick={() => setSettings({ lyricOffset: Math.max(-30000, (settings?.lyricOffset || 0) - 500) })}>−</PressableButton>
                  <motion.span data-tooltip="正值让歌词提前显示" key={settings?.lyricOffset || 0} initial={{ opacity: .55, y: 1 }} animate={{ opacity: 1, y: 0 }} transition={quickTextTransition}>偏移 {((settings?.lyricOffset || 0) / 1000).toFixed(1)}s</motion.span>
                  <PressableButton data-tooltip="歌词提前 0.5 秒" onClick={() => setSettings({ lyricOffset: Math.min(30000, (settings?.lyricOffset || 0) + 500) })}>+</PressableButton>
                  <PressableButton data-tooltip="重置歌词偏移" onClick={() => setSettings({ lyricOffset: 0 })}>↺</PressableButton>
                </div>
              </div>
            </div>
          </div>
        </section>}

      <AnimatePresence onExitComplete={() => { setQueueContentReady(false); setQueueImagesReady(false); }}>
        {queueOpen && <motion.button key="queue-dismiss" type="button" className="drawer-dismiss" aria-label="关闭队列背景" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => setQueueOpen(false)}/>}
        {queueOpen && <motion.aside key="queue-drawer" className="queue-drawer" initial={{ x: '100%' }} animate={{ x: 0 }} exit={{ x: '100%' }} transition={{ duration: .38, ease: smoothEase }}>
          <div className="drawer-heading"><div><h2>播放队列</h2><motion.p key={queue.length} initial={{ opacity: 0, y: 2 }} animate={{ opacity: 1, y: 0 }} transition={quickTextTransition}>{queue.length} 首音乐</motion.p></div><IconButton icon={X} label="关闭播放队列" onClick={() => setQueueOpen(false)}/></div>
          {queueContentReady && <motion.div ref={queueListRef} className="queue-list" data-virtual-start={queueWindow.start} data-virtual-end={queueWindow.end} initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ duration: .18, ease: smoothEase }}>
            {queue.length ? <div className="queue-virtual-space" role="list" style={{ height: queueWindow.totalHeight }}>
              {queue.slice(queueWindow.start, queueWindow.end).map((id, offset) => {
                const index = queueWindow.start + offset;
                const track = trackById.get(id);
                return track && <div key={`${id}-${index}`} className={`queue-item ${id === currentId ? 'current' : ''}`} role="listitem" aria-setsize={queue.length} aria-posinset={index + 1} style={{ top: index * QUEUE_ROW_HEIGHT }}>
                  <PressableButton onClick={() => playTrack(id)}><Cover track={track} enableLayout={false} loadImage={queueImagesReady}/><span><strong>{track.title}</strong><small>{track.artist}</small></span>{id === currentId && <span className="centered-motion-icon"><AudioLines size={17}/></span>}</PressableButton>
                  <IconButton icon={X} label={`从队列移除 ${track.title}`} onClick={() => setQueue(q => q.filter(x => x !== id))}/>
                </div>;
              })}
            </div> : <div className="empty-state"><ListMusic size={32}/><p>播放歌曲后，队列会出现在这里。</p></div>}
          </motion.div>}
        </motion.aside>}
      </AnimatePresence>

      {settingsOpen && <div className="modal-backdrop" onClick={() => setSettingsOpen(false)}>
          <section className="modal settings-modal" role="dialog" aria-modal="true" aria-label="设置与关于" onClick={e => e.stopPropagation()}>
            <header className="modal-header settings-caption"><h2>设置与关于</h2><button type="button" className="caption-close" aria-label="关闭设置" data-tooltip="关闭" onClick={() => setSettingsOpen(false)}>{'\uE8BB'}</button></header>
            <div className="settings-tabs" role="tablist" aria-label="设置分类">{[{ id: 'appearance', label: '外观', icon: Settings2 }, { id: 'library', label: '音乐库', icon: LibraryIcon }, { id: 'playback', label: '播放与快捷键', icon: Headphones }, { id: 'tools', label: '工具', icon: Languages }, { id: 'about', label: '关于', icon: Music2 }].map(({ id, label, icon: Icon }) => <button key={id} role="tab" aria-selected={settingsTab === id} tabIndex={settingsTab === id ? 0 : -1} onClick={() => setSettingsTab(id)} onKeyDown={event => { const buttons = Array.from(event.currentTarget.parentElement!.querySelectorAll<HTMLButtonElement>('[role=tab]')); const index = buttons.indexOf(event.currentTarget); if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) { event.preventDefault(); const target = buttons[event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 : (index + (event.key === 'ArrowRight' ? 1 : -1) + buttons.length) % buttons.length]; target.click(); target.focus(); } }}><Icon size={16}/>{label}</button>)}</div>
            <div className="settings-body">
              <div className="settings-brand"><span className="brand-icon"><AudioLines size={25}/></span><div><h3>DR Music <small>0.2.0</small></h3><p>你的音乐，你的节奏。个人音乐播放器。</p></div></div>
              <section role="tabpanel" aria-label="外观" hidden={settingsTab !== 'appearance'}><h4>外观</h4><p className="settings-hint">选择适合你的配色和窗口风格。</p>
              <div className="settings-row"><span>应用主题</span><Select label="应用主题" value={settings?.theme || 'dark'} onChange={value => setSettings({ theme: value as Settings['theme'] })} options={[{ value: 'dark', label: '深色 · 夜间唱片店' }, { value: 'light', label: '浅色 · 午后音乐室' }]}/></div>
              <div className="settings-row"><span>原生标题栏</span><Select label="原生标题栏" value={settings?.titleBar || 'direct2d'} onChange={value => setSettings({ titleBar: value as Settings['titleBar'] })} options={[{ value: 'direct2d', label: 'Direct2D 自绘' }, { value: 'webview2', label: 'WebView2 沉浸式' }, { value: 'system', label: 'Windows 系统标题栏' }]}/></div>
              <h5 className="settings-subheading">歌词</h5>
              <div className="settings-row"><span>歌词字体</span><Select label="歌词字体" value={settings?.lyricFont || ''} onChange={value => setSettings({ lyricFont: value })} options={[{ value: '', label: fonts ? '默认 · 跟随应用' : '默认 · 跟随应用（正在读取字体…）' }, ...(fonts || []).map(font => ({ value: font.family, label: font.name, style: { fontFamily: `"${font.family}", var(--font-ui)` } })), ...(settings?.lyricFont && fonts && !fonts.some(font => font.family === settings.lyricFont) ? [{ value: settings.lyricFont, label: `${settings.lyricFont}（未安装）` }] : [])]}/></div>
              <div className="settings-row"><span>歌词粗细</span><Select label="歌词粗细" value={String(settings?.lyricWeight || 0)} onChange={value => setSettings({ lyricWeight: Number(value) })} options={[{ value: '0', label: '默认' }, ...[[300, '细'], [400, '常规'], [500, '中等'], [600, '半粗'], [700, '粗'], [800, '特粗'], [900, '黑']].map(([weight, name]) => ({ value: String(weight), label: `${name} · ${weight}`, style: { fontWeight: weight as number } }))]}/></div>
              <div className="settings-row"><span>歌词大小</span><Select label="歌词大小" value={String(settings?.lyricScale || 1)} onChange={value => setSettings({ lyricScale: Number(value) })} options={[.8, .9, 1, 1.1, 1.2, 1.35, 1.5].map(scale => ({ value: String(scale), label: scale === 1 ? '默认 · 100%' : `${Math.round(scale * 100)}%` }))}/></div>
              <div className="settings-row settings-toggle-row"><span className="settings-row-label"><span>注音另起一行</span><small>播放页右上角「音」可为歌词加注音：中文歌词标粤拼，日文歌词标罗马音或假名，歌词自带罗马音时也可直接显示。关闭时注音标在文字上方，打开后在整行歌词下方单独显示。</small></span><button type="button" role="switch" className="switch" aria-label="注音另起一行" aria-checked={!!settings?.lyricRubyBelow} onClick={() => setSettings({ lyricRubyBelow: !settings?.lyricRubyBelow })}><span/></button></div>
              <div className="lyric-font-preview" aria-label="歌词字体预览" style={lyricTypographyStyle({ font: settings?.lyricFont, weight: settings?.lyricWeight, scale: settings?.lyricScale })}><span>让文字也跟着旋律</span><small>Lyrics follow the melody · 0123</small></div>
              </section><section role="tabpanel" aria-label="音乐库" hidden={settingsTab !== 'library'}><h4>音乐库</h4><p className="settings-hint">添加文件夹时包含子目录，歌曲先加入，标签和封面在后台补齐。重新扫描会更新标签和新增歌曲。移除文件夹会同时清理子目录、收藏和歌单中的歌曲记录，磁盘音乐会保留。</p>
              <div className="folder-list">{library?.folders.map(folder => <div key={folder}><FolderOpen size={16}/><span data-tooltip={folder}>{folder}</span><IconButton icon={X} label={`移除文件夹 ${folder}`} disabled={busy} onClick={() => void removeFolder(folder)}/></div>)}{!library?.folders.length && <p>还没有导入音乐文件夹</p>}</div>
              <div className="settings-actions"><PressableButton className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={16}/>添加文件夹</PressableButton><PressableButton className="secondary-button" disabled={busy || !library?.folders.length} onClick={() => void runImport('rescan')}><RefreshCw size={16}/>重新扫描</PressableButton><PressableButton className="text-button" onClick={() => void window.music.openData().catch(e => notify(String(e)))}>打开数据目录</PressableButton></div>
              <h5 className="settings-subheading">搜索</h5><div className="settings-row settings-toggle-row"><span className="settings-row-label"><span>简繁通搜</span><small>{chineseAvailable ? '输入简体或繁体都能找到另一种写法的歌曲，例如「后来」也能搜到「後來」。' : '未找到 OpenCC 数据（程序目录下的 opencc 文件夹），简繁通搜不可用。'}</small></span><button type="button" role="switch" className="switch" aria-label="简繁通搜" aria-checked={chineseSearch} disabled={!chineseAvailable} onClick={() => setSettings({ chineseSearch: !settings?.chineseSearch })}><span/></button></div>
              <h5 className="settings-subheading">歌词</h5><div className="settings-row settings-toggle-row"><span className="settings-row-label"><span>在线歌词</span><small>歌曲没有歌词时，从下面开启的歌词源同时查找并缓存到本地，选用匹配度最高的；可在播放页右下角切换来源。只会连接这些歌词源的接口。</small></span><button type="button" role="switch" className="switch" aria-label="在线歌词" aria-checked={settings?.onlineLyrics !== false} onClick={() => setSettings({ onlineLyrics: settings?.onlineLyrics === false })}><span/></button></div>
              <div className="lyric-provider-list" aria-label="歌词源" data-disabled={settings?.onlineLyrics === false ? 'true' : undefined}>
                {LYRIC_PROVIDERS.map(provider => { const enabled = (settings?.lyricProviders ?? LYRIC_PROVIDERS.map(p => p.id)).includes(provider.id); return <div key={provider.id} className="lyric-provider-row">
                  <span className="settings-row-label"><span>{provider.name}</span><small>{provider.id === 'apple' ? (appleToken.set ? '已设置 media-user-token（加密保存在本机）' : provider.hint) : provider.hint}</small></span>
                  <button type="button" role="switch" className="switch" aria-label={provider.name} aria-checked={enabled} disabled={settings?.onlineLyrics === false} onClick={() => { const current = settings?.lyricProviders ?? LYRIC_PROVIDERS.map(p => p.id); setSettings({ lyricProviders: enabled ? current.filter(id => id !== provider.id) : [...current, provider.id] }); }}><span/></button>
                </div>; })}
              </div>
              <div className="settings-field"><label htmlFor="amll-mirror">AMLL TTML DB 地址</label><div><input id="amll-mirror" type="url" spellCheck={false} placeholder="https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main" value={mirrorDraft ?? settings?.amllMirror ?? ''} onChange={event => setMirrorDraft(event.target.value)} onKeyDown={event => { if (event.key === 'Enter') saveMirror(); }}/><PressableButton className="secondary-button" disabled={mirrorDraft === undefined} onClick={saveMirror}>保存</PressableButton></div><small>留空使用 GitHub 原始地址；也可以填写仓库根目录的镜像。</small></div>
              <div className="settings-field"><label htmlFor="apple-token">Apple Music media-user-token</label><div><input id="apple-token" type="password" autoComplete="off" spellCheck={false} placeholder={appleToken.set ? '已设置，输入新令牌可替换' : '从 music.apple.com 登录后的 Cookie 中复制'} value={appleToken.draft} onChange={event => setAppleToken(t => ({ ...t, draft: event.target.value }))}/><PressableButton className="secondary-button" disabled={!appleToken.draft.trim()} onClick={() => saveAppleToken(appleToken.draft)}>保存</PressableButton>{appleToken.set && <PressableButton className="text-button" onClick={() => saveAppleToken('')}>清除</PressableButton>}</div><small>只有 Apple Music 订阅用户需要；令牌用 Windows 数据保护加密保存，不会显示或发回页面。</small></div>
              </section><section role="tabpanel" aria-label="播放与快捷键" hidden={settingsTab !== 'playback'}><h4>键盘与播放</h4><div className="shortcut-list"><span>播放 / 暂停 <kbd>Space</kbd></span><span>上一首 / 下一首 <kbd>Ctrl ← / →</kbd></span><span>打开 / 收起播放页 <kbd>Ctrl L</kbd></span><span>喜欢当前歌曲 <kbd>Ctrl D</kbd></span><span>搜索音乐 <kbd>Ctrl F</kbd></span><span>关闭面板 <kbd>Esc</kbd></span></div>
              </section><section role="tabpanel" aria-label="工具" hidden={settingsTab !== 'tools'}><h4>工具</h4><p className="settings-hint">歌词翻译等附加功能的服务与模型。</p>
              <h5 className="settings-subheading">歌词翻译</h5>
              {settingsTab === 'tools' && <TranslationSettings settings={settings} setSettings={setSettings} notify={notify}/>}
              </section><section role="tabpanel" aria-label="关于" hidden={settingsTab !== 'about'}><h4>来源与开源声明</h4><p className="settings-hint">本项目由 suxiaofan 定制，基于 MySpotify 的产品布局与歌词分词逻辑重构。原作者及开源声明保留。歌词使用 Apple Music-like Lyrics 渲染。应用遵循 AGPL-3.0-only。</p>
              <PressableButton className="secondary-button" onClick={() => void window.music.notices().then(setNoticeText).catch(e => notify(String(e)))}>查看原项目及第三方声明</PressableButton>
              <p className="privacy-note"><Check size={15}/>没有账号、遥测或自动更新。音频与音乐库都在你的电脑里；只有开启「在线歌词」时才会向所选歌词源查询歌词。</p></section>
            </div>
          </section>
        </div>}

      {noticeText && <div className="modal-backdrop">
          <section className="modal notices-modal" role="dialog" aria-label="开源声明">
            <div className="modal-header"><h2>开源与来源声明</h2><IconButton icon={X} label="关闭声明" onClick={() => setNoticeText('')}/></div><pre>{noticeText}</pre>
          </section>
        </div>}

      {playlistDialog && <div className="modal-backdrop">
          <form noValidate className="modal small-modal" role="dialog" aria-label={playlistDialog.id ? '重命名歌单' : '新建歌单'} onSubmit={savePlaylist}>
            <div className="modal-header"><h2>{playlistDialog.id ? '重命名歌单' : '新建歌单'}</h2><IconButton icon={X} label="取消" onClick={() => setPlaylistDialog(undefined)}/></div>
            <label>歌单名称<input autoFocus required maxLength={100} placeholder="给这一刻的心情起个名字" value={playlistDialog.name} onChange={e => setPlaylistDialog({ ...playlistDialog, name: e.target.value })}/></label>
            <div className="dialog-actions"><PressableButton type="button" className="secondary-button" onClick={() => setPlaylistDialog(undefined)}>取消</PressableButton><PressableButton className="primary-button" type="submit" disabled={!playlistDialog.name.trim()}>保存歌单</PressableButton></div>
          </form>
        </div>}

      {deletePlaylist && <div className="modal-backdrop">
          <section className="modal small-modal" role="dialog" aria-label="确认删除歌单">
            <h2>删除「{deletePlaylist.name}」？</h2><p>歌单中的歌曲仍保留在音乐库和磁盘上。</p>
            <div className="dialog-actions"><PressableButton className="secondary-button" onClick={() => setDeletePlaylist(undefined)}>取消</PressableButton><PressableButton className="primary-button" onClick={() => { persist({ playlists: library!.playlists.filter(p => p.id !== deletePlaylist.id) }); setDeletePlaylist(undefined); navigate('songs'); }}>删除歌单</PressableButton></div>
          </section>
        </div>}

      <AnimatePresence>
        {toast && <motion.div key={toast} className="toast" role="status" initial={{ opacity: 0, x: '-50%', y: 8 }} animate={{ opacity: 1, x: '-50%', y: 0 }} exit={{ opacity: 0, x: '-50%', y: 6 }} transition={layoutTransition}><span>{toast}</span><IconButton icon={X} label="关闭提示" onClick={() => setToast('')}/></motion.div>}
      </AnimatePresence>
    </motion.div>
  </MotionConfig>;
}
