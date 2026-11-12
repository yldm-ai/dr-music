import { test } from 'node:test';
import assert from 'node:assert/strict';
import { NativePlayback } from '../src/playback';
import type { PlaybackCommand, PlaybackState } from '../src/types';

test('native UI clock rejects stale switch/seek/stop snapshots and deduplicates EOF', async () => {
  let listener: (state: PlaybackState) => void = () => {};
  const calls: { command: PlaybackCommand; resolve: (state: PlaybackState) => void }[] = [];
  Object.assign(globalThis, { window: { music: {
    onPlayback: (callback: typeof listener) => { listener = callback; return () => { listener = () => {}; }; },
    playback: (command: PlaybackCommand) => new Promise<PlaybackState>(resolve => calls.push({ command, resolve })),
  } } });
  const audio = new NativePlayback();
  const first = 'a'.repeat(24), second = 'b'.repeat(24);
  const snapshot = (index: number, extra: Partial<PlaybackState> = {}): PlaybackState => ({ id: first, duration: 10, position: 1, playing: true, ended: false, volume: .7, error: '', serial: index + 1, request: calls[index].command.request, ...extra });
  audio.src = `https://media.dr-music.local/audio/${first}`;
  const play = audio.play(); calls[0].resolve(snapshot(0)); await play;
  assert.equal(audio.paused, false); assert.equal(audio.duration, 10); assert.ok(audio.currentTime >= 1);
  audio.src = `https://media.dr-music.local/audio/${second}`;
  listener(snapshot(0, { serial: 100 })); assert.equal(audio.src, second); assert.equal(audio.currentTime, 0);
  const next = audio.play(); calls[1].resolve(snapshot(1, { id: second })); await next;
  audio.currentTime = 5;
  listener(snapshot(1, { id: second, position: 1, serial: 101 })); assert.ok(audio.currentTime >= 5);
  calls[2].resolve(snapshot(2, { id: second, position: 5, playing: false })); await Promise.resolve();
  assert.equal(audio.currentTime, 5); assert.equal(audio.paused, true);
  let ended = 0; audio.addEventListener('ended', () => ++ended);
  listener(snapshot(2, { id: second, position: 10, playing: false, ended: true, serial: 102 }));
  listener(snapshot(2, { id: second, position: 10, playing: false, ended: true, serial: 103 })); assert.equal(ended, 1);
  audio.removeAttribute('src'); audio.load();
  listener(snapshot(2, { serial: 104 })); assert.equal(audio.src, ''); assert.equal(audio.currentTime, 0);
  calls[3].resolve(snapshot(3, { id: '', position: 0, duration: 0, playing: false, serial: 105 })); await Promise.resolve();
  assert.equal(audio.src, ''); assert.equal(audio.paused, true); audio.dispose();
});
