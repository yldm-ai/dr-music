import type { LyricFile, OnlineLyricResult } from '../types';
import { parseLyrics } from './lyrics';
import { hasHan } from './search';
import { hasKana } from './japanese';
import { hasSourceRoman, sourceRomanKind } from './ruby';

export type ReadingKind = Exclude<ReturnType<typeof sourceRomanKind>, 'unknown'>;
export interface ReadingSource { source: string; kind: ReadingKind }

/** Keep local-file readings separate from the online providers and generated readings. */
export function readingSources(local: LyricFile | undefined, online: OnlineLyricResult[] | null | undefined): ReadingSource[] {
  const files = [...(local?.text ? [{ source: 'local', file: local }] : []), ...(online || []).map(file => ({ source: file.provider, file }))];
  return files.flatMap(({ source, file }) => {
    try {
      const parsed = parseLyrics(file, 0);
      if (!hasSourceRoman(parsed.lines)) return [];
      const inferred = sourceRomanKind(parsed.romanLanguage, parsed.lines, hasHan(file.text) && !hasKana(file.text));
      const kind: ReadingKind = inferred === 'unknown' ? 'romaji' : inferred;
      return [{ source, kind }];
    } catch { return []; }
  });
}

/** Prefer the online provider currently shown, otherwise the best available result of this kind. */
export function findReadingSource(sources: ReadingSource[], kind: ReadingKind, origin: 'local' | 'online', current?: string): ReadingSource | undefined {
  const matches = sources.filter(item => item.kind === kind && (origin === 'local' ? item.source === 'local' : item.source !== 'local'));
  return matches.find(item => item.source === current) || matches[0];
}
