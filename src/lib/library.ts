import type { Library, Track } from '../types';

// Imported paths are absolute and canonical. This only projects cached state;
// the native host validates the request and owns persistence and file access.
const pathKey = (path: string) => path.replace(/^\\\\\?\\/, '').replaceAll('/', '\\').replace(/\\+$/, '').toLowerCase();

export function withoutFolder(library: Library, folder: string): Library {
  const key = pathKey(folder);
  const inside = (path: string) => {
    const normalized = pathKey(path);
    return normalized === key || normalized.startsWith(`${key}\\`);
  };
  const removed = new Set(library.tracks.filter(track => inside(track.path)).map(track => track.id));
  return {
    ...library,
    folders: library.folders.filter(path => !inside(path)),
    tracks: library.tracks.filter(track => !removed.has(track.id)),
    favorites: library.favorites.filter(id => !removed.has(id)),
    playlists: library.playlists.map(playlist => ({ ...playlist, trackIds: playlist.trackIds.filter(id => !removed.has(id)) })),
  };
}

export function withMetadata(library: Library, updates: Track[]): Library {
  const byId = new Map(updates.map(track => [track.id, track]));
  return {
    ...library,
    tracks: library.tracks.map(track => {
      const updated = byId.get(track.id);
      return updated && updated.metadataRevision === track.metadataRevision ? updated : track;
    }),
  };
}

export interface MusicFolder { path: string; name: string; root: string; trackIds: string[] }
const directoryOf = (path: string) => { const normalized = path.replaceAll('/', '\\'); const at = normalized.lastIndexOf('\\'); return at > 0 ? normalized.slice(0, at) : normalized; };
const baseName = (path: string) => path.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || path;
const byName = (a: string, b: string) => a.localeCompare(b, 'zh-CN', { numeric: true, sensitivity: 'base' });

/**
 * Directories (inside the folders added in settings) that directly contain music, in folder order.
 * Each one behaves like a read-only playlist; tracks are ordered by file name.
 */
export function musicFolders(library: Library): MusicFolder[] {
  const roots = library.folders.map(folder => ({ folder, key: pathKey(folder) })).sort((a, b) => b.key.length - a.key.length);
  const found = new Map<string, MusicFolder & { files: { id: string; file: string }[]; rootIndex: number }>();
  for (const track of library.tracks) {
    const dir = directoryOf(track.path); const key = pathKey(dir);
    const root = roots.find(r => key === r.key || key.startsWith(`${r.key}\\`));
    if (!root) continue;
    let entry = found.get(key);
    if (!entry) found.set(key, entry = { path: dir, name: baseName(dir), root: root.folder, trackIds: [], files: [], rootIndex: library.folders.indexOf(root.folder) });
    entry.files.push({ id: track.id, file: baseName(track.path) });
  }
  const entries = [...found.values()];
  const names = new Map<string, number>();
  for (const entry of entries) names.set(entry.name.toLowerCase(), (names.get(entry.name.toLowerCase()) || 0) + 1);
  return entries
    .map(({ files, rootIndex, ...entry }) => {
      const parent = baseName(directoryOf(entry.path));
      const name = (names.get(entry.name.toLowerCase()) || 0) > 1 && pathKey(entry.path) !== pathKey(entry.root) ? `${parent} / ${entry.name}` : entry.name;
      const relative = pathKey(entry.path).slice(pathKey(entry.root).length);
      return { entry: { ...entry, name, trackIds: files.sort((a, b) => byName(a.file, b.file)).map(f => f.id) }, rootIndex, relative };
    })
    .sort((a, b) => a.rootIndex - b.rootIndex || byName(a.relative, b.relative))
    .map(({ entry }) => entry);
}
/** 歌曲排序 choices; the list and the play queue follow the same order. */
export type TrackSort = 'added' | 'modified' | 'title' | 'artist' | 'album' | 'file' | 'duration';
export const TRACK_SORTS: { value: TrackSort; label: string }[] = [
  { value: 'added', label: '最近添加' },
  { value: 'modified', label: '修改日期（最新在前）' },
  { value: 'title', label: '歌曲名称' },
  { value: 'artist', label: '艺术家' },
  { value: 'album', label: '专辑' },
  { value: 'file', label: '文件名' },
  { value: 'duration', label: '时长' },
];
export const asSort = (value?: string): TrackSort => TRACK_SORTS.some(s => s.value === value) ? value as TrackSort : 'added';

/** The file name without its extension (歌曲名称 → 文件名). */
export const fileTitle = (path: string) => baseName(path).replace(/\.[^.]+$/, '') || baseName(path);

/**
 * Sorts songs. Ties (songs imported together share one 添加 time) fall back to the path, so the order
 * is stable and "next song" never jumps around. `keepOrder` keeps a playlist / folder's own order for 最近添加.
 */
export function sortTracks(tracks: Track[], sort: TrackSort, keepOrder = false): Track[] {
  // Tracks of one CUE sheet share their file: they follow the sheet's order.
  const byPath = (a: Track, b: Track) => byName(a.path, b.path) || (a.cue ? (a.trackNumber || 0) - (b.trackNumber || 0) : 0);
  const compare: Record<TrackSort, (a: Track, b: Track) => number> = {
    added: (a, b) => keepOrder ? 0 : b.addedAt - a.addedAt || byPath(a, b),
    modified: (a, b) => (b.modifiedAt || 0) - (a.modifiedAt || 0) || byPath(a, b),
    title: (a, b) => byName(a.title, b.title) || byPath(a, b),
    artist: (a, b) => byName(a.artist, b.artist) || byName(a.title, b.title) || byPath(a, b),
    album: (a, b) => byName(a.album, b.album) || (a.discNumber || 0) - (b.discNumber || 0) || (a.trackNumber || 0) - (b.trackNumber || 0) || byPath(a, b),
    file: (a, b) => byName(baseName(a.path), baseName(b.path)) || byPath(a, b),
    duration: (a, b) => a.duration - b.duration || byPath(a, b),
  };
  return [...tracks].sort(compare[sort]);
}

/** Applies 歌曲名称 (tag title or file name) to every song; the tag title is kept to switch back. */
export function withDisplayTitles(library: Library): Library {
  const fromFile = library.settings.titleSource === 'file';
  if (!fromFile && !library.tracks.some(t => t.tagTitle !== undefined)) return library;
  return {
    ...library,
    tracks: library.tracks.map(track => {
      const tag = track.tagTitle ?? track.title;
      // A CUE track's file is the whole disc: it keeps the sheet's title.
      if (fromFile && !track.cue) { const title = fileTitle(track.path); return track.title === title && track.tagTitle === tag ? track : { ...track, title, tagTitle: tag }; }
      if (track.tagTitle === undefined || track.cue) return track;
      const { tagTitle: _, ...rest } = track;
      return { ...rest, title: tag };
    }),
  };
}