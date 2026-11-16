import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { LyricLine } from '@applemusic-like-lyrics/core';
import { annotate, hasSourceRoman, lineText, loadReader, sourceRoman, sourceRomanKind, type Segment } from '../src/lib/ruby';

const line = (words: [string, number, number][]): LyricLine => ({
  words: words.map(([word, startTime, endTime]) => ({ word, startTime, endTime })),
  translatedLyric: '', romanLyric: '', startTime: words[0][1], endTime: words.at(-1)![2], isBG: false, isDuet: false,
});
const jyutping = async (lines: LyricLine[], below = false) => annotate(lines, await (await loadReader('jyutping'))(lines.map(lineText)), below);
const rubies = (l: LyricLine) => l.words.map(w => [w.word, w.ruby?.[0].word ?? null]);

test('Jyutping above each character follows the phrase, for simplified and traditional text', async () => {
  const [annotated] = await jyutping([line([['银行', 0, 1000], ['行路', 1000, 2000], [' OK', 2000, 2500]])]);
  assert.deepEqual(rubies(annotated), [['银', 'ngan4'], ['行', 'hong4'], ['行', 'haang4'], ['路', 'lou6'], [' OK', null]]);
  assert.deepEqual(annotated.words.slice(0, 2).map(w => [w.startTime, w.endTime]), [[0, 500], [500, 1000]]); // a word's time is shared by character
  for (const word of annotated.words) for (const r of word.ruby ?? []) assert.deepEqual([r.startTime, r.endTime], [word.startTime, word.endTime]);
  const [traditional] = await jyutping([line([['長大', 0, 1000]])]);
  assert.deepEqual(traditional.words.map(w => w.ruby?.[0].word), ['zoeng2', 'daai6']);
});

test('Jyutping below the line, and lines without Han characters stay untouched', async () => {
  const english = line([['Hello ', 0, 500], ['world', 500, 1000]]);
  const [below, untouched] = await jyutping([line([['Love 你', 0, 1000], ['冇問題', 1000, 2000]]), english], true);
  assert.equal(below.romanLyric, 'Love nei5 mou5 man6 tai4');
  assert.equal(below.words.length, 2); // words keep their timing when the guide is on its own line
  assert.equal(untouched, english);
});

test('a reading over several words joins them into one word; a word holding several readings is cut', () => {
  // One character per word (LRC spread / QRC), as most Japanese lyrics are timed.
  const perChar = line([['今', 0, 100], ['日', 100, 200], ['は', 200, 300]]);
  const segments: Segment[] = [['今日', 'kyou'], ['は', 'wa']];
  const [joined] = annotate([perChar], [segments], false);
  assert.deepEqual(rubies(joined), [['今日', 'kyou'], ['は', 'wa']]);
  assert.deepEqual(joined.words.map(w => [w.startTime, w.endTime]), [[0, 200], [200, 300]]);
  // One word for the whole phrase (TTML), with furigana over the kanji only.
  const [cut] = annotate([line([['食べる', 0, 300]])], [[['食', 'た'], ['べる', null]]], false, false);
  assert.deepEqual(rubies(cut), [['食', 'た'], ['べる', null]]);
  assert.deepEqual(cut.words.map(w => [w.startTime, w.endTime]), [[0, 100], [100, 300]]);
  // Kana under the line are not spaced; romaji are.
  assert.equal(annotate([perChar], [[['今日', 'きょう'], ['は', null]]], true, false)[0].romanLyric, 'きょうは');
  assert.equal(annotate([perChar], [segments], true)[0].romanLyric, 'kyou wa');
  // Readings that do not spell the line are ignored rather than misplaced.
  assert.equal(annotate([perChar], [[['昨日', 'kinou']]], false)[0], perChar);
});

test("the lyric's own romanization is hidden unless chosen, then shown above the words or under the line", () => {
  const worded: LyricLine = { ...line([['君', 0, 100], ['の', 100, 200]]), words: [{ word: '君', startTime: 0, endTime: 100, romanWord: 'kimi' }, { word: 'の', startTime: 100, endTime: 200, romanWord: 'no' }] };
  const lined: LyricLine = { ...line([['夜に駆ける', 0, 1000]]), romanLyric: 'yoru ni kakeru' };
  const plain = line([['Hello', 0, 100]]);
  assert.equal(hasSourceRoman([plain]), false);
  assert.equal(hasSourceRoman([plain, lined]), true);
  const [w0, l0, p0] = sourceRoman([worded, lined, plain], 'off');
  assert.equal(w0.words.some(w => w.romanWord || w.ruby), false);
  assert.equal(l0.romanLyric, '');
  assert.equal(p0, plain);
  const [w1, l1] = sourceRoman([worded, lined], 'above');
  assert.deepEqual(rubies(w1), [['君', 'kimi'], ['の', 'no']]);
  assert.equal(l1.romanLyric, 'yoru ni kakeru'); // a line-level romanization can only go under the line
  const [w2] = sourceRoman([worded], 'below');
  assert.equal(w2.romanLyric, 'kimi no');
  assert.equal(w2.words.some(w => w.romanWord || w.ruby), false);
});

test('source reading labels follow explicit language metadata, without treating every Chinese reading as Cantonese', () => {
  assert.equal(sourceRomanKind('zh-Latn-jyutping'), 'jyutping');
  assert.equal(sourceRomanKind('yue-Latn'), 'jyutping');
  assert.equal(sourceRomanKind('zh-Latn-pinyin'), 'pinyin');
  assert.equal(sourceRomanKind('ja-Latn'), 'romaji');
  assert.equal(sourceRomanKind('zh-Latn'), 'romaji');
  assert.equal(sourceRomanKind(), 'romaji');
});

test('unlabelled Chinese provider readings use several Cantonese clues, and otherwise keep a neutral label', () => {
  const withReading = (romanLyric: string): LyricLine[] => [{ ...line([['中文歌词', 0, 1000]]), romanLyric }];
  const netease = withReading('mei mu lui ci hu ba ci hu wan kei kao sam mo sv ba co pui zoe nei hing fu zoe yin hvn dou sun bin gong ba co mun zu nei di wan yao zam ho yi bou zu yv loi yv gan koe cong ba zi cu');
  assert.equal(sourceRomanKind(undefined, netease, true), 'jyutping');
  assert.equal(sourceRomanKind(undefined, withReading('mei muk leoi ci huk bat ci huk waan kei kau sam mo syut bat ceot pui zoek nei hing fu zoek yin hyun'), true), 'jyutping');
  assert.equal(sourceRomanKind(undefined, withReading('wo de xin li zhi you ni pei zhe ni zai ye li chang ge'), true), 'unknown');
  assert.equal(sourceRomanKind(undefined, withReading('ngo hello world'), true), 'unknown');
  assert.equal(sourceRomanKind(undefined, withReading('gei hei hou mou'), true), 'unknown');
  assert.equal(sourceRomanKind(undefined, withReading('kimi no namae o yobu'), false), 'romaji');
  assert.equal(sourceRomanKind('zh-Latn-pinyin', netease, true), 'pinyin');
  assert.equal(sourceRomanKind(undefined, [line([['ngo mou heoi', 0, 1000]])], true), 'unknown');
});

test('local-file and online reading sources remain distinct from local generation', async () => {
  const { readingSources, findReadingSource } = await import('../src/lib/rubySources');
  const japanese = { format: 'lrc', source: '', text: '[00:01.00]君の名前', roman: '[00:01.00]kimi no namae', romanFormat: 'lrc' };
  const online = (provider: string) => ({ ...japanese, provider, title: '', artists: '', album: '', score: 100, reference: '' });
  const sources = readingSources(japanese, [online('qq'), online('netease')]);
  assert.equal(findReadingSource(sources, 'romaji', 'local')?.source, 'local');
  assert.equal(findReadingSource(sources, 'romaji', 'online', 'netease')?.source, 'netease');
  assert.equal(findReadingSource(sources, 'romaji', 'online', 'absent')?.source, 'qq');
  const onlineOnly = readingSources(undefined, [online('qq')]);
  assert.equal(findReadingSource(onlineOnly, 'romaji', 'local'), undefined);
  assert.equal(findReadingSource(onlineOnly, 'romaji', 'online')?.source, 'qq');
  assert.deepEqual(readingSources({ ...japanese, roman: '' }, null), []);
});
