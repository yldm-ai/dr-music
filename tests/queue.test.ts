import { test } from 'node:test';
import assert from 'node:assert/strict';
import { nextTrack, formatTime } from '../src/lib/queue';

// Natural ending stops at the last track unless a repeat mode explicitly wraps it.
test('queue ending, looping and manual skips', () => {
  const queue = ['a', 'b', 'c'];
  assert.equal(nextTrack(queue, 'c', 1, 'off', false, true), undefined);
  assert.equal(nextTrack(queue, 'c', 1, 'all', false, true), 'a');
  assert.equal(nextTrack(queue, 'b', 1, 'one', false, true), 'b');
  assert.equal(nextTrack(queue, 'b', 1, 'one', false), 'c');
  assert.equal(nextTrack(queue, 'a', -1, 'off', false), 'c');
  assert.equal(nextTrack([], undefined, 1, 'off', false), undefined);
});
// Shuffle must avoid immediately replaying the current track when alternatives exist.
test('shuffle excludes current track', () => {
  assert.equal(nextTrack(['a', 'b', 'c'], 'b', 1, 'off', true, true, () => 0), 'a');
  assert.equal(nextTrack(['a', 'b', 'c'], 'b', 1, 'off', true, true, () => 0.99), 'c');
  assert.equal(nextTrack(['a'], 'a', 1, 'off', true, true), undefined);
});
// Invalid metadata must never show NaN/Infinity in a player time label.
test('time formatting handles invalid metadata', () => {
  assert.equal(formatTime(NaN), '0:00'); assert.equal(formatTime(Infinity), '0:00');
  assert.equal(formatTime(-1), '0:00'); assert.equal(formatTime(125.5), '2:05');
});
