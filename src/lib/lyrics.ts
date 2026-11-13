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
 * A romanization for a line: one per word when the counts match (word-level, as QQ Music and Kugou
 * write it), otherwise the whole line.
 */
function withRoman(line: LyricLine, roman: string[]): LyricLine {
  const parts = roman.map(part => part.trim());
  if (!parts.some(Boolean)) return line;
  if (parts.length === line.words.length && parts.length > 1) return { ...line, words: line.words.map((word, i) => ({ ...word, romanWord: parts[i] })) };
  return { ...line, romanLyric: roman.join('').replace(/\s+/g, ' ').trim() };
}
/** A translation for a line ("//" is NetEase's mark for a line left untranslated). */
function withTranslation(line: LyricLine, parts: string[]): LyricLine {
  const text = parts.join('').replace(/\s+/g, ' ').trim();
  return text && text !== '//' ? { ...line, translatedLyric: text } : line;
}
/**
 * Kugou's [language:…] tag: base64 JSON with a word list per lyric line, type 0 the romanization
 * and type 1 the translation.
 */
function krcLanguage(text: string, type: 0 | 1): string[][] | undefined {
  const tag = /^\[language:([A-Za-z0-9+/=]+)\]/m.exec(text);
  if (!tag) return undefined;
  try {
    const json = JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(tag[1]), c => c.charCodeAt(0))));
    const content = (json?.content as { type?: number; lyricContent?: unknown }[] | undefined)?.find(c => c?.type === type)?.lyricContent;
    return Array.isArray(content) ? content.map(line => Array.isArray(line) ? line.map(String) : []) : undefined;
  } catch { return undefined; }
}
/**
 * Kugou KRC (decrypted natively): "[lineStart,lineDuration]<offset,duration,0>word…", word offsets
 * relative to the line start. Metadata lines such as [ar:…] are skipped; [language:…] carries the
 * romanization (kept as romanWord / romanLyric) and the translation.
 */
export function parseKrc(text: string): LyricLine[] {
  const roman = krcLanguage(text, 0), translation = krcLanguage(text, 1);
  const lines: LyricLine[] = [];
  for (const raw of text.split(/\r?\n/)) {
    const head = /^\[(\d+),(\d+)\](.*)$/.exec(raw.trim());
    if (!head) continue;
    const start = Number(head[1]), duration = Number(head[2]);
    const words: LyricWord[] = [];
    for (const m of head[3].matchAll(/<(\d+),(\d+),-?\d+>([^<]*)/g))
      if (m[3]) words.push({ word: m[3], startTime: start + Number(m[1]), endTime: start + Number(m[1]) + Number(m[2]) });
    if (!words.length && head[3].trim()) words.push({ word: head[3], startTime: start, endTime: start + duration });
    const line: LyricLine = { words, translatedLyric: '', romanLyric: '', startTime: start, endTime: start + duration, isBG: false, isDuet: false };
    const parts = roman?.[lines.length], translated = translation?.[lines.length];
    const withParts = parts ? withRoman(line, parts) : line;
    lines.push(translated ? withTranslation(withParts, translated) : withParts);
  }
  return lines;
}
/**
 * A provider's separate romanization or translation file (NetEase romalrc / tlyric, QQ Music
 * contentroma / contentts; LRC or QRC), attached to the lyric line starting at the same time
 * (within a second; LRC and word timing differ a little).
 */
function attachByTime(lines: LyricLine[], text: string, format: string, attach: (line: LyricLine, parts: string[]) => LyricLine): LyricLine[] {
  let extra: LyricLine[];
  try { extra = format === 'qrc' ? parseQrc(text) : parseLrcLike(text).lines; } catch { return lines; }
  const out = [...lines], used = new Set<number>();
  for (const r of extra) {
    if (!r.words.some(w => w.word.trim())) continue;
    let best = -1;
    out.forEach((line, i) => {
      if (used.has(i) || line.isBG || Math.abs(line.startTime - r.startTime) > 1000) return;
      if (best < 0 || Math.abs(line.startTime - r.startTime) < Math.abs(out[best].startTime - r.startTime)) best = i;
    });
    if (best < 0) continue;
    used.add(best);
    out[best] = attach(out[best], r.words.map(w => w.word));
  }
  return out;
}
function parseLocalTTML(text: string): LyricLine[] {
  const document = new DOMParser().parseFromString(text, 'application/xml');
  if (document.getElementsByTagName('parsererror').length) throw new Error('TTML XML 格式无效');
  // AMLL's parser silently drops standard TTML paragraphs without Apple's
  // proprietary key attribute. Add stable keys to the in-memory copy only.
  const ns = 'http://music.apple.com/lyric-ttml-internal';
  const paragraphs = Array.from(document.getElementsByTagNameNS('*', 'p'));
  const used = new Set(paragraphs.map(p => p.getAttributeNS(ns, 'key')));
  // lrc.red keeps its line keys as lrc:key and its translations / transliterations in <sourceMetadata>;
  // under Apple's names AMLL attaches them to the lines too.
  for (const p of paragraphs) {
    const key = p.getAttributeNS(ns, 'key') ? null : Array.from(p.attributes).find(a => a.localName === 'key')?.value;
    if (key && !used.has(key)) { used.add(key); p.setAttributeNS(ns, 'itunes:key', key); }
  }
  for (const source of Array.from(document.getElementsByTagNameNS('*', 'sourceMetadata'))) {
    // Rebuilt in Apple's namespace: moved as they are, they would serialize as lrc:transliterations.
    const adopt = (node: Node): Node => {
      const element = node as Element;
      if (node.nodeType !== 1 || element.namespaceURI !== source.namespaceURI) return node.cloneNode(true);
      const copy = document.createElementNS(ns, element.localName);
      for (const a of Array.from(element.attributes)) copy.setAttributeNS(a.namespaceURI, a.name, a.value);
      for (const child of Array.from(element.childNodes)) copy.appendChild(adopt(child));
      return copy;
    };
    const meta = document.createElementNS(ns, 'iTunesMetadata');
    for (const child of Array.from(source.childNodes)) meta.appendChild(adopt(child));
    source.replaceWith(meta);
  }
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
  if (file.roman && file.romanFormat) lines = attachByTime(lines, file.roman, file.romanFormat, withRoman);
  if (file.translation && file.translationFormat) lines = attachByTime(lines, file.translation, file.translationFormat, withTranslation);
  return { lines, plain: '', estimated };
}
