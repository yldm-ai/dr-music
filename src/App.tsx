import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Music2, Library as LibraryIcon, Heart, Disc3, Mic2, FolderPlus, Plus, Search, Play, Pause, SkipBack, SkipForward, Shuffle, Repeat, Repeat1, Volume2, VolumeX, ListMusic, Maximize2, ChevronDown, ChevronLeft, Settings2, MoreHorizontal, FolderOpen, RefreshCw, X, Check, FileMusic, ArrowUpRight, Headphones, Trash2, Pencil, ListPlus, AudioLines } from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import type { Library, Track, Settings, LyricFile, Progress, Playlist } from './types';
import { formatTime, nextTrack } from './lib/queue';
import { Lyrics } from './Lyrics';

const media = (track: Track, kind = 'audio') => `https://media.dr-music.local/${kind}/${track.id}`;
function Cover({ track, className = '' }: { track?: Track; className?: string }) {
  const [failed, setFailed] = useState(false);
  useEffect(() => setFailed(false), [track?.id, track?.cover]);
  return <div className={`cover ${className}`} style={{ '--cover-hue': String(parseInt(track?.id.slice(0, 3) || '320', 16) % 360) } as React.CSSProperties}>
    {track?.cover && !failed ? <img src={media(track, 'cover')} alt={`${track.album} 封面`} onError={() => setFailed(true)}/> : <><span className="record"><i/></span><Music2 className="cover-note"/></>}
  </div>;
}
function IconButton({ icon: Icon, label, active, ...props }: { icon: LucideIcon; label: string; active?: boolean } & React.ButtonHTMLAttributes<HTMLButtonElement>) {
  return <button type="button" className={`icon-button ${active ? 'active' : ''}`} title={label} aria-label={label} {...props}><Icon size={19}/></button>;
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
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [noticeText, setNoticeText] = useState('');
  const [toast, setToast] = useState('');
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<Progress>();
  const [lyricFile, setLyricFile] = useState<LyricFile>({ text: '', source: '', format: 'lrc' });
  const [lyricLoading, setLyricLoading] = useState(false);
  const [songMenu, setSongMenu] = useState<string>();
  const [playlistDialog, setPlaylistDialog] = useState<{ id?: string; name: string }>();
  const [deletePlaylist, setDeletePlaylist] = useState<Playlist>();
  const [audio] = useState(() => new Audio());
  const stateRef = useRef({ library, queue, currentId }); stateRef.current = { library, queue, currentId };
  const searchRef = useRef<HTMLInputElement>(null);
  const lyricRequest = useRef(0);
  const persistence = useRef(Promise.resolve());
  const notify = useCallback((message: string) => setToast(message), []);
  useEffect(() => { if (toast) { const timer = setTimeout(() => setToast(''), 5000); return () => clearTimeout(timer); } }, [toast]);
  useEffect(() => {
    if (!window.music) { notify('请通过 C++ / WebView2 桌面程序打开'); return; }
    window.music.getLibrary().then(data => { setLibrary(data); if (data.notice) notify(data.notice); }).catch(e => notify(String(e)));
    return window.music.onProgress(setProgress);
  }, [notify]);
  useEffect(() => { document.documentElement.dataset.theme = library?.settings.theme || 'dark'; }, [library?.settings.theme]);
  useEffect(() => { audio.volume = library?.settings.volume ?? 0.7; }, [audio, library?.settings.volume]);
  useEffect(() => { setLimit(100); setSongMenu(undefined); }, [query, page, group]);

  const current = library?.tracks.find(t => t.id === currentId);
  const settings = library?.settings;
  useEffect(() => {
    if (!settings) return;
    void window.music.appearance({
      theme: nowPlaying ? 'dark' : settings.theme,
      dimmed: settingsOpen || !!playlistDialog || !!deletePlaylist || !!noticeText,
      title: current ? `${current.title} · ${current.artist} — DR Music` : 'DR Music',
    }).catch(error => notify(String(error)));
  }, [settings?.theme, nowPlaying, settingsOpen, playlistDialog, deletePlaylist, noticeText, current?.id, notify]);
  const persist = useCallback((patch: Parameters<Window['music']['update']>[0]) => {
    setLibrary(previous => previous ? { ...previous, ...patch, settings: { ...previous.settings, ...patch.settings } } : previous);
    persistence.current = persistence.current.then(async () => { await window.music.update(patch); }).catch(error => {
      notify(`保存失败：${error}`);
      void window.music.getLibrary().then(setLibrary);
    });
  }, [notify]);
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
    if (!currentId) return;
    setLyricLoading(true);
    window.music.lyrics(currentId).then(file => { if (token === lyricRequest.current) setLyricFile(file); }).catch(e => { if (token === lyricRequest.current) notify(String(e)); }).finally(() => { if (token === lyricRequest.current) setLyricLoading(false); });
  }, [currentId, notify]);
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
      if (result) { setLibrary(result.library); notify(`已扫描 ${result.imported} 首音乐${result.warnings.length ? `，${result.warnings.length} 个文件存在读取问题：${result.warnings.slice(0, 2).join('；')}` : ''}`); }
    } catch (error) { notify(`导入失败：${error}`); }
    finally { setBusy(false); setProgress(undefined); }
  };
  const attachLyrics = async () => {
    if (!currentId) return;
    const id = currentId; const token = ++lyricRequest.current;
    try { const file = await window.music.attachLyrics(id); if (file && stateRef.current.currentId === id && token === lyricRequest.current) { setLyricFile(file); notify(`已载入 ${file.source}`); } }
    catch (error) { notify(String(error)); }
  };
  const navigate = (next: string) => { setPage(next); setGroup(''); setQuery(''); setNowPlaying(false); };
  const selectedPlaylist = library?.playlists.find(p => page === `playlist:${p.id}`);
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
    for (const track of filtered) { const key = page === 'artists' ? track.artist : track.album; result.set(key, [...(result.get(key) || []), track]); }
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
  const savePlaylist = (event: React.FormEvent) => {
    event.preventDefault(); if (!playlistDialog?.name.trim() || !library) return;
    const playlists = playlistDialog.id ? library.playlists.map(p => p.id === playlistDialog.id ? { ...p, name: playlistDialog.name.trim() } : p) : [...library.playlists, { id: crypto.randomUUID(), name: playlistDialog.name.trim(), trackIds: [] }];
    persist({ playlists }); setPlaylistDialog(undefined);
  };
  const renderTransport = () => <div className="transport">
    <IconButton icon={Shuffle} label="随机播放" active={settings?.shuffle} onClick={() => setSettings({ shuffle: !settings?.shuffle })}/>
    <IconButton icon={SkipBack} label="上一首" disabled={!current} onClick={() => advance(-1)}/>
    <button className="play-button" aria-label={playing ? '暂停' : '播放'} disabled={!library?.tracks.length} onClick={togglePlay}>{playing ? <Pause size={21} fill="currentColor"/> : <Play size={21} fill="currentColor"/>}</button>
    <IconButton icon={SkipForward} label="下一首" disabled={!current} onClick={() => advance(1)}/>
    <IconButton icon={settings?.repeat === 'one' ? Repeat1 : Repeat} label={`循环模式：${settings?.repeat === 'all' ? '列表循环' : settings?.repeat === 'one' ? '单曲循环' : '关闭'}`} active={settings?.repeat !== 'off'} onClick={() => setSettings({ repeat: settings?.repeat === 'off' ? 'all' : settings?.repeat === 'all' ? 'one' : 'off' })}/>
  </div>;
  const renderProgress = () => <div className="progress-control"><span>{formatTime(position)}</span><input aria-label="播放进度" type="range" min="0" max={duration || 1} step="0.1" value={Math.min(position, duration || 0)} disabled={!current || !duration} onChange={e => seek(Number(e.target.value))} style={{ '--progress': `${duration ? position / duration * 100 : 0}%` } as React.CSSProperties}/><span>{formatTime(duration)}</span></div>;
  const renderRows = (tracks: Track[]) => tracks.map((track, index) => <div className={`track-row ${currentId === track.id ? 'current' : ''}`} key={track.id} role="row" onDoubleClick={() => playTrack(track.id, filtered.map(t => t.id))}>
    <button className="row-number" aria-label={`播放 ${track.title}`} onClick={() => playTrack(track.id, filtered.map(t => t.id))}>{currentId === track.id && playing ? <AudioLines size={17}/> : <><span>{String(index + 1).padStart(2, '0')}</span><Play size={15}/></>}</button>
    <button className="track-identity" onClick={() => playTrack(track.id, filtered.map(t => t.id))}><Cover track={track}/><span><strong>{track.title}</strong><small>{track.artist}</small></span></button>
    <span className="album-cell">{track.album}</span><span className="format-label">{track.format}</span>
    <IconButton icon={Heart} label={`${library?.favorites.includes(track.id) ? '取消喜欢' : '喜欢'} ${track.title}`} active={library?.favorites.includes(track.id)} onClick={() => favorite(track.id)}/>
    <span className="track-duration">{formatTime(track.duration)}</span>
    <div className="song-menu-wrap"><IconButton icon={MoreHorizontal} label={`${track.title} 更多操作`} onClick={() => setSongMenu(songMenu === track.id ? undefined : track.id)}/>
      {songMenu === track.id && <><button className="menu-dismiss" aria-label="关闭歌曲菜单" onClick={() => setSongMenu(undefined)}/><div className="song-menu">
        <button onClick={() => { setQueue(q => { const copy = q.filter(id => id !== track.id); const at = copy.indexOf(currentId || ''); copy.splice(at < 0 ? copy.length : at + 1, 0, track.id); return copy; }); setSongMenu(undefined); notify('已加入下一首播放'); }}><ListPlus size={16}/>下一首播放</button>
        <button onClick={() => { void window.music.reveal(track.id).catch(e => notify(String(e))); setSongMenu(undefined); }}><FolderOpen size={16}/>在文件夹中显示</button>
        {selectedPlaylist && <button onClick={() => { persist({ playlists: library!.playlists.map(p => p.id === selectedPlaylist.id ? { ...p, trackIds: p.trackIds.filter(id => id !== track.id) } : p) }); setSongMenu(undefined); }}>从当前歌单移除</button>}
        <small>添加到歌单</small>{library?.playlists.length ? library.playlists.map(p => <button key={p.id} onClick={() => addToPlaylist(p, track.id)}><ListMusic size={16}/>{p.name}</button>) : <button onClick={() => { setSongMenu(undefined); setPlaylistDialog({ name: '' }); }}>先创建一个歌单</button>}
        <hr/><button className="danger" disabled={busy} onClick={() => void removeTrack(track.id)}><Trash2 size={16}/>从音乐库移除</button>
      </div></>}
    </div>
  </div>);

  return <div className="app-shell">
    <aside className="sidebar">
      <button className="brand" onClick={() => navigate('songs')}><span className="brand-icon"><AudioLines size={25}/></span><span>DR Music<small>音乐，随心而动。</small></span></button>
      <div className="sidebar-section">我的音乐</div>
      {([{ id: 'songs', icon: LibraryIcon, label: '音乐库', count: library?.tracks.length }, { id: 'favorites', icon: Heart, label: '我喜欢的', count: library?.favorites.length }, { id: 'albums', icon: Disc3, label: '专辑' }, { id: 'artists', icon: Mic2, label: '艺术家' }] as { id: string; icon: LucideIcon; label: string; count?: number }[]).map(({ id, icon: Icon, label, count }) => <button key={id} className={`nav-item ${page === id ? 'selected' : ''}`} onClick={() => navigate(id)}><Icon size={19}/><span>{label}</span>{count !== undefined && <small>{count}</small>}</button>)}
      <div className="sidebar-section playlist-label">我的歌单<IconButton icon={Plus} label="新建歌单" onClick={() => setPlaylistDialog({ name: '' })}/></div>
      <div className="playlist-nav">{library?.playlists.map(p => <button key={p.id} className={`nav-item ${page === `playlist:${p.id}` ? 'selected' : ''}`} onClick={() => navigate(`playlist:${p.id}`)}><ListMusic size={18}/><span>{p.name}</span><small>{p.trackIds.length}</small></button>)}{!library?.playlists.length && <p className="sidebar-hint">为不同的心情，<br/>留一张自己的歌单。</p>}</div>
      <div className="sidebar-bottom"><button className="nav-item" onClick={() => setSettingsOpen(true)}><Settings2 size={18}/><span>设置与关于</span><small>0.2.0</small></button></div>
    </aside>
    <main className="main-content">
      <header className="topbar"><div className="breadcrumb">我的音乐 <span>/</span> {selectedPlaylist ? '歌单' : page === 'favorites' ? '我喜欢的' : page === 'albums' ? '专辑' : page === 'artists' ? '艺术家' : '音乐库'}</div><label className="search"><Search size={17}/><input ref={searchRef} placeholder="搜索歌曲、艺术家、专辑" aria-label="搜索本地音乐" value={query} onChange={e => setQuery(e.target.value)}/>{query ? <IconButton icon={X} label="清除搜索" onClick={() => setQuery('')}/> : <kbd>Ctrl F</kbd>}</label><span className="local-badge"><Headphones size={15}/>LOCAL</span></header>
      <div className="page-scroll" onScroll={() => setSongMenu(undefined)}>
        <section className={`hero ${page === 'favorites' ? 'favorite-hero' : ''}`}>
          <div className="hero-copy">{group && <button className="back-link" onClick={() => setGroup('')}><ChevronLeft size={16}/>返回{page === 'artists' ? '艺术家' : '专辑'}</button>}<div className="eyebrow">{query ? 'FIND YOUR SOUND' : selectedPlaylist ? 'YOUR OWN MIX' : page === 'favorites' ? 'CLOSE TO YOUR HEART' : 'YOUR OFFLINE COLLECTION'}</div><h1>{heading}</h1><p>{library?.tracks.length ? `${filtered.length} 首歌曲${totalDuration ? ` · ${Math.round(totalDuration / 60)} 分钟` : ''} · 只属于你的音乐时光` : '从一个文件夹开始，把喜欢的声音留在身边。'}</p><div className="hero-actions"><button className="primary-button" disabled={!filtered.length} onClick={() => playTrack(filtered[0].id, filtered.map(t => t.id))}><Play size={16} fill="currentColor"/>播放全部</button><button className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={17}/>导入文件夹</button>{selectedPlaylist && <><IconButton icon={Pencil} label="重命名歌单" onClick={() => setPlaylistDialog({ id: selectedPlaylist.id, name: selectedPlaylist.name })}/><IconButton icon={Trash2} label="删除歌单" onClick={() => setDeletePlaylist(selectedPlaylist)}/></>}</div></div>
          <div className="hero-art" aria-hidden="true"><div className="hero-orbit"/><div className="hero-vinyl"><div className="vinyl-label"><AudioLines size={34}/><span>DR / 33 RPM</span></div></div><span className="hero-caption">GOOD MUSIC.<br/>NO DISTRACTIONS.</span></div>
        </section>
        {busy && <div className="scan-status" role="status"><RefreshCw className="spin" size={16}/>{progress ? `正在扫描 ${progress.done} / ${progress.total} · ${progress.name}` : '正在选择或查找本地音乐…'}</div>}
        <section className="collection"><div className="collection-toolbar"><div><h2>{query ? `找到 ${filtered.length} 首歌曲` : group || selectedPlaylist ? '歌曲' : page === 'albums' ? '所有专辑' : page === 'artists' ? '所有艺术家' : '全部歌曲'}</h2><span>{filtered.length.toString().padStart(2, '0')}</span></div><div><button className="text-button" disabled={busy} onClick={() => void runImport('files')}><Plus size={16}/>添加音乐</button><select aria-label="歌曲排序" value={sort} onChange={e => setSort(e.target.value)}><option value="added">最近添加</option><option value="title">歌曲名称</option><option value="artist">艺术家</option></select></div></div>
          {!library ? <div className="empty-state"><AudioLines size={36}/><h3>正在打开本地音乐库</h3></div> : !filtered.length ? <div className="empty-state"><div className="empty-icon">{query ? <Search size={33}/> : page === 'favorites' ? <Heart size={33}/> : <FolderPlus size={33}/>}</div><h3>{query ? '还没有找到这首音乐' : selectedPlaylist ? '让这个歌单有自己的声音' : page === 'favorites' ? '把喜欢的音乐放在这里' : '把你的音乐带进来'}</h3><p>{query ? '试试歌曲名、艺术家或专辑名。' : selectedPlaylist ? '在歌曲的「更多操作」里，将音乐添加到此歌单。' : page === 'favorites' ? '点击歌曲旁的爱心，收藏每一次心动。' : '导入本地文件夹，自动整理歌曲、专辑与封面。'}<br/>{!query && !selectedPlaylist && page !== 'favorites' && '支持 MP3、FLAC、WAV、M4A、OGG 等常见格式'}</p>{!query && page !== 'favorites' && !selectedPlaylist && <button className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={17}/>选择音乐文件夹<ArrowUpRight size={15}/></button>}</div> : (page === 'albums' || page === 'artists') && !group ? <div className="album-grid">{groups.slice(0, limit).map(([name, tracks]) => <button className="album-card" key={name} onClick={() => setGroup(name)}><Cover track={tracks[0]} className={page === 'artists' ? 'round' : ''}/><strong>{name}</strong><small>{tracks.length} 首歌曲{page === 'albums' ? ` · ${tracks[0].artist}` : ''}</small></button>)}</div> : <div className="track-table" role="table" aria-label="本地歌曲列表"><div className="track-row table-head" role="row"><span>#</span><span>歌曲 / 艺术家</span><span>专辑</span><span>格式</span><span/><span>时长</span><span/></div>{renderRows(filtered.slice(0, limit))}</div>}
          {((page === 'albums' || page === 'artists') && !group ? groups.length : filtered.length) > limit && <button className="load-more secondary-button" onClick={() => setLimit(n => n + 100)}>加载更多</button>}
        </section>
        <footer className="library-footer"><span><span className="tiny-dot"/>音乐在本地，热爱也是。</span><span>DR MUSIC / PERSONAL LIBRARY</span></footer>
      </div>
    </main>
    <footer className="player-bar"><div className="player-track"><button className="cover-button" aria-label="打开正在播放" disabled={!current} onClick={() => setNowPlaying(true)}><Cover track={current}/></button><button className="player-title" disabled={!current} onClick={() => setNowPlaying(true)}><strong>{current?.title || '还没有正在播放的音乐'}</strong><small>{current?.artist || '选一首喜欢的，开始聆听'}</small></button>{current && <IconButton icon={Heart} label="喜欢当前歌曲" active={library?.favorites.includes(current.id)} onClick={() => favorite(current.id)}/>}</div><div className="player-center">{renderTransport()}{renderProgress()}</div><div className="player-tools"><IconButton icon={settings?.volume === 0 ? VolumeX : Volume2} label="静音 / 恢复音量" onClick={() => setSettings({ volume: settings?.volume ? 0 : 0.7 })}/><input aria-label="音量" type="range" min="0" max="1" step="0.01" value={settings?.volume ?? 0.7} onChange={e => setSettings({ volume: Number(e.target.value) })}/><span className="tool-divider"/><IconButton icon={ListMusic} label="播放队列" active={queueOpen} onClick={() => setQueueOpen(v => !v)}/><IconButton icon={Maximize2} label="打开歌词播放页" disabled={!current} onClick={() => setNowPlaying(true)}/></div></footer>
    {nowPlaying && current && <section className="now-playing" aria-label="正在播放"><div className="now-bg">{current.cover && <img src={media(current, 'cover')} alt=""/>}</div><header><IconButton icon={ChevronDown} label="收起播放页" onClick={() => setNowPlaying(false)}/><span>正在播放 <i/> 你的本地音乐</span><IconButton icon={ListMusic} label="播放队列" onClick={() => setQueueOpen(v => !v)}/></header><div className="now-layout"><div className="now-left"><Cover track={current} className="now-cover"/><div className="now-title"><div><h2>{current.title}</h2><p>{current.artist} · {current.album}</p></div><IconButton icon={Heart} label="喜欢当前歌曲" active={library?.favorites.includes(current.id)} onClick={() => favorite(current.id)}/></div>{renderProgress()}{renderTransport()}<div className="audio-quality">{current.format}{current.sampleRate ? ` · ${(current.sampleRate / 1000).toFixed(1)} kHz` : ''} · 本地播放</div></div><div className="now-right">{lyricFile.text ? <Lyrics file={lyricFile} audio={audio} offset={settings?.lyricOffset || 0} onSeek={seek}/> : <div className="no-lyrics"><AudioLines size={38}/><h3>{lyricLoading ? '正在读取本地歌词' : '让文字也跟着旋律'}</h3><p>自动读取歌曲旁的同名歌词文件<br/>也可以手动关联 LRC、TTML、YRC 或 TXT</p><button className="secondary-button" onClick={() => void attachLyrics()}><Plus size={16}/>关联本地歌词</button></div>}<div className="lyric-toolbar"><button className="text-button" onClick={() => void attachLyrics()}><FileMusic size={15}/>{lyricFile.source || '选择歌词'}</button><div><button title="歌词延后 0.5 秒" onClick={() => setSettings({ lyricOffset: Math.max(-30000, (settings?.lyricOffset || 0) - 500) })}>−</button><span title="正值让歌词提前显示">偏移 {((settings?.lyricOffset || 0) / 1000).toFixed(1)}s</span><button title="歌词提前 0.5 秒" onClick={() => setSettings({ lyricOffset: Math.min(30000, (settings?.lyricOffset || 0) + 500) })}>+</button><button title="重置歌词偏移" onClick={() => setSettings({ lyricOffset: 0 })}>↺</button></div></div></div></div></section>}
    {queueOpen && <><button className="drawer-dismiss" aria-label="关闭队列背景" onClick={() => setQueueOpen(false)}/><aside className="queue-drawer"><div className="drawer-heading"><div><h2>播放队列</h2><p>{queue.length} 首本地音乐</p></div><IconButton icon={X} label="关闭播放队列" onClick={() => setQueueOpen(false)}/></div><div className="queue-list">{queue.map((id, i) => { const track = library?.tracks.find(t => t.id === id); return track && <div key={`${id}-${i}`} className={`queue-item ${id === currentId ? 'current' : ''}`}><button onClick={() => playTrack(id)}><Cover track={track}/><span><strong>{track.title}</strong><small>{track.artist}</small></span>{id === currentId && <AudioLines size={17}/>}</button><IconButton icon={X} label={`从队列移除 ${track.title}`} onClick={() => setQueue(q => q.filter(x => x !== id))}/></div>; })}{!queue.length && <div className="empty-state"><ListMusic size={32}/><p>播放歌曲后，队列会出现在这里。</p></div>}</div></aside></>}
    {settingsOpen && <div className="modal-backdrop" onClick={() => setSettingsOpen(false)}><section className="modal settings-modal" role="dialog" aria-modal="true" aria-label="设置与关于" onClick={e => e.stopPropagation()}><div className="modal-header"><h2>设置与关于</h2><IconButton icon={X} label="关闭设置" onClick={() => setSettingsOpen(false)}/></div><div className="settings-body"><div className="settings-brand"><span className="brand-icon"><AudioLines size={30}/></span><div><h3>DR Music <small>0.2.0</small></h3><p>你的音乐，你的节奏。完全本地的个人音乐播放器。</p></div></div><h4>外观</h4><div className="settings-row"><span>应用主题</span><select aria-label="应用主题" value={settings?.theme || 'dark'} onChange={e => setSettings({ theme: e.target.value as Settings['theme'] })}><option value="dark">深色 · 夜间唱片店</option><option value="light">浅色 · 午后音乐室</option></select></div><div className="settings-row"><span>原生标题栏</span><select aria-label="原生标题栏" value={settings?.titleBar || 'direct2d'} onChange={e => setSettings({ titleBar: e.target.value as Settings['titleBar'] })}><option value="direct2d">Direct2D 自绘</option><option value="system">Windows 系统标题栏</option></select></div><h4>本地音乐库</h4><p className="settings-hint">文件夹扫描包含子目录。重新扫描会更新标签和新增歌曲；失效文件可手动移出音乐库。移除操作不会删除磁盘音乐。</p><div className="folder-list">{library?.folders.map(folder => <div key={folder}><FolderOpen size={16}/><span title={folder}>{folder}</span></div>)}{!library?.folders.length && <p>还没有导入音乐文件夹</p>}</div><div className="settings-actions"><button className="secondary-button" disabled={busy} onClick={() => void runImport('folder')}><FolderPlus size={16}/>添加文件夹</button><button className="secondary-button" disabled={busy || !library?.folders.length} onClick={() => void runImport('rescan')}><RefreshCw size={16}/>重新扫描</button><button className="text-button" onClick={() => void window.music.openData().catch(e => notify(String(e)))}>打开数据目录</button></div><h4>键盘与播放</h4><div className="shortcut-list"><span>播放 / 暂停 <kbd>Space</kbd></span><span>上一首 / 下一首 <kbd>Ctrl ← / →</kbd></span><span>打开 / 收起播放页 <kbd>Ctrl L</kbd></span><span>喜欢当前歌曲 <kbd>Ctrl D</kbd></span><span>搜索音乐 <kbd>Ctrl F</kbd></span><span>关闭面板 <kbd>Esc</kbd></span></div><h4>来源与开源声明</h4><p className="settings-hint">本项目由 suxiaofan 定制，基于 MySpotify 的产品布局与歌词分词逻辑重构。原作者及开源声明保留。歌词使用 Apple Music-like Lyrics 渲染。应用遵循 AGPL-3.0-only。</p><button className="secondary-button" onClick={() => void window.music.notices().then(setNoticeText).catch(e => notify(String(e)))}>查看原项目及第三方声明</button><p className="privacy-note"><Check size={15}/>没有账号、在线搜索、遥测或自动更新。音频、歌词与音乐库都在你的电脑里。</p></div></section></div>}
    {noticeText && <div className="modal-backdrop"><section className="modal notices-modal" role="dialog" aria-label="开源声明"><div className="modal-header"><h2>开源与来源声明</h2><IconButton icon={X} label="关闭声明" onClick={() => setNoticeText('')}/></div><pre>{noticeText}</pre></section></div>}
    {playlistDialog && <div className="modal-backdrop"><form className="modal small-modal" role="dialog" aria-label={playlistDialog.id ? '重命名歌单' : '新建歌单'} onSubmit={savePlaylist}><div className="modal-header"><h2>{playlistDialog.id ? '重命名歌单' : '新建歌单'}</h2><IconButton icon={X} label="取消" onClick={() => setPlaylistDialog(undefined)}/></div><label>歌单名称<input autoFocus required maxLength={100} placeholder="给这一刻的心情起个名字" value={playlistDialog.name} onChange={e => setPlaylistDialog({ ...playlistDialog, name: e.target.value })}/></label><div className="dialog-actions"><button type="button" className="secondary-button" onClick={() => setPlaylistDialog(undefined)}>取消</button><button className="primary-button" type="submit" disabled={!playlistDialog.name.trim()}>保存歌单</button></div></form></div>}
    {deletePlaylist && <div className="modal-backdrop"><section className="modal small-modal" role="dialog" aria-label="确认删除歌单"><h2>删除「{deletePlaylist.name}」？</h2><p>歌单中的歌曲仍保留在音乐库和磁盘上。</p><div className="dialog-actions"><button className="secondary-button" onClick={() => setDeletePlaylist(undefined)}>取消</button><button className="primary-button" onClick={() => { persist({ playlists: library!.playlists.filter(p => p.id !== deletePlaylist.id) }); setDeletePlaylist(undefined); navigate('songs'); }}>删除歌单</button></div></section></div>}
    {toast && <div className="toast" role="status"><span>{toast}</span><IconButton icon={X} label="关闭提示" onClick={() => setToast('')}/></div>}
  </div>;
}
