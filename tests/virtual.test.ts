import assert from 'node:assert/strict';
import test from 'node:test';
import { virtualRange } from '../src/lib/virtual';

test('virtual range renders the viewport with overscan', () => {
  assert.deepEqual(virtualRange(1000, 60, 6000, 480, 4), {
    start: 96,
    end: 112,
    totalHeight: 60000,
  });
});

test('virtual range clamps scroll and keeps an initial row visible', () => {
  assert.deepEqual(virtualRange(10, 60, -20, 0, 4), {
    start: 0,
    end: 5,
    totalHeight: 600,
  });
  assert.deepEqual(virtualRange(10, 60, 9999, 300, 2), {
    start: 3,
    end: 10,
    totalHeight: 600,
  });
});

test('virtual range handles an empty queue', () => {
  assert.deepEqual(virtualRange(0, 60, 0, 800, 4), {
    start: 0,
    end: 0,
    totalHeight: 0,
  });
});
