import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { Volume1, Volume2, VolumeX } from 'lucide-react';

type Orientation = 'vertical' | 'horizontal';
const STEP = .01;
const clamp = (value: number) => Math.max(0, Math.min(1, Math.round(value * 100) / 100));

/** Volume button that reveals a slider while hovered or focused. Wheel and arrow keys move by 1%. */
export function VolumeControl({ value, onChange, label = '音量', orientation = 'vertical', className = '' }: { value: number; onChange: (value: number) => void; label?: string; orientation?: Orientation; className?: string }) {
  const root = useRef<HTMLDivElement>(null);
  const track = useRef<HTMLDivElement>(null);
  const latest = useRef({ value, onChange }); latest.current = { value, onChange };
  const previous = useRef(value || .7);
  if (value > 0) previous.current = value;
  const [dragging, setDragging] = useState(false);

  const set = (next: number) => {
    next = clamp(next);
    if (next === latest.current.value) return;
    latest.current.value = next;
    latest.current.onChange(next);
  };

  useEffect(() => {
    const element = root.current;
    if (!element) return;
    const wheel = (event: WheelEvent) => {
      const delta = event.deltaY || event.deltaX;
      if (!delta || event.ctrlKey) return;
      event.preventDefault(); event.stopPropagation();
      set(latest.current.value + (delta < 0 ? STEP : -STEP));
    };
    element.addEventListener('wheel', wheel, { passive: false });
    return () => element.removeEventListener('wheel', wheel);
  }, []);

  const fromPointer = (event: React.PointerEvent) => {
    const box = track.current?.getBoundingClientRect();
    if (!box) return;
    set(orientation === 'vertical' ? (box.bottom - event.clientY) / box.height : (event.clientX - box.left) / box.width);
  };
  const keyDown = (event: React.KeyboardEvent) => {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    const step = event.key === 'PageUp' ? .1 : event.key === 'PageDown' ? -.1 : event.key === 'ArrowUp' || event.key === 'ArrowRight' ? STEP : event.key === 'ArrowDown' || event.key === 'ArrowLeft' ? -STEP : 0;
    if (step) { event.preventDefault(); set(latest.current.value + step); }
    else if (event.key === 'Home') { event.preventDefault(); set(0); }
    else if (event.key === 'End') { event.preventDefault(); set(1); }
    else if (event.key === 'Escape' && root.current?.contains(document.activeElement)) {
      event.preventDefault(); event.stopPropagation();
      (document.activeElement as HTMLElement).blur();
    }
  };

  // The thumb's centre lands on whole device pixels, as the progress slider's does: placed by a percentage it fell
  // between pixel columns at every other step, and the narrow hover bar looked thicker, then thinner again.
  const [box, setBox] = useState<{ origin: number; length: number; pad: number }>();
  useLayoutEffect(() => {
    const element = track.current;
    if (!element) return;
    const measure = () => {
      const rect = element.getBoundingClientRect(), vertical = orientation === 'vertical';
      const pad = parseFloat(getComputedStyle(element).getPropertyValue('--vp-pad')) || 0;
      setBox(old => { const next = { origin: vertical ? rect.bottom : rect.left, length: vertical ? rect.height : rect.width, pad }; return old && old.origin === next.origin && old.length === next.length && old.pad === next.pad ? old : next; });
    };
    measure();
    const observer = new ResizeObserver(measure); observer.observe(element);
    return () => observer.disconnect();
  }, [orientation]);
  const dpr = window.devicePixelRatio || 1;
  const thumbAt = box ? (() => {
    const along = box.pad + (box.length - 2 * box.pad) * value; // from the start of the track (bottom when vertical)
    const page = orientation === 'vertical' ? box.origin - along : box.origin + along;
    const snapped = Math.round(page * dpr) / dpr;
    return orientation === 'vertical' ? snapped - (box.origin - box.length) : snapped - box.origin;
  })() : undefined;
  const percent = Math.round(value * 100);
  // Muted shows the crossed speaker; any audible level keeps at least one sound wave.
  const Icon = value === 0 ? VolumeX : value < .5 ? Volume1 : Volume2;
  return <div ref={root} className={`volume-popover ${className}`} data-orientation={orientation} data-open={dragging ? 'true' : undefined} onKeyDown={keyDown}>
    <button type="button" className="icon-button volume-toggle" aria-label={`${label}静音 / 恢复`} onClick={() => set(value ? 0 : previous.current)}><Icon size={19}/></button>
    <div className="volume-panel">
      <div className="volume-panel-inner">
        <div
          ref={track}
          className="volume-track"
          role="slider"
          tabIndex={0}
          aria-label={label}
          aria-orientation={orientation}
          aria-valuemin={0}
          aria-valuemax={100}
          aria-valuenow={percent}
          aria-valuetext={`${percent}%`}
          style={{ '--volume': `${percent}%`, '--v': value, ...(thumbAt !== undefined ? { '--thumb-at': `${thumbAt}px` } : {}) } as React.CSSProperties}
          onPointerDown={event => { if (event.button) return; event.currentTarget.setPointerCapture(event.pointerId); setDragging(true); fromPointer(event); }}
          onPointerMove={event => { if (dragging) fromPointer(event); }}
          onPointerUp={event => { event.currentTarget.releasePointerCapture(event.pointerId); setDragging(false); }}
          onPointerCancel={() => setDragging(false)}
        >
          <span className="volume-rail"><span className="volume-fill"/></span>
          <span className="volume-thumb"/>
        </div>
        <output className="volume-value" aria-label={`${label}百分比`}>{percent}</output>
      </div>
    </div>
  </div>;
}
