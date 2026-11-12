import { parseLrcLike, parseQrc, parseTTML, parseYrc } from '@applemusic-like-lyrics/lyric';
import type { LyricLine, LyricWord } from '@applemusic-like-lyrics/core';
import type { LyricFile } from '../types';

// Port of MySpotify's SpreadWords (margbug01/MySpotify, AGPL-3.0).
// Line-only LRC has estimated word timing; real syllable timing stays intact.
export function spreadWords(text: string, start: number, end: number): LyricWord[] {
  const words = text.match(/[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Hangul}]|[^\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Hangul}\s]+\s*/gu) || [];
  const total = words.reduce((n, w) => n + [...w.trim()].length, 0);
  let at = start;
  return words.map(word => {
    const duration = (end - start) * 0.85 * [...word.trim()].length / Math.max(1, total);
    const result = { word, startTime: at, endTime: at + duration }; at += duration; return result;
  });
}
/**
 * Kugou KRC (decrypted natively): "[lineStart,lineDuration]<offset,duration,0>word…", word offsets
 * relative to the line start. Metadata lines such as [ar:…] or [language:…] are skipped.
 */
export function parseKrc(text: string): LyricLine[] {
  const lines: LyricLine[] = [];
  for (const raw of text.split(/\r?\n/)) {
    const head = /^\[(\d+),(\d+)\](.*)$/.exec(raw.trim());
    if (!head) continue;
    const start = Number(head[1]), duration = Number(head[2]);
    const words: LyricWord[] = [];
    for (const m of head[3].matchAll(/<(\d+),(\d+),-?\d+>([^<]*)/g))
      if (m[3]) words.push({ word: m[3], startTime: start + Number(m[1]), endTime: start + Number(m[1]) + Number(m[2]) });
    if (!words.length && head[3].trim()) words.push({ word: head[3], startTime: start, endTime: start + duration });
    lines.push({ words, translatedLyric: '', romanLyric: '', startTime: start, endTime: start + duration, isBG: false, isDuet: false });
  }
  return lines;
}
function parseLocalTTML(text: string): LyricLine[] {
  const document = new DOMParser().parseFromString(text, 'application/xml');
  if (document.getElementsByTagName('parsererror').length) throw new Error('TTML XML 格式无效');
  // AMLL's parser silently drops standard TTML paragraphs without Apple's
  // proprietary key attribute. Add stable keys to the in-memory copy only.
  const ns = 'http://music.apple.com/lyric-ttml-internal';
  const paragraphs = Array.from(document.getElementsByTagNameNS('*', 'p'));
  const used = new Set(paragraphs.map(p => p.getAttributeNS(ns, 'key')));
  paragraphs.forEach((p, index) => {
    if (!p.getAttributeNS(ns, 'key')) {
      let key = `dr-music-${index}`;
      while (used.has(key)) key += '-local';
      used.add(key); p.setAttributeNS(ns, 'itunes:key', key);
    }
  });
  return parseTTML(new XMLSerializer().serializeToString(document)).lines;
}
export function parseLyrics(file: LyricFile, durationMs: number): { lines: LyricLine[]; plain: string; estimated: boolean } {
  if (!file.text.trim()) return { lines: [], plain: '', estimated: false };
  if (file.format === 'txt' || (file.format === 'lrc' && !/\[\d+:\d+/.test(file.text))) return { lines: [], plain: file.text, estimated: false };
  let lines: LyricLine[] = file.format === 'ttml' ? parseLocalTTML(file.text)
    : file.format === 'yrc' ? parseYrc(file.text)
    : file.format === 'qrc' ? parseQrc(file.text)
    : file.format === 'krc' ? parseKrc(file.text)
    : parseLrcLike(file.text).lines;
  let estimated = false;
  // Do not reorder simultaneous TTML background/duet lines. LRC auxiliary
  // lines with equal timestamps are paired as a translation.
  if (file.format === 'lrc') {
    lines.sort((a, b) => a.startTime - b.startTime);
    const merged: LyricLine[] = [];
    for (const line of lines) {
      const prev = merged.at(-1);
      if (prev && prev.startTime === line.startTime && !line.isBG && !line.isDuet && !prev.translatedLyric) prev.translatedLyric = line.words.map(w => w.word).join('');
      else merged.push(line);
    }
    lines = merged;
  }
  lines = lines.filter(l => l.words.length > 0 && l.words.some(w => w.word.trim())).map((line, i) => {
    const startTime = Math.max(0, line.startTime);
    const next = lines.slice(i + 1).find(l => l.startTime > startTime)?.startTime;
    const endTime = Math.max(startTime + 1, Math.min(startTime + 6000, next ?? (durationMs > startTime ? durationMs : startTime + 6000)));
    if (file.format === 'lrc' && line.words.length === 1) {
      estimated = true;
      return { ...line, startTime, endTime, words: spreadWords(line.words[0].word, startTime, endTime) };
    }
    return { ...line, startTime, endTime: Math.max(startTime, line.endTime), words: line.words.map(w => ({ ...w, startTime: Math.max(0, w.startTime), endTime: Math.max(0, w.startTime, w.endTime) })) };
  });
  if (!lines.length) throw new Error('没有识别到有效的时间轴歌词，请检查文件格式。');
  return { lines, plain: '', estimated };
}
