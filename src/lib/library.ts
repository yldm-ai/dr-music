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