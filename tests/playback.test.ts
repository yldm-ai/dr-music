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

test('native UI clock never steps back on jittered snapshots, converges on drift and snaps on real jumps', async () => {
  let listener: (state: PlaybackState) => void = () => {};
  const calls: { command: PlaybackCommand; resolve: (state: PlaybackState) => void }[] = [];
  Object.assign(globalThis, { window: { music: {
    onPlayback: (callback: typeof listener) => { listener = callback; return () => { listener = () => {}; }; },
    playback: (command: PlaybackCommand) => new Promise<PlaybackState>(resolve => calls.push({ command, resolve })),
  } } });
  let wall = 1000;
  const realNow = performance.now;
  Object.defineProperty(performance, 'now', { value: () => wall, configurable: true, writable: true });
  try {
    const audio = new NativePlayback();
    const id = 'c'.repeat(24);
    let serial = 0;
    const snapshot = (position: number, extra: Partial<PlaybackState> = {}): PlaybackState => ({ id, duration: 600, position, playing: true, ended: false, volume: .7, error: '', serial: ++serial, request: calls[0].command.request, ...extra });
    audio.src = `https://media.dr-music.local/audio/${id}`;
    const play = audio.play(); calls[0].resolve(snapshot(0)); await play;
    // Native reports every ~100 ms; arrival jitters and the reported position lags by up to 40 ms.
    let previous = audio.currentTime, truth = 0;
    for (let i = 0; i < 100; ++i) {
      for (let frame = 0; frame < 6; ++frame) {
        wall += 100 / 6; truth += 100 / 6000;
        const now = audio.currentTime; assert.ok(now >= previous, `clock went back ${previous} -> ${now}`); previous = now;
      }
      listener(snapshot(truth - (i % 3) * .02));
      const now = audio.currentTime; assert.ok(now >= previous, `clock went back on snapshot ${previous} -> ${now}`); previous = now;
    }
    assert.ok(Math.abs(audio.currentTime - truth) < .05, `clock drifted: ${audio.currentTime} vs ${truth}`);
    // A position far from the prediction (device stall, external seek) snaps immediately.
    listener(snapshot(truth - 3)); assert.ok(Math.abs(audio.currentTime - (truth - 3)) < 1e-9);
    audio.dispose();
  } finally {
    Object.defineProperty(performance, 'now', { value: realNow, configurable: true, writable: true });
  }
});
