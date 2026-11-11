import { useId, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { Check, ChevronDown } from 'lucide-react';

export function Select({ label, value, options, onChange }: { label: string; value: string; options: { value: string; label: string }[]; onChange: (value: string) => void }) {
  const id = useId();
  const trigger = useRef<HTMLButtonElement>(null);
  const list = useRef<HTMLDivElement>(null);
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const [position, setPosition] = useState({ left: 0, top: 0, width: 200, maxHeight: 280 });
  const selected = Math.max(0, options.findIndex(option => option.value === value));
  const close = () => { setOpen(false); trigger.current?.focus({ preventScroll: true }); };
  useLayoutEffect(() => {
    if (!open) return;
    const place = () => {
      const rect = trigger.current!.getBoundingClientRect();
      const width = Math.min(Math.max(rect.width, 216), innerWidth - 24);
      const below = innerHeight - rect.bottom - 12;
      const above = rect.top - 12;
      const height = Math.min(options.length * 38 + 12, 280);
      const up = below < height && above > below;
      const maxHeight = Math.max(60, Math.min(280, up ? above : below));
      setPosition({ left: Math.max(12, Math.min(rect.left, innerWidth - width - 12)), top: up ? rect.top - Math.min(height, maxHeight) - 6 : rect.bottom + 6, width, maxHeight });
    };
    place(); list.current?.focus({ preventScroll: true });
    const outside = (event: PointerEvent) => { if (!trigger.current?.contains(event.target as Node) && !list.current?.contains(event.target as Node)) setOpen(false); };
    const dismiss = () => setOpen(false);
    const scroll = (event: Event) => { if (!list.current?.contains(event.target as Node)) dismiss(); };
    document.addEventListener('pointerdown', outside, true);
    window.addEventListener('resize', dismiss);
    document.addEventListener('scroll', scroll, true);
    return () => { document.removeEventListener('pointerdown', outside, true); window.removeEventListener('resize', dismiss); document.removeEventListener('scroll', scroll, true); };
  }, [open, options.length]);
  useLayoutEffect(() => { if (open) list.current?.querySelector(`[data-index="${active}"]`)?.scrollIntoView({ block: 'nearest' }); }, [active, open]);
  return <>
    <button ref={trigger} type="button" className="select-trigger" role="combobox" aria-label={label} aria-expanded={open} aria-controls={open ? id : undefined} aria-haspopup="listbox" onClick={() => { setActive(selected); setOpen(!open); }} onKeyDown={event => { if (['ArrowDown', 'ArrowUp', 'Enter', ' '].includes(event.key)) { event.preventDefault(); event.stopPropagation(); setActive(selected); setOpen(true); } }}>
      <span>{options[selected]?.label}</span><ChevronDown size={15}/>
    </button>
    {open && createPortal(<div ref={list} id={id} role="listbox" aria-label={label} tabIndex={-1} aria-activedescendant={`${id}-${active}`} className="select-popover" style={position} onBlur={event => { if (!event.currentTarget.contains(event.relatedTarget) && !trigger.current?.contains(event.relatedTarget)) setOpen(false); }} onKeyDown={event => {
      event.stopPropagation();
      if (event.key === 'Escape' || event.key === 'Tab') { if (event.key === 'Escape') event.preventDefault(); close(); }
      else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); setActive(index => (index + (event.key === 'ArrowDown' ? 1 : -1) + options.length) % options.length); }
      else if (event.key === 'Home' || event.key === 'End') { event.preventDefault(); setActive(event.key === 'Home' ? 0 : options.length - 1); }
      else if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); onChange(options[active].value); close(); }
      else if (event.key.length === 1) { const index = options.findIndex(option => option.label.toLocaleLowerCase().startsWith(event.key.toLocaleLowerCase())); if (index >= 0) setActive(index); }
    }}>{options.map((option, index) => <div key={option.value} id={`${id}-${index}`} data-index={index} role="option" aria-selected={option.value === value} className={active === index ? 'highlighted' : ''} onPointerMove={() => setActive(index)} onPointerDown={event => event.preventDefault()} onClick={() => { onChange(option.value); close(); }}><span>{option.label}</span>{option.value === value && <Check size={15}/>}</div>)}</div>, document.body)}
  </>;
}
