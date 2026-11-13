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
test("the providers' own romanization is kept: Kugou [language:], NetEase romalrc, QQ contentroma", () => {
  const b64 = (value: unknown) => Buffer.from(JSON.stringify(value), 'utf8').toString('base64');
  // Kugou: type 0 is the romanization, one word list per lyric line; type 1 (translation) is not used here.
  const language = b64({ content: [{ language: 0, type: 1, lyricContent: [['你'], ['我']] }, { language: 0, type: 0, lyricContent: [['ki ', 'mi'], ['yo ru ']] }], version: 1 });
  const krc = parseKrc(`[language:${language}]\n[1000,1000]<0,500,0>君<500,500,0>の\n[2000,1000]<0,1000,0>夜`);
  assert.deepEqual(krc[0].words.map(w => w.romanWord), ['ki', 'mi']);
  assert.equal(krc[1].romanLyric, 'yo ru');
  assert.equal(krc[0].translatedLyric, '');
  // NetEase: an LRC timed like the lyric, matched to the line starting within a second.
  const netease = parseLyrics({ format: 'yrc', source: 'NetEase', text: '[1000,1000](1000,500,0)君(1500,500,0)の\n[3000,1000](3000,1000,0)夜', roman: '[00:01.02]kimi no\n[00:03.00]yoru\n[00:09.00]extra', romanFormat: 'lrc' }, 10000);
  assert.deepEqual(netease.lines.map(l => l.romanLyric), ['kimi no', 'yoru']);
  // QQ Music: a QRC with the same words, so the romanization goes word by word.
  const qq = parseLyrics({ format: 'qrc', source: 'QQ', text: '[1000,1000]君(1000,500)の(1500,500)', roman: '[ti:x]\n[1000,1000]ki mi (1000,500)no (1500,500)', romanFormat: 'qrc' }, 5000);
  assert.deepEqual(qq.lines[0].words.map(w => w.romanWord), ['ki mi', 'no']);
  // A broken romanization file never breaks the lyric.
  assert.equal(parseLyrics({ format: 'lrc', source: '', text: '[00:01.00]君', roman: 'nonsense', romanFormat: 'qrc' }, 5000).lines.length, 1);
});