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
