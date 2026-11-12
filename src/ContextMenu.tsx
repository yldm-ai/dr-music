import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { Check, ChevronRight } from 'lucide-react';
import type { LucideIcon } from 'lucide-react';

export type MenuItem =
  | { kind?: 'item'; label: string; icon?: LucideIcon; shortcut?: string; checked?: boolean; danger?: boolean; disabled?: boolean; onSelect?: () => void; submenu?: MenuItem[] }
  | { kind: 'separator' }
  | { kind: 'header'; label: string };

type Point = { x: number; y: number };
const GAP = 4, EDGE = 8, SUBMENU_DELAY = 120;
const actionable = (item: MenuItem) => item.kind !== 'separator' && item.kind !== 'header' && !item.disabled;

/** Compact, top-layer context menu with hover / keyboard submenus. */
export function ContextMenu({ items, point, label, onClose }: { items: MenuItem[]; point: Point; label: string; onClose: () => void }) {
  const closeRef = useRef(onClose); closeRef.current = onClose;
  useEffect(() => {
    const close = () => closeRef.current();
    const outside = (event: PointerEvent) => { if (!(event.target as Element).closest?.('.context-menu')) close(); };
    const scroll = (event: Event) => { if (!(event.target as Element).closest?.('.context-menu')) close(); };
    document.addEventListener('pointerdown', outside, true);
    document.addEventListener('scroll', scroll, true);
    window.addEventListener('resize', close); window.addEventListener('blur', close);
    return () => { document.removeEventListener('pointerdown', outside, true); document.removeEventListener('scroll', scroll, true); window.removeEventListener('resize', close); window.removeEventListener('blur', close); };
  }, []);
  return createPortal(<MenuPanel items={items} point={point} label={label} depth={0} onClose={onClose} autoFocus/>, document.body);
}

function MenuPanel({ items, point, label, depth, onClose, onBack, onEnter, autoFocus, flipX }: { items: MenuItem[]; point: Point; label: string; depth: number; onClose: () => void; onBack?: () => void; onEnter?: () => void; autoFocus?: boolean; flipX?: number }) {
  const panel = useRef<HTMLDivElement>(null);
  const [position, setPosition] = useState<Point>(point);
  const [active, setActive] = useState(-1);
  const [open, setOpen] = useState<{ index: number; point: Point; flipX: number; focus: boolean }>();
  const timer = useRef<number | undefined>(undefined);

  useLayoutEffect(() => {
    const box = panel.current!.getBoundingClientRect();
    let x = point.x, y = point.y;
    // Submenus flip to the parent's left edge when they would leave the window.
    if (x + box.width > innerWidth - EDGE) x = flipX !== undefined ? flipX - box.width : innerWidth - EDGE - box.width;
    if (y + box.height > innerHeight - EDGE) y = depth ? innerHeight - EDGE - box.height : Math.max(EDGE, point.y - box.height);
    setPosition({ x: Math.max(EDGE, x), y: Math.max(EDGE, y) });
    if (autoFocus) panel.current!.focus({ preventScroll: true });
  }, [point.x, point.y, flipX, depth, autoFocus]);
  useEffect(() => () => clearTimeout(timer.current), []);

  const openSubmenu = (index: number, focus: boolean) => {
    const element = panel.current?.querySelector<HTMLElement>(`[data-index="${index}"]`);
    const parent = panel.current?.getBoundingClientRect();
    if (!element || !parent) return;
    const row = element.getBoundingClientRect();
    setOpen({ index, point: { x: parent.right + GAP - 2, y: row.top - 5 }, flipX: parent.left - GAP + 2, focus });
  };
  const select = (index: number) => {
    const item = items[index];
    if (!item || !actionable(item) || item.kind === 'separator' || item.kind === 'header') return;
    if (item.submenu) { openSubmenu(index, true); return; }
    item.onSelect?.(); onClose();
  };
  const move = (delta: number) => {
    if (!items.some(actionable)) return;
    let next = active;
    do next = (next + delta + items.length) % items.length; while (!actionable(items[next]));
    setActive(next);
  };
  const keyDown = (event: React.KeyboardEvent) => {
    if (open?.focus) return;
    event.stopPropagation();
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); move(event.key === 'ArrowDown' ? 1 : -1); }
    else if (event.key === 'Home' || event.key === 'End') { event.preventDefault(); setActive(-1); move(event.key === 'Home' ? 1 : -1); }
    else if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); if (active >= 0) select(active); }
    else if (event.key === 'ArrowRight') { event.preventDefault(); const item = items[active]; if (item && item.kind !== 'separator' && item.kind !== 'header' && item.submenu) openSubmenu(active, true); }
    else if (event.key === 'ArrowLeft' && onBack) { event.preventDefault(); onBack(); }
    else if (event.key === 'Escape') { event.preventDefault(); if (onBack) onBack(); else onClose(); }
    else if (event.key === 'Tab') { event.preventDefault(); onClose(); }
  };
  const submenu = open && items[open.index];
  return <>
    <div
      ref={panel}
      className="context-menu"
      role="menu"
      aria-label={label}
      tabIndex={-1}
      data-depth={depth}
      style={{ left: position.x, top: position.y }}
      onKeyDown={keyDown}
      onPointerEnter={onEnter}
      onContextMenu={event => event.preventDefault()}
      onPointerDown={event => event.stopPropagation()}
      onDoubleClick={event => event.stopPropagation()}
    >
      {items.map((item, index) => {
        if (item.kind === 'separator') return <div key={index} className="context-menu-separator" role="separator"/>;
        if (item.kind === 'header') return <div key={index} className="context-menu-header" role="presentation">{item.label}</div>;
        const Icon = item.icon;
        return <button
          key={index}
          type="button"
          role="menuitem"
          data-index={index}
          aria-haspopup={item.submenu ? 'menu' : undefined}
          aria-expanded={item.submenu ? open?.index === index : undefined}
          aria-disabled={item.disabled || undefined}
          aria-label={item.label}
          aria-keyshortcuts={item.shortcut?.replace('Ctrl', 'Control')}
          tabIndex={-1}
          className={`context-menu-item${active === index ? ' active' : ''}${item.danger ? ' danger' : ''}${open?.index === index ? ' expanded' : ''}`}
          onPointerEnter={() => {
            if (item.disabled) return;
            setActive(index); clearTimeout(timer.current);
            timer.current = window.setTimeout(() => item.submenu ? openSubmenu(index, false) : setOpen(undefined), SUBMENU_DELAY);
          }}
          onClick={() => { if (!item.disabled) select(index); }}
        >
          <span className="context-menu-icon">{item.checked ? <Check size={14}/> : Icon && <Icon size={14}/>}</span>
          <span className="context-menu-label">{item.label}</span>
          {item.shortcut && <kbd aria-hidden="true">{item.shortcut}</kbd>}
          {item.submenu && <ChevronRight className="context-menu-chevron" size={14}/>}
        </button>;
      })}
    </div>
    {submenu && submenu.kind !== 'separator' && submenu.kind !== 'header' && submenu.submenu && <MenuPanel
      key={open.index}
      items={submenu.submenu}
      point={open.point}
      flipX={open.flipX}
      label={submenu.label}
      depth={depth + 1}
      autoFocus={open.focus}
      onClose={onClose}
      onBack={() => { setOpen(undefined); panel.current?.focus({ preventScroll: true }); }}
      onEnter={() => { clearTimeout(timer.current); setActive(open.index); }}
    />}
  </>;
}
