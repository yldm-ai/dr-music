import type { LyricLine, LyricWord } from '@applemusic-like-lyrics/core';

// Pronunciation guides (注音) for lyrics. Cantonese Jyutping comes from ToJyutping (CanCLID,
// BSD-2-Clause), which reads whole lines so multi-reading characters follow their phrase
// (銀行 hong4 / 行路 haang4). It accepts simplified and traditional text alike. The ~390 KB
// dictionary is loaded the first time an annotation is shown.
export type RubyKind = 'jyutping';
export type Reader = (text: string) => [string, string | null][];

let jyutping: Promise<Reader> | undefined;
export const loadReader = (_kind: RubyKind): Promise<Reader> => jyutping ??= import('to-jyutping').then(m => m.getJyutpingList);

const HAN = /\p{Script=Han}/u;

/** Gives every Han character its own word, sharing the word's time by character count. */
export function splitHan(word: LyricWord): LyricWord[] {
  const parts = word.word.match(/\p{Script=Han}|[^\p{Script=Han}]+/gu) || [];
  if (parts.length <= 1) return [word];
  const total = [...word.word].length, span = word.endTime - word.startTime;
  let at = 0;
  return parts.map(part => {
    const startTime = word.startTime + span * at / total;
    at += [...part].length;
    return { ...word, word: part, startTime, endTime: word.startTime + span * at / total };
  });
}

/**
 * Adds readings to lines with Han characters: above each character (AMLL ruby) or, with `below`,
 * as a line of its own under the lyric (AMLL romanLyric). Other text is left as written.
 */
export function annotate(lines: LyricLine[], read: Reader, below: boolean): LyricLine[] {
  return lines.map(line => {
    const text = line.words.map(w => w.word).join('');
    if (!HAN.test(text)) return line;
    const readings = read(text);
    if (below) {
      const romanLyric = readings.map(([char, reading]) => reading ? ` ${reading} ` : char).join('').replace(/\s+/g, ' ').trim();
      return { ...line, romanLyric };
    }
    // Readings are per code point; fall back to word-by-word lookup if they do not line up.
    const aligned = readings.length === [...text].length;
    let index = 0;
    const words = line.words.flatMap(splitHan).map(word => {
      const length = [...word.word].length;
      const reading = aligned ? (length === 1 ? readings[index][1] : null) : HAN.test(word.word) && length === 1 ? read(word.word)[0]?.[1] : null;
      index += length;
      return reading ? { ...word, ruby: [{ word: reading, startTime: word.startTime, endTime: word.endTime }] } : word;
    });
    return { ...line, words };
  });
}
