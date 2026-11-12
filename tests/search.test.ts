import { test } from 'node:test';
import assert from 'node:assert/strict';
import { matchesQuery, hasHan, searchText } from '../src/lib/search';
import type { Track } from '../src/types';

const track: Track = { id: 'a', path: 'C:\\Music\\後來.flac', title: '後來', artist: '劉若英', album: '我等你', duration: 1, format: 'FLAC', addedAt: 0 };

test('plain search matches case-insensitively across title, artist, album and path', () => {
  assert.equal(matchesQuery(searchText(track), undefined, ['劉若英']), true);
  assert.equal(matchesQuery(searchText(track), undefined, ['FLAC']), true);
  assert.equal(matchesQuery(searchText(track), undefined, ['后来']), false);
});

test('simplified/traditional search matches through the folded text and any query form', () => {
  // Native folding gives the simplified track text; the query forms come from OpenCC chains.
  const folded = '后来\n刘若英\n我等你\nC:\\Music\\后来.flac';
  assert.equal(matchesQuery(searchText(track), folded, ['后来']), true);
  assert.equal(matchesQuery(searchText(track), folded, ['劉若英', '刘若英']), true);
  assert.equal(matchesQuery(searchText(track), folded, ['以后']), false);
  assert.equal(matchesQuery(searchText(track), folded, ['']), false);
});

test('han detection decides whether native conversion is needed', () => {
  assert.equal(hasHan('Hello 世界'), true);
  assert.equal(hasHan('Hello world'), false);
  assert.equal(hasHan('こんにちは'), false);
});