import type { NativePlayback } from './playback';
import { useEffect, useRef, useState } from 'react';
import { AnimatePresence, motion } from 'motion/react';
import { LyricPlayer, LyricLineMouseEvent } from '@applemusic-like-lyrics/core';
import '@applemusic-like-lyrics/core/style.css';
import { parseLyrics } from './lib/lyrics';
import { annotate, lineText, loadReader, sourceRoman, type RubyChoice, type Segment } from './lib/ruby';
import { sourceLanguage, translateLines, type TranslateTarget } from './lib/translate';
import type { LyricFile } from './types';

const smoothEase: [number, number, number, number] = [.22, 1, .36, 1];
const textTransition = { duration: .36, ease: smoothEase } as const;

export interface LyricTypography { font?: string; weight?: number; scale?: number }
/** CSS custom properties consumed by .lyrics-shell; empty / 0 values fall back to the app defaults. */
export const lyricTypographyStyle = ({ font, weight, scale }: LyricTypography) => ({
  ...(font ? { '--dr-lyric-font': `"${font}", var(--font-ui)` } : {}),
  ...(weight ? { '--dr-lyric-weight': String(weight) } : {}),
  '--dr-lyric-scale': String(scale || 1),
}) as React.CSSProperties;

export interface LyricRuby { kind: RubyChoice; below: boolean }
export type TranslateState = { state: 'idle' | 'working' | 'done' } | { state: 'error'; message: string } | { state: 'downloading'; progress: number };

export function Lyrics({ file, audio, offset, active, onSeek, typography = {}, ruby, translate, onTranslateState, onRubyError }: { file: LyricFile; audio: NativePlayback; offset: number; active: boolean; onSeek: (seconds: number) => void; typography?: LyricTypography; ruby?: LyricRuby; translate?: TranslateTarget | 'source'; onTranslateState?: (state: TranslateState) => void; onRubyError?: (message: string) => void }) {
  // Readings for the chosen guide, read for the whole lyric at once; until they arrive, lyrics show without them.
  const rubyErrorRef = useRef(onRubyError); rubyErrorRef.current = onRubyError;
  const [readings, setReadings] = useState<{ key: string; lines: Segment[][] }>();
  const readKind = ruby && ruby.kind !== 'romaji-online' ? ruby.kind : undefined;
  const readKey = readKind ? `${readKind}\n${file.text}` : '';
  useEffect(() => {
    if (!readKind || readings?.key === readKey) return;
    let texts: string[];
    try { texts = parseLyrics(file, 0).lines.map(lineText); } catch { return; }
    let live = true;
    const key = readKey;
    loadReader(readKind).then(read => read(texts)).then(lines => { if (live) setReadings({ key, lines }); },
      error => { if (live) rubyErrorRef.current?.(error instanceof Error ? error.message : String(error)); });
    return () => { live = false; };
  }, [readKey]);
  const lineReadings = readKind && readings?.key === readKey ? readings.lines : undefined;
  const below = !!ruby?.below, spaced = readKind !== 'kana';
  const showSourceRoman = ruby?.kind === 'romaji-online' ? (below ? 'below' : 'above') : 'off';
  // 译: the lyric's own translation ('source'), or every line machine-translated (小牛翻译 / local Hunyuan), shown under the line.
  const reportRef = useRef(onTranslateState); reportRef.current = onTranslateState;
  const [translated, setTranslated] = useState<{ key: string; lines: string[] }>();
  const machine = translate === 'zh' || translate === 'en' ? translate : undefined;
  const translateKey = machine ? `${translate}\n${file.text}` : '';
  useEffect(() => {
    const report = (state: TranslateState) => reportRef.current?.(state);
    if (!machine) { report({ state: 'idle' }); return; }
    if (translated?.key === translateKey) { report({ state: 'done' }); return; }
    let texts: string[];
    try { texts = parseLyrics(file, 0).lines.map(line => line.words.map(w => w.word).join('').trim()); } catch { return; }
    if (!texts.some(Boolean)) return;
    const controller = new AbortController(), key = translateKey;
    report({ state: 'working' });
    translateLines(texts, sourceLanguage(texts.join('\n')), machine, controller.signal)
      .then(lines => { setTranslated({ key, lines }); report({ state: 'done' }); })
      .catch(error => { if (!controller.signal.aborted) report({ state: 'error', message: error instanceof Error ? error.message : String(error) }); });
    return () => controller.abort();
  }, [translateKey]);
  const translation = machine && translated?.key === translateKey ? translated.lines : undefined;
  const host = useRef<HTMLDivElement>(null);
  const [info, setInfo] = useState('');
  const [plain, setPlain] = useState('');
  const seekRef = useRef(onSeek); seekRef.current = onSeek;
  const activeRef = useRef(active); activeRef.current = active;
  useEffect(() => {
    if (!host.current) return;
    let parsed;
    try { parsed = parseLyrics(file, audio.duration * 1000); }
    catch (error) { setInfo(String(error)); setPlain(''); return; }
    setPlain(parsed.plain);
    setInfo('');
    if (!parsed.lines.length) return;
    // The lyric's own romanization only shows when chosen in 音 (romaji-online).
    parsed = { ...parsed, lines: sourceRoman(parsed.lines, showSourceRoman) };
    if (lineReadings) parsed = { ...parsed, lines: annotate(parsed.lines, lineReadings, below, spaced) };
    // The lyric's own translation only shows when chosen in 译; a machine translation replaces it.
    const showSource = translate === 'source';
    parsed = { ...parsed, lines: parsed.lines.map(line => showSource || !line.translatedLyric ? line : { ...line, translatedLyric: '' }) };
    if (translation) parsed = { ...parsed, lines: parsed.lines.map((line, i) => translation[i] ? { ...line, translatedLyric: translation[i] } : line) };
    const player = new LyricPlayer();
    player.setAlignPosition(0.38);
    player.setWordFadeWidth(.35);
    player.setEnableScale(false);
    player.setEnableBlur(true);
    player.setEnableSpring(true);
    host.current.appendChild(player.getElement());
    player.setLyricLines(parsed.lines, Math.max(0, audio.currentTime * 1000 + offset));
    const click = (event: Event) => { const line = parsed.lines[(event as LyricLineMouseEvent).lineIndex]; if (line) seekRef.current(Math.max(0, (line.startTime - offset) / 1000)); };
    player.addEventListener('line-click', click);
    const sync = () => audio.paused ? player.pause() : player.resume();
    const seek = () => player.setCurrentTime(Math.max(0, audio.currentTime * 1000 + offset), true);
    audio.addEventListener('play', sync); audio.addEventListener('pause', sync); audio.addEventListener('seeking', seek); sync();
    const blurCache = new WeakMap<HTMLElement, string>();
    const softenBlur = () => {
      // haruhikage uses AMLL with full-screen, larger lyrics. Keep its depth
      // effect subtle in our smaller lyric pane, without changing AMLL timing
      // or relying on its private blur resolver / generated CSS class names.
      const maxBlur = Math.min(1.5, player.baseFontSize * 0.045);
      for (const group of player.currentLyricGroups) {
        const element = group.getElement();
        const radius = Math.min(maxBlur, Math.max(0, group.blur) * 0.35).toFixed(2);
        if (!blurCache.has(element)) element.classList.add('dr-lyric-line');
        if (blurCache.get(element) !== radius) {
          element.style.setProperty('--dr-lyric-blur', `${radius}px`);
          blurCache.set(element, radius);
        }
      }
    };
    let frame = 0, previous = performance.now();
    const tick = (now: number) => {
      if (activeRef.current && !document.hidden) {
        player.setCurrentTime(Math.max(0, audio.currentTime * 1000 + offset));
        player.update(Math.min(50, now - previous));
        softenBlur();
      }
      previous = now; frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => { cancelAnimationFrame(frame); audio.removeEventListener('play', sync); audio.removeEventListener('pause', sync); audio.removeEventListener('seeking', seek); player.removeEventListener('line-click', click); player.dispose(); };
    // Font metrics change line heights, so rebuild AMLL's layout when typography changes.
  }, [file, audio, offset, typography.font, typography.weight, typography.scale, lineReadings, below, spaced, showSourceRoman, translate, translation]);
  return <div className="lyrics-shell" style={lyricTypographyStyle(typography)}>
    <motion.div className="lyrics-host" ref={host} initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={textTransition}/>
    <AnimatePresence>
      {plain && <motion.div key={`${file.source}:${plain.slice(0, 24)}`} className="plain-lyrics" initial={{ opacity: 0, y: 4 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -3 }} transition={textTransition}>{plain}</motion.div>}
      {info && <span className="lyric-info" role="status">{info}</span>}
    </AnimatePresence>
  </div>;
}
