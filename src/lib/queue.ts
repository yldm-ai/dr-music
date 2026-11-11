export function nextTrack(queue: string[], current: string | undefined, direction: 1 | -1, repeat: 'off' | 'all' | 'one', shuffle: boolean, ended = false, random = Math.random): string | undefined {
  if (!queue.length) return undefined;
  if (ended && repeat === 'one' && current && queue.includes(current)) return current;
  if (shuffle && queue.length > 1) {
    const candidates = queue.filter(id => id !== current);
    return candidates[Math.min(candidates.length - 1, Math.floor(random() * candidates.length))];
  }
  const index = queue.indexOf(current || '');
  if (index < 0) return queue[0];
  const next = index + direction;
  if (next >= 0 && next < queue.length) return queue[next];
  if (repeat === 'all' || !ended) return queue[(next + queue.length) % queue.length];
  return undefined;
}
export const formatTime = (seconds: number) => {
  if (!Number.isFinite(seconds) || seconds < 0) return '0:00';
  return `${Math.floor(seconds / 60)}:${Math.floor(seconds % 60).toString().padStart(2, '0')}`;
};
