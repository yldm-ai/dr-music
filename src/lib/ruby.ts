import type { LyricLine, LyricWord } from '@applemusic-like-lyrics/core';
import type { JapaneseMode } from './japanese';

// Pronunciation guides (注音) for lyrics:
// - jyutping: Cantonese from ToJyutping (CanCLID, BSD-2-Clause), which reads whole lines so
//   multi-reading characters follow their phrase (銀行 hong4 / 行路 haang4). ~390 KB, loaded on first use.
// - romaji / kana: Japanese from kuromoji + wanakana in a worker (see japanese.ts).
// - romaji-online: the romanization that came with the lyric (TTML, NetEase, QQ Music, Kugou).
export type RubyKind = 'jyutping' | JapaneseMode;
export type RubyChoice = RubyKind | 'romaji-online';
/** A run of the line's text and the reading shown for it (null: none). */
export type Segment = [text: string, reading: string | null];
/** Readings for whole lines at once, one segment list per line, covering each line's text exactly. */
export type Reader = (lines: string[]) => Promise<Segment[][]>;

const readers = new Map<RubyKind, Promise<Reader>>();
export function loadReader(kind: RubyKind): Promise<Reader> {
  let reader = readers.get(kind);
  if (!reader) {
    reader = kind === 'jyutping'
      ? import('to-jyutping').then(m => async (lines: string[]) => lines.map(line => m.getJyutpingList(line) as Segment[]))
      : Promise.resolve(japaneseReader(kind));
    reader.catch(() => readers.delete(kind));
    readers.set(kind, reader);
  }
  return reader;
}

let worker: Worker | undefined;
let nextId = 0;
const pending = new Map<number, { resolve: (lines: Segment[][]) => void; reject: (error: Error) => void }>();
function japaneseReader(mode: JapaneseMode): Reader {
  return lines => new Promise((resolve, reject) => {
    if (!worker) {
      worker = new Worker(new URL('./japanese.worker.ts', import.meta.url), { type: 'module' });
      const target = worker;
      // A worker that fails to start (or crashes) fails every waiting request, and the next one starts afresh.
      worker.onerror = event => {
        event.preventDefault();
        for (const request of pending.values()) request.reject(new Error(event.message || '日语注音组件启动失败'));
        pending.clear(); target.terminate(); if (worker === target) worker = undefined;
      };
      worker.onmessage = (event: MessageEvent<{ id: number; segments?: Segment[][]; error?: string } | { file: number; url: string }>) => {
        if ('file' in event.data) {
          // The worker's dictionary fetch, made here (workers cannot reach the app's virtual host).
          const { file, url } = event.data, place = new URL(url, location.href);
          if (place.origin !== location.origin || !/^\/kuromoji\/[\w.]+\.dat\.gz$/.test(place.pathname)) { target.postMessage({ file, error: '无效的词典文件' }); return; }
          fetch(place).then(r => { if (!r.ok) throw new Error(`日语词典加载失败（HTTP ${r.status}）`); return r.arrayBuffer(); })
            .then(buffer => target.postMessage({ file, buffer }, [buffer]), error => target.postMessage({ file, error: error instanceof Error ? error.message : String(error) }));
          return;
        }
        const request = pending.get(event.data.id);
        pending.delete(event.data.id);
        if (event.data.segments) request?.resolve(event.data.segments); else request?.reject(new Error(event.data.error || '日语词典加载失败'));
      };
    }
    const id = ++nextId;
    pending.set(id, { resolve, reject });
    worker.postMessage({ id, lines, mode });
  });
}

export const lineText = (line: LyricLine) => line.words.map(w => w.word).join('');

/** Splits a word at the given character offsets (relative to the word), sharing its time by character count. */
function splitWord(word: LyricWord, cuts: number[]): LyricWord[] {
  const chars = [...word.word];
  if (!cuts.length) return [word];
  const span = word.endTime - word.startTime, total = chars.length;
  const bounds = [0, ...cuts, total];
  return bounds.slice(1).map((end, i) => {
    const start = bounds[i];
    return { ...word, word: chars.slice(start, end).join(''), startTime: word.startTime + span * start / total, endTime: word.startTime + span * end / total };
  });
}

/**
 * Adds readings to lines: above the text (AMLL ruby) or, with `below`, as a line of its own under
 * the lyric (AMLL romanLyric; `spaced` puts spaces between readings, as for romaji). Above the text,
 * words are cut where a reading starts or ends, and the words under one reading become one word.
 * Lines whose segments do not spell the line, or that have no reading, are left as they are.
 */
export function annotate(lines: LyricLine[], readings: Segment[][], below: boolean, spaced = true): LyricLine[] {
  return lines.map((line, index) => {
    const segments = readings[index];
    if (!segments?.some(([, reading]) => reading) || segments.map(([text]) => text).join('') !== lineText(line)) return line;
    if (below) {
      const romanLyric = segments.map(([text, reading]) => reading ? (spaced ? ` ${reading} ` : reading) : text).join('').replace(/\s+/g, ' ').trim();
      return { ...line, romanLyric };
    }
    // Character ranges of the segments that carry a reading.
    const ranged: { start: number; end: number; reading: string }[] = [];
    let at = 0;
    for (const [text, reading] of segments) {
      const length = [...text].length;
      if (reading) ranged.push({ start: at, end: at + length, reading });
      at += length;
    }
    const cuts = new Set(ranged.flatMap(r => [r.start, r.end]));
    const pieces: LyricWord[] = [];
    at = 0;
    for (const word of line.words) {
      const length = [...word.word].length;
      pieces.push(...splitWord(word, [...cuts].filter(c => c > at && c < at + length).sort((a, b) => a - b).map(c => c - at)));
      at += length;
    }
    const words: LyricWord[] = [];
    at = 0;
    let next = 0;
    for (let i = 0; i < pieces.length;) {
      const range = ranged[next];
      if (range && at === range.start) {
        // Every piece inside the reading's range joins one word that carries the reading.
        const group: LyricWord[] = [];
        while (i < pieces.length && at < range.end) { group.push(pieces[i]); at += [...pieces[i].word].length; ++i; }
        const startTime = group[0].startTime, endTime = group.at(-1)!.endTime;
        words.push({ ...group[0], word: group.map(w => w.word).join(''), startTime, endTime, ruby: [{ word: range.reading, startTime, endTime }] });
        ++next;
      } else {
        at += [...pieces[i].word].length;
        words.push(pieces[i++]);
      }
    }
    return { ...line, words };
  });
}

/** Whether the lyric came with its own romanization. */
export const hasSourceRoman = (lines: LyricLine[]) => lines.some(line => line.romanLyric?.trim() || line.words.some(w => w.romanWord?.trim()));

/**
 * The lyric's own romanization: hidden ('off'), above each word that has one (as ruby, like the
 * other guides; a line-level romanization stays under the line), or all of it under the line.
 */
export function sourceRoman(lines: LyricLine[], show: 'off' | 'above' | 'below'): LyricLine[] {
  return lines.map(line => {
    const wordRoman = line.words.some(w => w.romanWord?.trim());
    if (!wordRoman && !line.romanLyric) return line;
    const words = !wordRoman ? line.words : line.words.map(({ romanWord, ...word }) =>
      show === 'above' && romanWord?.trim() ? { ...word, ruby: [{ word: romanWord.trim(), startTime: word.startTime, endTime: word.endTime }] } : word);
    const romanLyric = show === 'off' ? '' : show === 'below' && wordRoman ? line.romanLyric?.trim() || line.words.map(w => w.romanWord?.trim()).filter(Boolean).join(' ') : show === 'above' && wordRoman ? '' : line.romanLyric;
    return { ...line, words, romanLyric };
  });
}
