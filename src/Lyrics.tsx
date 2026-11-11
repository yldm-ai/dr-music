import { useEffect, useRef, useState } from 'react';
import { LyricPlayer, LyricLineMouseEvent } from '@applemusic-like-lyrics/core';
import '@applemusic-like-lyrics/core/style.css';
import { parseLyrics } from './lib/lyrics';
import type { LyricFile } from './types';

export function Lyrics({ file, audio, offset, onSeek }: { file: LyricFile; audio: HTMLAudioElement; offset: number; onSeek: (seconds: number) => void }) {
  const host = useRef<HTMLDivElement>(null);
  const [info, setInfo] = useState('');
  const [plain, setPlain] = useState('');
  const seekRef = useRef(onSeek); seekRef.current = onSeek;
  useEffect(() => {
    if (!host.current) return;
    let parsed;
    try { parsed = parseLyrics(file, audio.duration * 1000); }
    catch (error) { setInfo(String(error)); setPlain(''); return; }
    setPlain(parsed.plain);
    setInfo(parsed.estimated ? '逐行歌词 · 字词时间为估算' : parsed.lines.length ? '逐字时间轴' : '');
    if (!parsed.lines.length) return;
    const player = new LyricPlayer();
    player.setAlignPosition(0.38);
    player.setEnableBlur(true);
    host.current.appendChild(player.getElement());
    player.setLyricLines(parsed.lines, Math.max(0, audio.currentTime * 1000 + offset));
    const click = (event: Event) => { const line = parsed.lines[(event as LyricLineMouseEvent).lineIndex]; if (line) seekRef.current(Math.max(0, (line.startTime - offset) / 1000)); };
    player.addEventListener('line-click', click);
    const sync = () => audio.paused ? player.pause() : player.resume();
    const seek = () => player.setCurrentTime(Math.max(0, audio.currentTime * 1000 + offset), true);
    audio.addEventListener('play', sync); audio.addEventListener('pause', sync); audio.addEventListener('seeking', seek); sync();
    let frame = 0, previous = performance.now();
    const tick = (now: number) => {
      if (!document.hidden) {
        player.setCurrentTime(Math.max(0, audio.currentTime * 1000 + offset));
        player.update(Math.min(50, now - previous));
      }
      previous = now; frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => { cancelAnimationFrame(frame); audio.removeEventListener('play', sync); audio.removeEventListener('pause', sync); audio.removeEventListener('seeking', seek); player.removeEventListener('line-click', click); player.dispose(); };
  }, [file, audio, offset]);
  return <div className="lyrics-shell"><div className="lyrics-host" ref={host}/>{plain && <div className="plain-lyrics">{plain}</div>}{info && <span className="lyric-info">{info}</span>}</div>;
}
