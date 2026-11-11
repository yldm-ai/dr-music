export interface VirtualRange {
  start: number;
  end: number;
  totalHeight: number;
}

export function virtualRange(
  itemCount: number,
  rowHeight: number,
  scrollTop: number,
  viewportHeight: number,
  overscan = 4,
): VirtualRange {
  const count = Math.max(0, Math.floor(itemCount));
  const row = Math.max(1, rowHeight);
  const viewport = Math.max(0, viewportHeight);
  const safeOverscan = Math.max(0, Math.floor(overscan));
  const maxScroll = Math.max(0, count * row - viewport);
  const top = Math.min(Math.max(0, scrollTop), maxScroll);
  const visibleStart = Math.floor(top / row);
  const visibleEnd = Math.ceil((top + Math.max(viewport, row)) / row);

  return {
    start: Math.max(0, visibleStart - safeOverscan),
    end: Math.min(count, visibleEnd + safeOverscan),
    totalHeight: count * row,
  };
}
