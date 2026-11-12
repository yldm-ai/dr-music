import { useEffect, useRef } from 'react';
import { Volume2, VolumeX } from 'lucide-react';

export function VolumeControl({ value, onChange, label = '音量' }: { value: number; onChange: (value: number) => void; label?: string }) {
  const slider = useRef<HTMLInputElement>(null);
  const latest = useRef({ value, onChange }); latest.current = { value, onChange };
  const previous = useRef(value || .7);
  if (value > 0) previous.current = value;
  useEffect(() => {
    const input = slider.current;
    if (!input) return;
    const wheel = (event: WheelEvent) => {
      if (!event.deltaY || event.ctrlKey) return;
      event.preventDefault(); event.stopPropagation();
      const next = Math.max(0, Math.min(1, Math.round((latest.current.value + (event.deltaY < 0 ? .05 : -.05)) * 100) / 100));
      if (next !== latest.current.value) {
        latest.current.value = next;
        latest.current.onChange(next);
      }
    };
    input.addEventListener('wheel', wheel, { passive: false });
    return () => input.removeEventListener('wheel', wheel);
  }, []);
  const percent = Math.round(value * 100);
  return <div className="volume-control">
    <button type="button" className="icon-button" aria-label={`${label}静音 / 恢复`} data-tooltip="静音 / 恢复音量" onClick={() => onChange(value ? 0 : previous.current)}>{value ? <Volume2 size={19}/> : <VolumeX size={19}/>}</button>
    <input ref={slider} className="volume-slider" aria-label={label} aria-valuetext={`${percent}%`} type="range" min="0" max="1" step="0.01" value={value} onChange={event => onChange(Number(event.target.value))} style={{ '--progress': `${percent}%` } as React.CSSProperties}/>
    <output className="volume-value" aria-label={`${label}百分比`}>{percent}%</output>
  </div>;
}
