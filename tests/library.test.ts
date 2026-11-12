import { test } from 'node:test';
import assert from 'node:assert/strict';
import { withoutFolder, withMetadata, musicFolders } from '../src/lib/library';
import type { Library, Track } from '../src/types';

const track = (id: string, path: string): Track => ({ id, path, title: id, artist: '', album: '', duration: 0, format: 'WAV', addedAt: 0, metadataPending: true, metadataRevision: 1 });
const library = (): Library => ({
  version: 1, folders: ['C:\\Music', 'C:\\Music\\Nested', 'C:\\Music-other'],
  tracks: [track('a', 'C:\\Music\\a.wav'), track('b', 'C:\\Music\\Nested\\b.wav'), track('c', 'C:\\Music-other\\c.wav'), track('d', 'D:\\single.wav')],
  favorites: ['a', 'b', 'c', 'd'], playlists: [{ id: 'mix', name: 'Mix', trackIds: ['b', 'c', 'a', 'd'] }],
  settings: { volume: .7, repeat: 'off', shuffle: false, lyricOffset: 0, theme: 'dark', titleBar: 'direct2d' },
});

test('removal clears descendants and references while preserving siblings and standalone tracks', () => {
  const before = library();
  const next = withoutFolder(before, 'c:/MUSIC/');
  assert.deepEqual(next.tracks.map(t => t.id), ['c', 'd']);
  assert.deepEqual(next.folders, ['C:\\Music-other']);
  assert.deepEqual(next.favorites, ['c', 'd']);
  assert.deepEqual(next.playlists[0].trackIds, ['c', 'd']);
  assert.equal(before.tracks.length, 4);
  assert.equal(next.settings, before.settings);
  assert.equal(withoutFolder(library(), 'C:\\').tracks.length, 1);
});

test('metadata updates preserve user edits and cannot resurrect removed or newer tracks', () => {
  const before = library();
  const update = { ...before.tracks[0], title: 'Tagged title', metadataPending: false };
  const enriched = withMetadata(before, [update]);
  assert.equal(enriched.tracks[0].title, 'Tagged title');
  assert.equal(enriched.favorites, before.favorites);
  assert.equal(enriched.settings, before.settings);
  assert.equal(withMetadata(withoutFolder(before, 'C:\\Music'), [update]).tracks.length, 2);
  before.tracks[0].metadataRevision = 2;
  assert.equal(withMetadata(before, [update]).tracks[0].title, 'a');
});

test('music folders list directories that directly hold tracks inside added folders, ordered and disambiguated', () => {
  const base = library();
  const data: Library = { ...base, folders: ['C:\\Music', 'E:\\Other'], tracks: [
    track('a10', 'C:\\Music\\Album\\10 Ten.wav'), track('a2', 'C:\\Music\\Album\\2 Two.wav'), track('root', 'C:\\Music\\root.wav'),
    track('cd', 'C:\\Music\\Live\\Disc 1\\x.wav'), track('cd2', 'C:\\Music\\Studio\\Disc 1\\y.wav'),
    track('other', 'E:\\Other\\z.wav'), track('loose', 'D:\\single.wav'),
  ] };
  const folders = musicFolders(data);
  assert.deepEqual(folders.map(f => f.path), ['C:\\Music', 'C:\\Music\\Album', 'C:\\Music\\Live\\Disc 1', 'C:\\Music\\Studio\\Disc 1', 'E:\\Other']);
  assert.deepEqual(folders.map(f => f.name), ['Music', 'Album', 'Live / Disc 1', 'Studio / Disc 1', 'Other']);
  assert.deepEqual(folders[1].trackIds, ['a2', 'a10']);
  assert.ok(folders.every(f => !f.trackIds.includes('loose')));
  assert.equal(folders[4].root, 'E:\\Other');
});