import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseLyrics, spreadWords } from '../src/lib/lyrics';

// Repeated timestamps represent translations; multi-tag LRC must retain its later repeats.
test('LRC timestamps, translations, duration and CJK segmentation', () => {
  const parsed = parseLyrics({ format: 'lrc', source: '', text: '[00:01.00][00:05.00]Hello 世界\n[00:01.00]你好世界\n[00:10.00]Ending' }, 12000);
  assert.deepEqual(parsed.lines.map(l => l.startTime), [1000, 5000, 10000]);
  assert.equal(parsed.lines[0].translatedLyric, '你好世界');
  assert.deepEqual(parsed.lines[0].words.map(w => w.word), ['Hello ', '世', '界']);
  assert.equal(parsed.lines[0].endTime, 5000);
  assert.equal(parsed.lines[2].endTime, 12000);
  assert.equal(parsed.estimated, true);
});
// A genuine word-timed file must retain the singer's word boundaries.
test('YRC preserves real word timings', () => {
  const parsed = parseLyrics({ format: 'yrc', source: '', text: '[1000,2000](1000,500,0)你(1500,1000,0)好' }, 5000);
  assert.equal(parsed.estimated, false); assert.equal(parsed.lines[0].words[1].startTime, 1500);
  assert.equal(parsed.lines[0].words[1].endTime, 2500);
});
// Untimed text remains readable without inventing a song-long timeline.
test('plain lyrics and empty input', () => {
  assert.equal(parseLyrics({ format: 'lrc', source: '', text: '第一行\n第二行' }, 1000).plain, '第一行\n第二行');
  assert.equal(parseLyrics({ format: 'lrc', source: '', text: '' }, 1000).lines.length, 0);
  assert.deepEqual(spreadWords('   ', 0, 1000), []);
});
