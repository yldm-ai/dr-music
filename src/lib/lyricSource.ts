import type { LyricFile, OnlineLyricResult } from '../types';

/** Online lyric providers, in the order settings list them (ids match native/online_lyrics.cpp). */
export const LYRIC_PROVIDERS = [
  { id: 'amll', name: 'AMLL TTML DB', hint: '逐词 TTML 歌词库（CC0）' },
  { id: 'lrcred', name: 'lrc.red', hint: '社区逐词 TTML 歌词库（原 BiniLyrics，Better Lyrics 同款）' },
  { id: 'qq', name: 'QQ 音乐', hint: '逐字 QRC 歌词' },
  { id: 'netease', name: '网易云音乐', hint: '逐字 YRC 或逐行歌词' },
  { id: 'kugou', name: '酷狗音乐', hint: '逐字 KRC 歌词' },
  { id: 'lrclib', name: 'LRCLIB', hint: '开放的逐行歌词库' },
  { id: 'apple', name: 'Apple Music', hint: '需要你的 Apple Music media-user-token' },
] as const;
export const providerName = (id: string) => LYRIC_PROVIDERS.find(p => p.id === id)?.name ?? id;

export type LyricSource = 'local' | string;

/**
 * Which lyric the player shows. A song's own lyric wins unless the user picked an online source;
 * a song without lyrics falls back to the best online match. Any two sources make it switchable.
 * The legacy preference "online" means "the best online match".
 */
export function chooseLyric(local: LyricFile | undefined, online: OnlineLyricResult[] | null | undefined, preference?: string) {
  const hasLocal = !!local?.text;
  const results = (online || []).filter(r => r.text);
  const picked = preference === 'online' ? results[0] : results.find(r => r.provider === preference);
  const file: LyricFile | undefined = preference !== 'local' && picked ? picked : hasLocal ? local : results[0];
  const source: LyricSource | undefined = !file ? undefined : file === local ? 'local' : (file as OnlineLyricResult).provider;
  return { source, file, switchable: (hasLocal ? 1 : 0) + results.length >= 2, results };
}