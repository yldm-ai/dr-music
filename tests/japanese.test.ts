import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import path from 'node:path';
import kuromoji from '@sglkc/kuromoji';
import { hasKana, hiragana, segments, type Token } from '../src/lib/japanese';

// The real IPADIC dictionary, read from node_modules as the worker reads it from the app.
const dicPath = path.join(path.dirname(createRequire(import.meta.url).resolve('@sglkc/kuromoji/package.json')), 'dict');
const tokenizer = new Promise<{ tokenize(text: string): Token[] }>((resolve, reject) => kuromoji.builder({ dicPath }).build((error, built) => error ? reject(error) : resolve(built)));
const read = async (text: string, mode: 'romaji' | 'kana') => segments((await tokenizer).tokenize(text), mode);
const shown = (list: [string, string | null][]) => list.filter(([, r]) => r).map(([t, r]) => `${t}:${r}`);

test('Japanese lyrics are recognised by their kana', () => {
  assert.equal(hasKana('夜に駆ける'), true);
  assert.equal(hasKana('アイドル'), true);
  assert.equal(hasKana('晴天'), false);
  assert.equal(hiragana('キョウ ハ'), 'きょう は');
});

test('romaji follow the word (今日 kyou, 今 ima) and particles are spoken wa / e / o', async () => {
  const today = await read('今日は君を待つ', 'romaji');
  assert.equal(today.map(([t]) => t).join(''), '今日は君を待つ'); // segments spell the line
  assert.deepEqual(shown(today), ['今日:kyou', 'は:wa', '君:kimi', 'を:o', '待つ:matsu']);
  assert.deepEqual(shown(await read('今行く', 'romaji')).slice(0, 1), ['今:ima']);
  // Latin text and punctuation get no reading.
  const mixed = await read('Hello、世界へ', 'romaji');
  assert.equal(mixed.map(([t]) => t).join(''), 'Hello、世界へ');
  assert.deepEqual(shown(mixed), ['世界:sekai', 'へ:e']);
});

test('furigana go over the kanji only, matching the kana written in the word out of its reading', async () => {
  assert.deepEqual(shown(await read('食べる', 'kana')), ['食:た']);
  assert.deepEqual(shown(await read('今日は晴れ', 'kana')), ['今日:きょう', '晴:は']);
  assert.deepEqual(shown(await read('ひらがなとカタカナ', 'kana')), []);
});
