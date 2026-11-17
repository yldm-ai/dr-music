import { test } from 'node:test';
import assert from 'node:assert/strict';
import { hasSourceTranslation, modelSize, providerOf, sourceLanguage, translateLines, translateState, type TranslatorStatus } from '../src/lib/translate';

test('the lyric language comes from its script', () => {
  assert.equal(sourceLanguage('[00:01.00]测试歌词'), 'zh');
  assert.equal(sourceLanguage('<p>夜に駆ける</p>'), 'ja');
  assert.equal(sourceLanguage('사랑해'), 'ko');
  assert.equal(sourceLanguage('[00:01.00]Hello world'), 'en');
});

test('a target is offered only when the chosen provider is set up, and never for same-language lyrics', () => {
  const bilibili = { model: 'Index-Translate-2B', models: ['Index-Translate-2B', 'Index-Translate-9B', 'Index-Translate-35B-A3B'] };
  const index: TranslatorStatus = { provider: 'bilibili', bilibili, niutrans: { configured: false, appId: '' } };
  const niutrans = (configured: boolean): TranslatorStatus => ({ provider: 'niutrans', bilibili, niutrans: { configured, appId: configured ? 'app' : '' } });
  assert.equal(translateState(undefined, 'zh', 'en'), 'checking');
  assert.equal(translateState(index, 'zh', 'zh'), 'same');
  assert.equal(translateState(index, 'ja', 'zh'), 'ready'); // the free API needs no setup
  assert.equal(translateState(niutrans(false), 'en', 'zh'), 'no-key');
  assert.equal(translateState(niutrans(true), 'en', 'zh'), 'ready');
});

test('Index-Translate is the default provider, also for libraries that chose the removed local model', () => {
  assert.equal(providerOf(undefined), 'bilibili');
  assert.equal(providerOf('local'), 'bilibili');
  assert.equal(providerOf('niutrans'), 'niutrans');
  assert.equal(modelSize('Index-Translate-35B-A3B'), '35B-A3B');
});

test('lines go through the bridge, and a superseded request is dropped', async () => {
  const calls: unknown[] = [];
  (globalThis as unknown as { window: unknown }).window = { music: { translateLines: async (lines: string[], source: string, target: string) => { calls.push([lines, source, target]); return lines.map(l => l && `EN ${l}`); } } };
  assert.deepEqual(await translateLines(['你好', ''], 'zh', 'en', new AbortController().signal), ['EN 你好', '']);
  assert.deepEqual(calls, [[['你好', ''], 'zh', 'en']]);
  const controller = new AbortController(); controller.abort();
  await assert.rejects(translateLines(['你好'], 'zh', 'en', controller.signal), /aborted/);
});

test('a lyric offers its own translation only when some line has one', () => {
  assert.equal(hasSourceTranslation([{ translatedLyric: '' }, {}]), false);
  assert.equal(hasSourceTranslation([{ translatedLyric: ' ' }, { translatedLyric: '你好' }]), true);
});
