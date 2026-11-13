import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { LyricLine } from '@applemusic-like-lyrics/core';
import { annotate, loadReader, splitHan } from '../src/lib/ruby';

const line = (words: [string, number, number][]): LyricLine => ({
  words: words.map(([word, startTime, endTime]) => ({ word, startTime, endTime })),
  translatedLyric: '', romanLyric: '', startTime: words[0][1], endTime: words.at(-1)![2], isBG: false, isDuet: false,
});

test('splitHan gives each Han character its own word and keeps other runs whole', () => {
  assert.deepEqual(splitHan({ word: '事的', startTime: 0, endTime: 1000 }).map(w => [w.word, w.startTime, w.endTime]), [['事', 0, 500], ['的', 500, 1000]]);
  assert.deepEqual(splitHan({ word: 'Love你', startTime: 0, endTime: 500 }).map(w => [w.word, w.startTime, w.endTime]), [['Love', 0, 400], ['你', 400, 500]]);
  assert.equal(splitHan({ word: 'hello ', startTime: 0, endTime: 1 }).length, 1);
});

test('Jyutping above each character follows the phrase, for simplified and traditional text', async () => {
  const read = await loadReader('jyutping');
  const [annotated] = annotate([line([['银行', 0, 1000], ['行路', 1000, 2000], [' OK', 2000, 2500]])], read, false);
  assert.deepEqual(annotated.words.map(w => [w.word, w.ruby?.[0].word ?? null]), [['银', 'ngan4'], ['行', 'hong4'], ['行', 'haang4'], ['路', 'lou6'], [' OK', null]]);
  for (const word of annotated.words) for (const r of word.ruby ?? []) assert.deepEqual([r.startTime, r.endTime], [word.startTime, word.endTime]);
  const [traditional] = annotate([line([['長大', 0, 1000]])], read, false);
  assert.deepEqual(traditional.words.map(w => w.ruby?.[0].word), ['zoeng2', 'daai6']);
});

test('Jyutping below the line, and lines without Han characters stay untouched', async () => {
  const read = await loadReader('jyutping');
  const english = line([['Hello ', 0, 500], ['world', 500, 1000]]);
  const [below, untouched] = annotate([line([['Love 你', 0, 1000], ['冇問題', 1000, 2000]]), english], read, true);
  assert.equal(below.romanLyric, 'Love nei5 mou5 man6 tai4');
  assert.equal(below.words.length, 2); // words keep their timing when the guide is on its own line
  assert.equal(untouched, english);
});
