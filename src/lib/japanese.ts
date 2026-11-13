import { toRomaji } from 'wanakana';
import type { Segment } from './ruby';

// Japanese readings for 音 → 罗马音 / 假名注音. kuromoji (Apache-2.0, IPADIC) splits a line into words
// with their katakana readings — kanji readings depend on the word (今日 きょう / 今 いま) — and
// wanakana (MIT) spells them in Hepburn romaji. This module only turns tokens into segments; the
// tokenizer itself runs in a worker (japanese.worker.ts) because its dictionary is ~17 MB.

/** The fields of a kuromoji token used here. */
export interface Token { surface_form: string; pos: string; reading?: string }
export type JapaneseMode = 'romaji' | 'kana';

const KANA = /[\p{Script=Hiragana}\p{Script=Katakana}]/u;
const KANJI = /[\p{Script=Han}々〆ヶ]/u;
const KANJI_RUN = /[\p{Script=Han}々〆ヶ]+|[^\p{Script=Han}々〆ヶ]+/gu;
export const hasKana = (text: string) => KANA.test(text);

/** Katakana to hiragana, leaving ー and everything else as written. */
export const hiragana = (text: string) => text.replace(/[ァ-ヶ]/g, c => String.fromCharCode(c.charCodeAt(0) - 0x60));
const escape = (text: string) => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

/** The word's reading in hiragana: the dictionary's, or the word itself when it is already kana. */
function reading(token: Token): string | null {
  if (token.reading && token.reading !== '*' && KANA.test(token.reading)) return hiragana(token.reading);
  return [...token.surface_form].every(c => KANA.test(c) || c === 'ー') ? hiragana(token.surface_form) : null;
}

/**
 * Furigana over the kanji only: the kana written in the word (okurigana) are matched out of its
 * reading, so 食べる gets た over 食 and 取り扱い gets と / あつか. Falls back to the whole word.
 */
function furigana(surface: string, read: string): Segment[] {
  const runs = surface.match(KANJI_RUN) || [surface];
  if (runs.length > 1) {
    const match = new RegExp(`^${runs.map(run => KANJI.test(run) ? '(.+?)' : escape(hiragana(run))).join('')}$`, 'u').exec(read);
    if (match) {
      let group = 1;
      return runs.map((run): Segment => KANJI.test(run) ? [run, match[group++]] : [run, null]);
    }
  }
  return [[surface, hiragana(surface) === read ? null : read]];
}

/** Romaji for one word; the particles は / へ / を are spoken wa / e / o. */
function romaji(token: Token, read: string): string {
  if (token.pos === '助詞') {
    if (token.surface_form === 'は') return 'wa';
    if (token.surface_form === 'へ') return 'e';
    if (token.surface_form === 'を') return 'o';
  }
  return toRomaji(read);
}

/**
 * Segments covering the line text exactly, with a reading where one is shown: romaji for every
 * Japanese word, or hiragana over the kanji. Text that is not Japanese keeps no reading.
 */
export function segments(tokens: Token[], mode: JapaneseMode): Segment[] {
  const out: Segment[] = [];
  for (const token of tokens) {
    const surface = token.surface_form;
    if (!surface) continue;
    const read = KANA.test(surface) || KANJI.test(surface) ? reading(token) : null;
    if (!read) out.push([surface, null]);
    else if (mode === 'kana') out.push(...(KANJI.test(surface) ? furigana(surface, read) : [[surface, null] as Segment]));
    else out.push([surface, romaji(token, read)]);
  }
  return out;
}
