import { test } from 'node:test';
import assert from 'node:assert/strict';
import { chooseLyric, providerName } from '../src/lib/lyricSource';
import type { OnlineLyricResult } from '../src/types';

const local = { text: '[00:01.00]本地', format: 'lrc', source: '歌曲.lrc' };
const online = (provider: string, score: number): OnlineLyricResult => ({ text: `<${provider}/>`, format: 'ttml', source: provider, provider, title: 't', artists: 'a', album: '', score, reference: '' });
const empty = { text: '', format: 'lrc', source: '' };
const amll = online('amll', 100), qq = online('qq', 95);

test('own lyrics win by default; the best online match fills in when a song has none', () => {
  assert.equal(chooseLyric(local, [amll, qq]).source, 'local');
  assert.equal(chooseLyric(empty, [amll, qq]).source, 'amll');
  assert.equal(chooseLyric(local, null).source, 'local');
  assert.equal(chooseLyric(empty, undefined).file, undefined);
  assert.equal(chooseLyric(empty, [amll]).switchable, false);
  assert.equal(chooseLyric(empty, [amll, qq]).switchable, true);
  assert.equal(chooseLyric(local, [amll]).switchable, true);
});

test('a saved preference picks that provider while it still has lyrics', () => {
  assert.equal(chooseLyric(local, [amll, qq], 'qq').source, 'qq');
  assert.equal(chooseLyric(local, [amll], 'qq').source, 'local');      // provider no longer answers
  assert.equal(chooseLyric(empty, [amll, qq], 'local').source, 'amll'); // nothing local to show
  assert.equal(chooseLyric(local, [amll, qq], 'online').source, 'amll'); // legacy preference
  assert.equal(providerName('netease'), '网易云音乐');
});