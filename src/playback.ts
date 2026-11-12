import type { PlaybackCommand, PlaybackState } from './types';

// UI clock and event adapter only. No media element, file reads or decoding.
// A command generation prevents delayed native events from undoing a newer
// selection/seek/stop; serial numbers order snapshots within that generation.
export class NativePlayback extends EventTarget {
  private target = '';
  private intent = 0;
  private serial = 0;
  private position = 0;
  private timestamp = performance.now();
  private level = .7;
  private off: () => void;
  duration = 0;
  paused = true;
  ended = false;
  error: string | null = null;
  constructor() { super(); this.off = window.music.onPlayback(state => this.apply(state)); }
  get src() { return this.target; }
  set src(value: string) {
    const id = value.split('/').at(-1) || '';
    if (!/^[0-9a-f]{24}$/.test(id)) throw new Error('无效的歌曲 ID');
    ++this.intent; this.target = id; this.position = 0; this.duration = 0; this.paused = true; this.ended = false; this.error = null;
  }
  get volume() { return this.level; }
  set volume(value: number) { this.level = value; this.send({ action: 'volume', volume: value }); }
  get currentTime() {
    const position = this.position + (this.paused ? 0 : Math.min(.25, (performance.now() - this.timestamp) / 1000));
    return this.duration > 0 ? Math.min(this.duration, position) : position;
  }
  set currentTime(value: number) {
    this.position = value; this.timestamp = performance.now(); this.ended = false;
    this.dispatchEvent(new Event('seeking')); this.dispatchEvent(new Event('timeupdate'));
    this.send({ action: 'seek', position: value });
  }
  async play() {
    if (!this.target) return;
    const request = ++this.intent;
    try { this.apply(await window.music.playback({ action: 'play', id: this.target, request })); }
    catch (error) { if (request === this.intent) this.fail(error); throw error; }
  }
  pause() {
    this.position = this.currentTime; this.timestamp = performance.now(); this.paused = true;
    this.dispatchEvent(new Event('pause')); this.send({ action: 'pause' });
  }
  removeAttribute(name: string) { if (name === 'src') { ++this.intent; this.target = ''; this.duration = this.position = 0; this.paused = true; this.ended = false; this.error = null; } }
  load() { this.send({ action: 'stop' }); this.dispatchEvent(new Event('timeupdate')); }
  dispose() { this.off(); }
  private send(command: Omit<PlaybackCommand, 'request'>) {
    const request = ++this.intent;
    void window.music.playback({ ...command, request }).then(state => this.apply(state)).catch(error => { if (request === this.intent) this.fail(error); });
  }
  private fail(error: unknown) { this.paused = true; this.error = String(error); this.dispatchEvent(new Event('error')); }
  private apply(state: PlaybackState) {
    if (state.request < this.intent || state.serial <= this.serial) return;
    this.serial = state.serial;
    const changed = this.duration !== state.duration || this.target !== state.id;
    const wasPaused = this.paused, wasEnded = this.ended;
    this.target = state.id; this.position = state.position; this.timestamp = performance.now(); this.duration = state.duration;
    this.paused = !state.playing; this.ended = state.ended; this.level = state.volume; this.error = state.error || null;
    if (changed) this.dispatchEvent(new Event('loadedmetadata'));
    this.dispatchEvent(new Event('timeupdate'));
    if (wasPaused !== this.paused) this.dispatchEvent(new Event(this.paused ? 'pause' : 'play'));
    if (this.error) this.dispatchEvent(new Event('error'));
    if (!wasEnded && this.ended) this.dispatchEvent(new Event('ended'));
  }
}
