import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseLyrics, parseKrc, spreadWords } from '../src/lib/lyrics';

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

test('online provider formats keep their word timings: QQ QRC, Kugou KRC, NetEase YRC', () => {
  const qrc = parseLyrics({ format: 'qrc', source: 'QQ', text: '[ti:晴天]\n[ar:周杰伦]\n[1000,1500]故(1000,500)事(1500,1000)\n[2500,1000]的(2500,1000)' }, 10000);
  assert.deepEqual(qrc.lines.map(l => l.words.map(w => [w.word, w.startTime, w.endTime])), [[['故', 1000, 1500], ['事', 1500, 2500]], [['的', 2500, 3500]]]);
  assert.equal(qrc.estimated, false);
  const krc = parseKrc('[id:$00000000]\n[language:eyJ9]\n[1000,1500]<0,500,0>故<500,1000,0>事\n[2500,1000]<0,1000,0>的');
  assert.deepEqual(krc.map(l => [l.startTime, l.endTime, l.words.map(w => [w.word, w.startTime, w.endTime])]), [[1000, 2500, [['故', 1000, 1500], ['事', 1500, 2500]]], [2500, 3500, [['的', 2500, 3500]]]]);
  assert.equal(parseLyrics({ format: 'krc', source: 'Kugou', text: '[1000,1000]<0,1000,0>好' }, 5000).lines[0].words[0].word, '好');
  // NetEase YRC starts with JSON credit lines; they must not become lyrics.
  const yrc = parseLyrics({ format: 'yrc', source: 'NetEase', text: '{"t":0,"c":[{"tx":"作词: "},{"tx":"方文山"}]}\n[1000,1000](1000,500,0)晴(1500,500,0)天' }, 5000);
  assert.deepEqual(yrc.lines.map(l => l.words.map(w => w.word).join('')), ['晴天']);
});