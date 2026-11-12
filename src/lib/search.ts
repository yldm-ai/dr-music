import type { Track } from '../types';

/** The text a track is searched by (title, artist, album and file path). */
export const searchText = (track: Track) => `${track.title}\n${track.artist}\n${track.album}\n${track.path}`;
export const hasHan = (text: string) => /\p{Script=Han}/u.test(text);

/**
 * Whether a track matches any written form of the query. With simplified/traditional search on,
 * `folded` is the track text in simplified characters and `forms` holds the query's simplified
 * writings, so either script finds the other; otherwise `forms` is just the query.
 */
export function matchesQuery(text: string, folded: string | undefined, forms: string[]): boolean {
  const haystack = text.toLocaleLowerCase();
  const simplified = folded?.toLocaleLowerCase();
  return forms.some(form => {
    const needle = form.toLocaleLowerCase();
    return !!needle && (haystack.includes(needle) || !!simplified?.includes(needle));
  });
}