// Shared web/caption hover tips, following ../dr-dict/web/tooltip.ts and
// ../dr-reader/web/tooltip.ts. Native title attributes are adopted as well so
// third-party or dynamically added elements cannot show a second browser tip.
const SHOW_DELAY = 500;
type Anchor = { left: number; top: number; width: number; height: number };

export class Tooltips {
  private readonly tip = document.createElement('div');
  private readonly observer: MutationObserver;
  private readonly events = new AbortController();
  private target: Element | null = null;
  private timer?: number;
  private frame?: number;

  constructor() {
    this.tip.className = 'ui-tooltip';
    this.tip.id = 'dr-hover-tip';
    this.tip.setAttribute('role', 'tooltip');
    this.tip.popover = 'manual';
    document.body.append(this.tip);
    this.adopt(document.body);
    this.observer = new MutationObserver(records => {
      for (const record of records) {
        if (record.type === 'attributes') this.adopt(record.target as Element);
        else for (const node of record.addedNodes) if (node instanceof Element) this.adopt(node);
      }
      if (this.target && !this.target.isConnected) this.hide();
      else if (this.target && this.tip.matches(':popover-open')) {
        const text = this.target.getAttribute('data-tooltip');
        if (text) this.place(this.target.getBoundingClientRect(), text);
        else this.hide();
      }
    });
    this.observer.observe(document.body, { subtree: true, childList: true, attributes: true, attributeFilter: ['title', 'data-tooltip'] });
    const options = { capture: true, signal: this.events.signal };
    document.addEventListener('pointerover', event => this.over(event.target), options);
    document.addEventListener('pointerout', event => {
      if (this.target && (!(event.relatedTarget instanceof Node) || !this.target.contains(event.relatedTarget))) this.hide();
    }, options);
    document.addEventListener('focusin', event => this.over(event.target), options);
    document.addEventListener('focusout', () => this.hide(), options);
    for (const event of ['pointerdown', 'keydown', 'wheel', 'scroll']) document.addEventListener(event, () => this.hide(), { ...options, passive: true });
    window.addEventListener('blur', () => this.hide(), { signal: this.events.signal });
    window.addEventListener('resize', () => this.hide(), { signal: this.events.signal });
  }

  private adopt(scope: Element) {
    const elements = [scope, ...scope.querySelectorAll('[title]')];
    for (const element of elements) {
      if (element instanceof HTMLIFrameElement || !element.hasAttribute('title')) continue;
      const text = element.getAttribute('title') || '';
      element.removeAttribute('title');
      if (text) {
        element.setAttribute('data-tooltip', text);
        if (!element.hasAttribute('aria-label') && !element.textContent?.trim()) element.setAttribute('aria-label', text);
      } else element.removeAttribute('data-tooltip');
    }
  }

  private over(target: EventTarget | null) {
    const owner = target instanceof Element ? target.closest('[data-tooltip]') : null;
    if (owner === this.target) return;
    this.hide();
    if (!owner || owner.matches(':disabled') || owner.getAttribute('aria-expanded') === 'true') return;
    this.target = owner;
    this.timer = window.setTimeout(() => {
      if (this.target === owner && owner.isConnected) this.show(owner.getBoundingClientRect(), owner.getAttribute('data-tooltip') || '', owner);
    }, SHOW_DELAY);
  }

  show(anchor: Anchor, text: string, owner?: Element) {
    if (!text) { this.hide(); return; }
    clearTimeout(this.timer); cancelAnimationFrame(this.frame || 0);
    this.tip.classList.remove('visible');
    if (this.tip.matches(':popover-open')) this.tip.hidePopover();
    const dark = owner ? !!owner.closest('.now-playing') : !!document.querySelector('.now-playing');
    this.tip.dataset.theme = dark ? 'dark' : document.documentElement.dataset.theme || 'dark';
    this.tip.showPopover();
    this.place(anchor, text);
    if (owner) {
      const ids = new Set((owner.getAttribute('aria-describedby') || '').split(/\s+/).filter(Boolean));
      ids.add(this.tip.id); owner.setAttribute('aria-describedby', [...ids].join(' '));
    }
    this.frame = requestAnimationFrame(() => { if (this.tip.matches(':popover-open')) this.tip.classList.add('visible'); });
  }

  private place(anchor: Anchor, text: string) {
    if (this.tip.textContent !== text) this.tip.textContent = text;
    const { width, height } = this.tip.getBoundingClientRect();
    const margin = 4, gap = 2;
    const above = anchor.top + anchor.height + gap + height > innerHeight - margin && anchor.top - height - gap >= margin;
    this.tip.dataset.side = above ? 'above' : 'below';
    this.tip.style.left = `${Math.round(Math.max(margin, Math.min(anchor.left + (anchor.width - width) / 2, innerWidth - width - margin)))}px`;
    this.tip.style.top = `${Math.round(Math.max(margin, Math.min(above ? anchor.top - height - gap : anchor.top + anchor.height + gap, innerHeight - height - margin)))}px`;
  }

  hide() {
    clearTimeout(this.timer); cancelAnimationFrame(this.frame || 0);
    if (this.target) {
      const ids = (this.target.getAttribute('aria-describedby') || '').split(/\s+/).filter(id => id && id !== this.tip.id);
      if (ids.length) this.target.setAttribute('aria-describedby', ids.join(' '));
      else this.target.removeAttribute('aria-describedby');
    }
    this.target = null;
    this.tip.classList.remove('visible');
    if (this.tip.matches(':popover-open')) this.tip.hidePopover();
  }

  dispose() { this.hide(); this.observer.disconnect(); this.events.abort(); this.tip.remove(); }
}
