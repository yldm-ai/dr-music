import { useLayoutEffect, useRef, useState } from 'react';

const THUMB = 8; // width of the (invisible) native thumb, i.e. the travel inset on both ends
const STEP = .2; // seconds between visible thumb / fill updates

/**
 * Playback progress with a self-drawn thumb.
 *
 * The visible thumb moves in discrete 200 ms steps and lands on whole device pixels. Continuous sub-pixel
 * movement made the 5px hover bar render anti-aliased across changing pixel columns, so it looked
 * like it kept swelling and recovering while playing. Dragging still follows the pointer directly.
 */
export function ProgressSlider({ position, duration, disabled, onSeek }: { position: number; duration: number; disabled: boolean; onSeek: (seconds: number) => void }) {
  const root = useRef<HTMLSpanElement>(null);
  const [width, setWidth] = useState(0);
  const [dragging, setDragging] = useState(false);
  useLayoutEffect(() => {
    const element = root.current;
    if (!element) return;
    const observer = new ResizeObserver(() => setWidth(element.getBoundingClientRect().width));
    observer.observe(element); setWidth(element.getBoundingClientRect().width);
    return () => observer.disconnect();
  }, []);
  const shown = dragging ? position : Math.floor(position / STEP) * STEP;
  const ratio = duration ? Math.min(1, Math.max(0, shown / duration)) : 0;
  const dpr = window.devicePixelRatio || 1;
  const snap = (value: number) => Math.round(value * dpr) / dpr;
  // Left edge of the 8px thumb box, snapped in page coordinates (the slider itself may sit at a
  // fractional x); the fill ends exactly at the thumb's center.
  const origin = root.current?.getBoundingClientRect().left ?? 0;
  const left = snap(origin + (width - THUMB) * ratio) - origin;
  const end = () => setDragging(false);
  return <span ref={root} className="progress-slider" data-dragging={dragging ? 'true' : undefined} style={{ '--progress': `${left + THUMB / 2}px` } as React.CSSProperties}>
    <input
      aria-label="播放进度"
      type="range"
      min="0"
      max={duration || 1}
      step="0.1"
      value={Math.min(position, duration || 0)}
      disabled={disabled}
      onPointerDown={() => setDragging(true)}
      onPointerUp={end}
      onPointerCancel={end}
      onLostPointerCapture={end}
      onBlur={end}
      onChange={event => onSeek(Number(event.target.value))}
    />
    <span className="progress-thumb" aria-hidden="true" style={{ transform: `translateX(${left}px)` }}><i/></span>
  </span>;
}
