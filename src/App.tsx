import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { AnimatePresence, motion, MotionConfig, type Transition } from 'motion/react';
import { Music2, Library as LibraryIcon, Heart, Disc3, Mic2, FolderPlus, Plus, Search, Play, Pause, SkipBack, SkipForward, Shuffle, Repeat, Repeat1, Volume2, VolumeX, ListMusic, Maximize2, ChevronDown, ChevronLeft, Settings2, MoreHorizontal, FolderOpen, RefreshCw, X, Check, FileMusic, ArrowUpRight, Headphones, Trash2, Pencil, ListPlus, AudioLines } from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import type { Library, Track, Settings, LyricFile, Progress, Playlist } from './types';
import { formatTime, nextTrack } from './lib/queue';
import { virtualRange } from './lib/virtual';
import { withoutFolder, withMetadata } from './lib/library';
import { Lyrics } from './Lyrics';
import { Select } from './Select';
import { onCaptionButton, onWindowState } from './bridge';

const media = (track: Track, kind = 'audio') => `https://media.dr-music.local/${kind}/${track.id}`;
const smoothEase: [number, number, number, number] = [.22, 1, .36, 1];
const textTransition: Transition = { duration: .36, ease: smoothEase };
const quickTextTransition: Transition = { duration: .24, ease: smoothEase };
const pressTransition: Transition = { duration: .2, ease: smoothEase };
const playTransition = { type: 'spring', stiffness: 340, damping: 32, mass: .78 } as const;
const layoutTransition = { type: 'spring', stiffness: 250, damping: 30, mass: .9 } as const;
const QUEUE_ROW_HEIGHT = 60;
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
  const [failed, setFailed] = useState(false);
  useEffect(() => setFailed(false), [track?.id, track?.cover]);
  return <motion.div layout={enableLayout ? 'position' : false} initial={false} className={`cover ${className}`} style={{ '--cover-hue': String(parseInt(track?.id.slice(0, 3) || '320', 16) % 360) } as React.CSSProperties}>
    <AnimatePresence mode="sync" initial={false}>
      {track?.cover && !failed && loadImage
        ? <motion.img key={`${track.id}:cover`} src={media(track, 'cover')} alt={`${track.album} 封面`} loading="lazy" decoding="async" initial={{ opacity: 0, scale: 1.08 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0, scale: .96 }} transition={{ duration: .24 }} onError={() => setFailed(true)}/>
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
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<Progress>();
  const [lyricFile, setLyricFile] = useState<LyricFile>({ text: '', source: '', format: 'lrc' });
  const [lyricLoading, setLyricLoading] = useState(false);
  const [songMenu, setSongMenu] = useState<string>();
  const [songMenuPoint, setSongMenuPoint] = useState<{ x: number; y: number }>();
  const [playlistDialog, setPlaylistDialog] = useState<{ id?: string; name: string }>();
  const [deletePlaylist, setDeletePlaylist] = useState<Playlist>();
  const [audio] = useState(() => new Audio());
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
  useEffect(() => { setLimit(100); setSongMenu(undefined); }, [query, page, group]);
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

  const trackById = useMemo(() => new Map((library?.tracks || []).map(track => [track.id, track])), [library?.tracks]);
  const current = currentId ? trackById.get(currentId) : undefined;
  const queueWindow = useMemo(() => virtualRange(queue.length, QUEUE_ROW_HEIGHT, queueViewport.scrollTop, queueViewport.height, QUEUE_OVERSCAN), [queue.length, queueViewport]);
  const settings = library?.settings;
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
    } else if (audio.paused) void audio.play().catch(() => notify('无法播放，请检查本地文件。'));
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
  useEffect(() => {
    const time = () => setPosition(audio.currentTime);
    const loaded = () => setDuration(Number.isFinite(audio.duration) ? audio.duration : 0);
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
  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      const element = event.target as HTMLElement;
      const editing = ['INPUT', 'TEXTAREA', 'SELECT'].includes(element.tagName) || element.isContentEditable;
      if (event.key === 'Escape') { setNowPlaying(false); setSettingsOpen(false); setQueueOpen(false); setSongMenu(undefined); setPlaylistDialog(undefined); setDeletePlaylist(undefined); return; }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f') { event.preventDefault(); setNowPlaying(false); searchRef.current?.focus(); return; }
      if (editing || playlistDialog || deletePlaylist || settingsOpen) return;
      if (event.code === 'Space') { event.preventDefault(); togglePlay(); }
      if (event.ctrlKey || event.metaKey) {
        if (event.key === 'ArrowRight') { event.preventDefault(); advance(1); }
        if (event.key === 'ArrowLeft') { event.preventDefault(); advance(-1); }
        if (event.key.toLowerCase() === 'l' && currentId) { event.preventDefault(); setNowPlaying(v => !v); }
        if (event.key.toLowerCase() === 'd' && currentId) { event.preventDefault(); favorite(currentId); }
      }
    };
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key);
  }, [advance, togglePlay, currentId, favorite, playlistDialog, deletePlaylist, settingsOpen]);
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
      if (result) { setLibrary(projectLibrary(result.library)); notify(`已加入 ${result.imported} 首音乐，歌曲信息将在后台补齐${result.warnings.length ? `，${result.warnings.length} 个文件存在读取问题：${result.warnings.slice(0, 2).join('；')}` : ''}`); }
    } catch (error) { notify(`导入失败：${error}`); }
    finally { setBusy(false); setProgress(undefined); }
  };
  const attachLyrics = async () => {
    if (!currentId) return;
    const id = currentId; const token = ++lyricRequest.current;
    try { const file = await window.music.attachLyrics(id); if (file && stateRef.current.currentId === id && token === lyricRequest.current) { setLyricFile(file); notify(`已载入 ${file.source}`); } }
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
  const selectedPlaylist = library?.playlists.find(p => page === `playlist:${p.id}`);
  const playlistCover = selectedPlaylist?.trackIds.map(id => trackById.get(id)).find(track => track?.cover);
  const filtered = useMemo(() => {
    let tracks = library?.tracks || [];
    if (page === 'favorites') tracks = tracks.filter(t => library!.favorites.includes(t.id));
    if (selectedPlaylist) { const map = new Map(tracks.map(t => [t.id, t])); tracks = selectedPlaylist.trackIds.map(id => map.get(id)).filter((t): t is Track => !!t); }
    if (group) tracks = tracks.filter(t => (page === 'artists' ? t.artist : t.album) === group);
    const q = query.trim().toLocaleLowerCase();
    if (q) tracks = tracks.filter(t => `${t.title}\n${t.artist}\n${t.album}\n${t.path}`.toLocaleLowerCase().includes(q));
    return [...tracks].sort((a, b) => sort === 'title' ? a.title.localeCompare(b.title, 'zh-CN') : sort === 'artist' ? a.artist.localeCompare(b.artist, 'zh-CN') : selectedPlaylist ? 0 : b.addedAt - a.addedAt);
  }, [library, page, group, query, sort, selectedPlaylist]);
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
  const heading = query ? '搜索结果' : group || selectedPlaylist?.name || ({ songs: '你的音乐，随时在场。', favorites: '我喜欢的音乐', albums: '专辑收藏', artists: '艺术家' }[page] || '音乐库');
  const addToPlaylist = (playlist: Playlist, id: string) => {
    if (playlist.trackIds.includes(id)) { notify('这首歌已在歌单中'); return; }
    persist({ playlists: library!.playlists.map(p => p.id === playlist.id ? { ...p, trackIds: [...p.trackIds, id] } : p) }); setSongMenu(undefined); notify(`已添加到「${playlist.name}」`);
  };
  const removeTrack = async (id: string) => {
    try {
      await persistence.current;
      const data = await window.music.remove(id); setLibrary(data); setQueue(q => q.filter(x => x !== id));
      if (id === currentId) { audio.pause(); audio.removeAttribute('src'); audio.load(); setCurrentId(undefined); setPosition(0); setDuration(0); setNowPlaying(false); }
      notify('已从音乐库移除，磁盘文件已保留');
    } catch (error) { notify(String(error)); }
    setSongMenu(undefined);
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
    setLibrary(next); setQueue(nextQueue); setSongMenu(undefined);
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
    const playlists = playlistDialog.id ? library.playlists.map(p => p.id === playlistDialog.id ? { ...p, name: playlistDialog.name.trim() } : p) : [...library.playlists, { id: crypto.randomUUID(), name: playlistDialog.name.trim(), trackIds: [] }];
    persist({ playlists }); setPlaylistDialog(undefined);
  };
  const renderTransport = () => <div className="transport">
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
  </div>;
  const renderProgress = () => <div className="progress-control">
    <span>{formatTime(position)}</span>
    <input aria-label="播放进度" type="range" min="0" max={duration || 1} step="0.1" value={Math.min(position, duration || 0)} disabled={!current || !duration} onChange={e => seek(Number(e.target.value))} style={{ '--progress': `${duration ? position / duration * 100 : 0}%` } as React.CSSProperties}/>
    <span>{formatTime(duration)}</span>
  </div>;
  const copySongText = async (text: string) => {
    setSongMenu(undefined);
    try { await navigator.clipboard.writeText(text); notify('已复制'); }
    catch { notify('复制失败，请重试'); }
  };
  const renderRows = (tracks: Track[]) => tracks.map((track, index) => <motion.div layout="position" className={`track-row ${currentId === track.id ? 'current' : ''}`} key={track.id} role="row" initial={{ opacity: 0, y: 6 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, x: -10 }} transition={{ duration: .14, ease: smoothEase }} onDoubleClick={() => playTrack(track.id, filtered.map(t => t.id))} onContextMenu={event => { event.preventDefault(); setSongMenuPoint({ x: event.clientX, y: event.clientY }); setSongMenu(track.id); }}>
    <PressableButton className="row-number" aria-label={`播放 ${track.title}`} onClick={() => playTrack(track.id, filtered.map(t => t.id))}>
      <AnimatePresence mode="sync" initial={false}>
        {currentId === track.id && playing
          ? <motion.span key="playing" className="centered-motion-icon" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={quickTextTransition}><AudioLines size={17}/></motion.span>
          : <motion.span key="number" className="row-number-content" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={quickTextTransition}><span>{String(index + 1).padStart(2, '0')}</span><Play size={15}/></motion.span>}
      </AnimatePresence>
    </PressableButton>
    <PressableButton className="track-identity" onClick={() => playTrack(track.id, filtered.map(t => t.id))}><span className="track-cover-play"><Cover track={track}/><span className="track-cover-overlay" aria-hidden="true"><Play size={22} fill="currentColor"/></span></span><span><strong data-tooltip={track.title}>{track.title}</strong><small data-tooltip={track.artist}>{track.artist}</small></span></PressableButton>
    <span className="album-cell" data-tooltip={track.album}>{track.album}</span><span className="format-label">{track.format}</span>
    <IconButton icon={Heart} label={`${library?.favorites.includes(track.id) ? '取消喜欢' : '喜欢'} ${track.title}`} active={library?.favorites.includes(track.id)} onClick={() => favorite(track.id)}/>
    <span className="track-duration">{formatTime(track.duration)}</span>
    <div className="song-menu-wrap">
      <IconButton icon={MoreHorizontal} label={`${track.title} 更多操作`} onClick={() => { setSongMenuPoint(undefined); setSongMenu(songMenu === track.id ? undefined : track.id); }}/>
      <AnimatePresence>
        {songMenu === track.id && <motion.button key="dismiss" type="button" className="menu-dismiss" aria-label="关闭歌曲菜单" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={quickTextTransition} onClick={() => setSongMenu(undefined)}/>}
        {songMenu === track.id && <motion.div key="menu" className="song-menu" aria-label={`${track.title} 歌曲菜单`} style={songMenuPoint ? { position: 'fixed', left: Math.max(8, Math.min(songMenuPoint.x, innerWidth - 228)), top: Math.max(8, Math.min(songMenuPoint.y, innerHeight - 328)), right: 'auto' } : undefined} onContextMenu={event => { event.preventDefault(); event.stopPropagation(); }} onDoubleClick={event => event.stopPropagation()} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={quickTextTransition}>
          <PressableButton onClick={() => void copySongText(track.title)}>复制歌名</PressableButton>
          <PressableButton onClick={() => void copySongText(`${track.title} - ${track.artist}`)}>复制歌名和歌手</PressableButton>
          <hr/>
          <PressableButton onClick={() => { navigate('artists'); setGroup(track.artist); setSongMenu(undefined); }}>查看歌手：{track.artist}</PressableButton>
          <PressableButton onClick={() => { navigate('albums'); setGroup(track.album); setSongMenu(undefined); }}>查看专辑：{track.album}</PressableButton>
          <hr/>
          <PressableButton onClick={() => { setQueue(q => { const copy = q.filter(id => id !== track.id); const at = copy.indexOf(currentId || ''); copy.splice(at < 0 ? copy.length : at + 1, 0, track.id); return copy; }); setSongMenu(undefined); notify('已加入下一首播放'); }}><ListPlus size={16}/>下一首播放</PressableButton>
          <PressableButton onClick={() => { void window.music.reveal(track.id).catch(e => notify(String(e))); setSongMenu(undefined); }}><FolderOpen size={16}/>在文件夹中显示</PressableButton>
          {selectedPlaylist && <PressableButton onClick={() => { persist({ playlists: library!.playlists.map(p => p.id === selectedPlaylist.id ? { ...p, trackIds: p.trackIds.filter(id => id !== track.id) } : p) }); setSongMenu(undefined); }}>从当前歌单移除</PressableButton>}
          <small>添加到歌单</small>
          {library?.playlists.length
            ? library.playlists.map(p => <PressableButton key={p.id} onClick={() => addToPlaylist(p, track.id)}><ListMusic size={16}/>{p.name}</PressableButton>)
            : <PressableButton onClick={() => { setSongMenu(undefined); setPlaylistDialog({ name: '' }); }}>先创建一个歌单</PressableButton>}
          <hr/><PressableButton className="danger" disabled={busy} onClick={() => void removeTrack(track.id)}><Trash2 size={16}/>从音乐库移除</PressableButton>
        </motion.div>}
      </AnimatePresence>
    </div>
  </motion.div>);
  const viewKey = `${page}:${group}:${selectedPlaylist?.id || ''}`;
  const collectionKey = `${viewKey}:${query.trim()}:${sort}`;
  const renderCollection = () => {
    if (!library) return <motion.div key="library-loading" className="empty-state" initial={{ opacity: 0, y: 7 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -5 }} transition={textTransition}><AudioLines size={36}/><h3>正在打开本地音乐库</h3></motion.div>;
    if (!filtered.length) return <motion.div key={`empty:${collectionKey}`} className="empty-state" initial={{ opacity: 0, y: 9 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -6 }} transition={layoutTransition}>
      <motion.div className="empty-icon" initial={{ rotate: -7 }} animate={{ rotate: 0 }} transition={{ ...layoutTransition, delay: .06 }}>{query ? <Search size={33}/> : page === 'favorites' ? <Heart size={33}/> : <FolderPlus size={33}/>}</motion.div>
      <h3>{query ? '还没有找到这首音乐' : selectedPlaylist ? '让这个歌单有自己的声音' : page === 'favorites' ? '把喜欢的音乐放在这里' : '把你的音乐带进来'}</h3>
      <p>{query ? '试试歌曲名、艺术家或专辑名。' : selectedPlaylist ? '在歌曲的「更多操作」里，将音乐添加到此歌单。' : page === 'favorites' ? '点击歌曲旁的爱心，收藏每一次心动。' : '导入本地文件夹，自动整理歌曲、专辑与封面。'}<br/>{!query && !selectedPlaylist && page !== 'favorites' && '支持 MP3、FLAC、WAV、M4A、OGG 等常见格式'}</p>
      {!query && page !== 'favorites' && !selectedPlaylist && <PressableButton className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={17}/>选择音乐文件夹<ArrowUpRight size={15}/></PressableButton>}
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
    return <motion.div key={`tracks:${viewKey}`} className="track-table" role="table" aria-label="本地歌曲列表" initial={{ opacity: .6 }} animate={{ opacity: 1 }} transition={{ duration: .14 }}>
      <motion.div className="track-row table-head" role="row" layout="position"><span>#</span><span>歌曲 / 艺术家</span><span>专辑</span><span>格式</span><span/><span>时长</span><span/></motion.div>
      <AnimatePresence initial={false}>{renderRows(filtered.slice(0, limit))}</AnimatePresence>
    </motion.div>;
  };

  return <MotionConfig reducedMotion="user" transition={layoutTransition}>
    <motion.div className={`app-shell ${settings?.titleBar === 'webview2' ? 'web-caption-mode' : ''}`} initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ duration: .42, ease: smoothEase }}>
      {settings?.titleBar === 'webview2' && <header className={`web-caption ${nowPlaying ? 'immersive' : ''}`}>
        <span>DR Music</span>
        <div>
          <button aria-label="最小化窗口" data-tooltip="最小化" onClick={() => void window.music.windowCommand('minimize').catch(error => notify(String(error)))}><svg viewBox="0 0 12 12"><path d="M1 6h10"/></svg></button>
          <button className={`${captionHot ? 'hot' : ''} ${captionActive ? 'active' : ''}`} aria-label="最大化或还原窗口" data-tooltip={maximized ? '向下还原' : '最大化'} onClick={() => void window.music.windowCommand('maximize').catch(error => notify(String(error)))}><svg viewBox="0 0 12 12">{maximized ? <><path d="M4 1h7v7"/><rect x="1" y="4" width="7" height="7"/></> : <rect x="1.5" y="1.5" width="9" height="9"/>}</svg></button>
          <button aria-label="关闭窗口" data-tooltip="关闭" onClick={() => void window.music.windowCommand('close').catch(error => notify(String(error)))}><svg viewBox="0 0 12 12"><path d="m1 1 10 10M1 11 11 1"/></svg></button>
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
        <div className="sidebar-section playlist-label">我的歌单<IconButton icon={Plus} label="新建歌单" onClick={() => setPlaylistDialog({ name: '' })}/></div>
        <motion.div className="playlist-nav" layout="position">
          <AnimatePresence initial={false}>
            {library?.playlists.map(p => <NavButton key={p.id} selected={page === `playlist:${p.id}`} layout="position" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => navigate(`playlist:${p.id}`)}><Cover track={p.trackIds.map(id => trackById.get(id)).find(track => track?.cover)} className="sidebar-playlist-cover" enableLayout={false}/><span>{p.name}</span><small>{p.trackIds.length}</small></NavButton>)}
          </AnimatePresence>
          <AnimatePresence>{!library?.playlists.length && <motion.p className="sidebar-hint" initial={{ opacity: 0 }} animate={{ opacity: .6 }} exit={{ opacity: 0 }}>为不同的心情，<br/>留一张自己的歌单。</motion.p>}</AnimatePresence>
        </motion.div>
        <div className="sidebar-bottom"><NavButton onClick={() => setSettingsOpen(true)}><Settings2 size={18}/><span>设置与关于</span><small>0.2.0</small></NavButton></div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div className="breadcrumb">我的音乐 <span>/</span> {selectedPlaylist ? '歌单' : page === 'favorites' ? '我喜欢的' : page === 'albums' ? '专辑' : page === 'artists' ? '艺术家' : '音乐库'}</div>
          <motion.label className="search" layout="position">
            <Search size={17}/>
            <input ref={searchRef} placeholder="搜索歌曲、艺术家、专辑" aria-label="搜索本地音乐" value={query} onChange={e => setQuery(e.target.value)}/>
            <AnimatePresence mode="wait" initial={false}>{query ? <IconButton key="clear" icon={X} label="清除搜索" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => setQuery('')}/> : <motion.kbd key="shortcut" initial={{ opacity: 0 }} animate={{ opacity: .5 }} exit={{ opacity: 0 }}>Ctrl F</motion.kbd>}</AnimatePresence>
          </motion.label>
          <motion.span className="local-badge" animate={{ opacity: [.6, 1, .6] }} transition={{ duration: 3, repeat: Infinity }}><Headphones size={15}/>LOCAL</motion.span>
        </header>

        <div className={`page-scroll ${selectedPlaylist ? 'playlist-page' : ''}`} onScroll={() => setSongMenu(undefined)}>
          <motion.section className={`hero ${selectedPlaylist ? 'playlist-hero' : ''} ${page === 'favorites' ? 'favorite-hero' : ''}`}>
            {selectedPlaylist && <Cover track={playlistCover} className="playlist-cover" enableLayout={false}/>}
              <motion.div key={viewKey} className="hero-copy" initial={{ opacity: .65 }} animate={{ opacity: 1 }} transition={{ duration: .14 }}>
                {group && <PressableButton className="back-link" onClick={() => setGroup('')}><ChevronLeft size={16}/>返回{page === 'artists' ? '艺术家' : '专辑'}</PressableButton>}
                {selectedPlaylist && <div className="eyebrow">歌单</div>}
                <h1>{heading}</h1>
                <p>{library?.tracks.length ? `${filtered.length} 首歌曲${totalDuration ? ` · ${Math.round(totalDuration / 60)} 分钟` : ''} · 只属于你的音乐时光` : '从一个文件夹开始，把喜欢的声音留在身边。'}</p>
                <div className="hero-actions">
                  <PressableButton className="primary-button" disabled={!filtered.length} onClick={() => playTrack(filtered[0].id, filtered.map(t => t.id))}><Play size={16} fill="currentColor"/>播放全部</PressableButton>
                  {selectedPlaylist ? <PressableButton className="secondary-button" disabled={!filtered.length} onClick={() => { setSettings({ shuffle: true }); playTrack(filtered[Math.floor(Math.random() * filtered.length)].id, filtered.map(t => t.id)); }}><Shuffle size={17}/>随机播放</PressableButton> : <PressableButton className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={17}/>导入文件夹</PressableButton>}
                  <AnimatePresence>{selectedPlaylist && <motion.span className="hero-playlist-actions" initial={{ opacity: 0, x: -8 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0, x: -8 }}><IconButton icon={Pencil} label="重命名歌单" onClick={() => setPlaylistDialog({ id: selectedPlaylist.id, name: selectedPlaylist.name })}/><IconButton icon={Trash2} label="删除歌单" onClick={() => setDeletePlaylist(selectedPlaylist)}/></motion.span>}</AnimatePresence>
                </div>
              </motion.div>
            {!selectedPlaylist && <motion.div className="hero-art" aria-hidden="true" initial={{ opacity: .65 }} animate={{ opacity: .85 }} transition={{ duration: .14 }}>
              <motion.div className="hero-orbit" animate={{ rotate: 360 }} transition={{ duration: 22, repeat: Infinity, ease: 'linear' }}/>
              <motion.div className="hero-vinyl" animate={{ rotate: [-18, -13, -18] }} transition={{ duration: 9, repeat: Infinity, ease: 'easeInOut' }}><div className="vinyl-label"><AudioLines size={34}/></div></motion.div>

            </motion.div>}
          </motion.section>

          <AnimatePresence initial={false}>
            {(busy || library?.tracks.some(track => track.metadataPending)) && <motion.div className="scan-status" role="status" initial={{ opacity: 0, height: 0, y: -8 }} animate={{ opacity: 1, height: 'auto', y: 0 }} exit={{ opacity: 0, height: 0, y: -8 }} transition={layoutTransition}><RefreshCw className="spin" size={16}/>{busy ? progress ? `已找到 ${progress.done} 首音乐 · ${progress.name}` : '正在选择或查找本地音乐…' : '音乐已加入，正在后台完善标签和封面…'}</motion.div>}
          </AnimatePresence>

          <motion.section className="collection">
            <motion.div className="collection-toolbar">
              <div><h2>{query ? `找到 ${filtered.length} 首歌曲` : group || selectedPlaylist ? '歌曲' : page === 'albums' ? '所有专辑' : page === 'artists' ? '所有艺术家' : '全部歌曲'}</h2><AnimatePresence mode="wait" initial={false}><motion.span key={filtered.length} initial={{ opacity: 0, y: 2 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -2 }} transition={quickTextTransition}>{filtered.length.toString().padStart(2, '0')}</motion.span></AnimatePresence></div>
              <div><PressableButton className="text-button" disabled={busy} onClick={() => void runImport('files')}><Plus size={16}/>添加音乐</PressableButton><Select label="歌曲排序" value={sort} onChange={setSort} options={[{ value: 'added', label: '最近添加' }, { value: 'title', label: '歌曲名称' }, { value: 'artist', label: '艺术家' }]}/></div>
            </motion.div>
            {renderCollection()}
            <AnimatePresence>{((page === 'albums' || page === 'artists') && !group ? groups.length : filtered.length) > limit && <motion.div key="load-more" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}><PressableButton className="load-more secondary-button" onClick={() => setLimit(n => n + 100)}>加载更多</PressableButton></motion.div>}</AnimatePresence>
          </motion.section>
          {!selectedPlaylist && <motion.footer className="library-footer" initial={{ opacity: 0 }} animate={{ opacity: .6 }}><span><span className="tiny-dot"/>音乐在本地，热爱也是。</span></motion.footer>}
        </div>
      </main>

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
          <IconButton icon={settings?.volume === 0 ? VolumeX : Volume2} label="静音 / 恢复音量" onClick={() => setSettings({ volume: settings?.volume ? 0 : 0.7 })}/>
          <input className="volume-slider" aria-label="音量" type="range" min="0" max="1" step="0.01" value={settings?.volume ?? 0.7} onChange={e => setSettings({ volume: Number(e.target.value) })} style={{ '--progress': `${(settings?.volume ?? 0.7) * 100}%` } as React.CSSProperties}/>
          <span className="tool-divider"/>
          <IconButton icon={ListMusic} label="播放队列" active={queueOpen} onClick={toggleQueue}/>
          <IconButton icon={Maximize2} label="打开歌词播放页" disabled={!current} onClick={() => setNowPlaying(true)}/>
        </div>
      </motion.footer>

      {current && <section className="now-playing" data-open={nowPlaying ? 'true' : 'false'} aria-label="正在播放" aria-hidden={!nowPlaying} inert={!nowPlaying}>
          <div className="now-bg"><AnimatePresence mode="popLayout" initial={false}>{current.cover && <motion.img key={current.id} src={media(current, 'cover')} alt="" initial={{ opacity: 0, scale: 1.05 }} animate={{ opacity: .7, scale: 1 }} exit={{ opacity: 0, scale: .98 }} transition={{ duration: .58, ease: smoothEase }}/>}</AnimatePresence></div>
          <header><IconButton icon={ChevronDown} label="收起播放页" onClick={() => setNowPlaying(false)}/><IconButton icon={ListMusic} label="播放队列" onClick={toggleQueue}/></header>
          <div className="now-layout">
            <AnimatePresence mode="wait" initial={false}>
              <motion.div key={current.id} className="now-left" initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -3 }} transition={textTransition}>
                <Cover track={current} className="now-cover"/>
                <div className="now-title"><div><h2>{current.title}</h2><p>{current.artist} · {current.album}</p></div><IconButton icon={Heart} label="喜欢当前歌曲" active={library?.favorites.includes(current.id)} onClick={() => favorite(current.id)}/></div>
                {renderProgress()}{renderTransport()}
                <div className="audio-quality">{current.format}{current.sampleRate ? ` · ${(current.sampleRate / 1000).toFixed(1)} kHz` : ''} · 本地播放</div>
              </motion.div>
            </AnimatePresence>
            <div className="now-right">
              <AnimatePresence mode="wait" initial={false}>
                <motion.div key={lyricFile.text ? `lyrics:${current.id}:${lyricFile.source}` : `empty:${current.id}:${lyricLoading}`} className="lyric-content" initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -3 }} transition={textTransition}>
                  {lyricFile.text ? <Lyrics file={lyricFile} audio={audio} offset={settings?.lyricOffset || 0} active={nowPlaying} onSeek={seek}/> : <div className="no-lyrics"><AudioLines size={38}/><h3>{lyricLoading ? '正在读取本地歌词' : '让文字也跟着旋律'}</h3><p>自动读取歌曲旁的同名歌词文件<br/>也可以手动关联 LRC、TTML、YRC 或 TXT</p><PressableButton className="secondary-button" onClick={() => void attachLyrics()}><Plus size={16}/>关联本地歌词</PressableButton></div>}
                </motion.div>
              </AnimatePresence>
              <div className="lyric-toolbar">
                <PressableButton className="text-button" onClick={() => void attachLyrics()}><FileMusic size={15}/>{lyricFile.source || '选择歌词'}</PressableButton>
                <div>
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
          <div className="drawer-heading"><div><h2>播放队列</h2><motion.p key={queue.length} initial={{ opacity: 0, y: 2 }} animate={{ opacity: 1, y: 0 }} transition={quickTextTransition}>{queue.length} 首本地音乐</motion.p></div><IconButton icon={X} label="关闭播放队列" onClick={() => setQueueOpen(false)}/></div>
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

      <AnimatePresence>
        {settingsOpen && <motion.div key="settings-backdrop" className="modal-backdrop" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => setSettingsOpen(false)}>
          <motion.section className="modal settings-modal" role="dialog" aria-modal="true" aria-label="设置与关于" initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 8 }} transition={layoutTransition} onClick={e => e.stopPropagation()}>
            <div className="modal-header"><h2>设置与关于</h2><IconButton icon={X} label="关闭设置" onClick={() => setSettingsOpen(false)}/></div>
            <div className="settings-tabs" role="tablist" aria-label="设置分类">{[{ id: 'appearance', label: '外观', icon: Settings2 }, { id: 'library', label: '音乐库', icon: LibraryIcon }, { id: 'playback', label: '播放与快捷键', icon: Headphones }, { id: 'about', label: '关于', icon: Music2 }].map(({ id, label, icon: Icon }) => <button key={id} role="tab" aria-selected={settingsTab === id} tabIndex={settingsTab === id ? 0 : -1} onClick={() => setSettingsTab(id)} onKeyDown={event => { const buttons = Array.from(event.currentTarget.parentElement!.querySelectorAll<HTMLButtonElement>('[role=tab]')); const index = buttons.indexOf(event.currentTarget); if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) { event.preventDefault(); const target = buttons[event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 : (index + (event.key === 'ArrowRight' ? 1 : -1) + buttons.length) % buttons.length]; target.click(); target.focus(); } }}><Icon size={16}/>{label}</button>)}</div>
            <div className="settings-body">
              <div className="settings-brand"><span className="brand-icon"><AudioLines size={25}/></span><div><h3>DR Music <small>0.2.0</small></h3><p>你的音乐，你的节奏。完全本地的个人音乐播放器。</p></div></div>
              <section role="tabpanel" aria-label="外观" hidden={settingsTab !== 'appearance'}><h4>外观</h4><p className="settings-hint">选择适合你的配色和窗口风格。</p>
              <div className="settings-row"><span>应用主题</span><Select label="应用主题" value={settings?.theme || 'dark'} onChange={value => setSettings({ theme: value as Settings['theme'] })} options={[{ value: 'dark', label: '深色 · 夜间唱片店' }, { value: 'light', label: '浅色 · 午后音乐室' }]}/></div>
              <div className="settings-row"><span>原生标题栏</span><Select label="原生标题栏" value={settings?.titleBar || 'direct2d'} onChange={value => setSettings({ titleBar: value as Settings['titleBar'] })} options={[{ value: 'direct2d', label: 'Direct2D 自绘' }, { value: 'webview2', label: 'WebView2 沉浸式' }, { value: 'system', label: 'Windows 系统标题栏' }]}/></div>
              </section><section role="tabpanel" aria-label="音乐库" hidden={settingsTab !== 'library'}><h4>本地音乐库</h4><p className="settings-hint">添加文件夹时包含子目录，歌曲先加入，标签和封面在后台补齐。重新扫描会更新标签和新增歌曲。移除文件夹会同时清理子目录、收藏和歌单中的歌曲记录，磁盘音乐会保留。</p>
              <div className="folder-list">{library?.folders.map(folder => <div key={folder}><FolderOpen size={16}/><span data-tooltip={folder}>{folder}</span><IconButton icon={X} label={`移除文件夹 ${folder}`} disabled={busy} onClick={() => void removeFolder(folder)}/></div>)}{!library?.folders.length && <p>还没有导入音乐文件夹</p>}</div>
              <div className="settings-actions"><PressableButton className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={16}/>添加文件夹</PressableButton><PressableButton className="secondary-button" disabled={busy || !library?.folders.length} onClick={() => void runImport('rescan')}><RefreshCw size={16}/>重新扫描</PressableButton><PressableButton className="text-button" onClick={() => void window.music.openData().catch(e => notify(String(e)))}>打开数据目录</PressableButton></div>
              </section><section role="tabpanel" aria-label="播放与快捷键" hidden={settingsTab !== 'playback'}><h4>键盘与播放</h4><div className="shortcut-list"><span>播放 / 暂停 <kbd>Space</kbd></span><span>上一首 / 下一首 <kbd>Ctrl ← / →</kbd></span><span>打开 / 收起播放页 <kbd>Ctrl L</kbd></span><span>喜欢当前歌曲 <kbd>Ctrl D</kbd></span><span>搜索音乐 <kbd>Ctrl F</kbd></span><span>关闭面板 <kbd>Esc</kbd></span></div>
              </section><section role="tabpanel" aria-label="关于" hidden={settingsTab !== 'about'}><h4>来源与开源声明</h4><p className="settings-hint">本项目由 suxiaofan 定制，基于 MySpotify 的产品布局与歌词分词逻辑重构。原作者及开源声明保留。歌词使用 Apple Music-like Lyrics 渲染。应用遵循 AGPL-3.0-only。</p>
              <PressableButton className="secondary-button" onClick={() => void window.music.notices().then(setNoticeText).catch(e => notify(String(e)))}>查看原项目及第三方声明</PressableButton>
              <p className="privacy-note"><Check size={15}/>没有账号、在线搜索、遥测或自动更新。音频、歌词与音乐库都在你的电脑里。</p></section>
            </div>
          </motion.section>
        </motion.div>}
      </AnimatePresence>

      <AnimatePresence>
        {noticeText && <motion.div key="notice-backdrop" className="modal-backdrop" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
          <motion.section className="modal notices-modal" role="dialog" aria-label="开源声明" initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 8 }} transition={layoutTransition}>
            <div className="modal-header"><h2>开源与来源声明</h2><IconButton icon={X} label="关闭声明" onClick={() => setNoticeText('')}/></div><pre>{noticeText}</pre>
          </motion.section>
        </motion.div>}
      </AnimatePresence>

      <AnimatePresence>
        {playlistDialog && <motion.div key="playlist-backdrop" className="modal-backdrop" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
          <motion.form noValidate className="modal small-modal" role="dialog" aria-label={playlistDialog.id ? '重命名歌单' : '新建歌单'} initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 7 }} transition={layoutTransition} onSubmit={savePlaylist}>
            <div className="modal-header"><h2>{playlistDialog.id ? '重命名歌单' : '新建歌单'}</h2><IconButton icon={X} label="取消" onClick={() => setPlaylistDialog(undefined)}/></div>
            <label>歌单名称<input autoFocus required maxLength={100} placeholder="给这一刻的心情起个名字" value={playlistDialog.name} onChange={e => setPlaylistDialog({ ...playlistDialog, name: e.target.value })}/></label>
            <div className="dialog-actions"><PressableButton type="button" className="secondary-button" onClick={() => setPlaylistDialog(undefined)}>取消</PressableButton><PressableButton className="primary-button" type="submit" disabled={!playlistDialog.name.trim()}>保存歌单</PressableButton></div>
          </motion.form>
        </motion.div>}
      </AnimatePresence>

      <AnimatePresence>
        {deletePlaylist && <motion.div key="delete-backdrop" className="modal-backdrop" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
          <motion.section className="modal small-modal" role="dialog" aria-label="确认删除歌单" initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 7 }} transition={layoutTransition}>
            <h2>删除「{deletePlaylist.name}」？</h2><p>歌单中的歌曲仍保留在音乐库和磁盘上。</p>
            <div className="dialog-actions"><PressableButton className="secondary-button" onClick={() => setDeletePlaylist(undefined)}>取消</PressableButton><PressableButton className="primary-button" onClick={() => { persist({ playlists: library!.playlists.filter(p => p.id !== deletePlaylist.id) }); setDeletePlaylist(undefined); navigate('songs'); }}>删除歌单</PressableButton></div>
          </motion.section>
        </motion.div>}
      </AnimatePresence>

      <AnimatePresence>
        {toast && <motion.div key={toast} className="toast" role="status" initial={{ opacity: 0, x: '-50%', y: 8 }} animate={{ opacity: 1, x: '-50%', y: 0 }} exit={{ opacity: 0, x: '-50%', y: 6 }} transition={layoutTransition}><span>{toast}</span><IconButton icon={X} label="关闭提示" onClick={() => setToast('')}/></motion.div>}
      </AnimatePresence>
    </motion.div>
  </MotionConfig>;
}
