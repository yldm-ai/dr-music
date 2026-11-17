import { test } from 'node:test';
import assert from 'node:assert/strict';
import { asSort, fileTitle, musicFolders, sortTracks, withDisplayTitles, withMetadata, withoutFolder } from '../src/lib/library';
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
test('songs imported together keep a stable order, so the next song follows the list', () => {
  const batch = [track('z', 'C:\\Music\\03 c.wav'), track('y', 'C:\\Music\\01 a.wav'), track('x', 'C:\\Music\\02 b.wav')].map(t => ({ ...t, addedAt: 5 }));
  const older = { ...track('w', 'C:\\Music\\00.wav'), addedAt: 1 };
  assert.deepEqual(sortTracks([...batch, older], 'added').map(t => t.id), ['y', 'x', 'z', 'w']);
  assert.deepEqual(sortTracks([...batch].reverse(), 'added').map(t => t.id), ['y', 'x', 'z']); // independent of the stored order
  assert.deepEqual(sortTracks(batch, 'added', true).map(t => t.id), ['z', 'y', 'x']); // a playlist keeps its own order
});

test('修改日期 puts the newest files first; other sorts and unknown values', () => {
  const songs = [{ ...track('old', 'C:\\a.wav'), modifiedAt: 100 }, { ...track('new', 'C:\\b.wav'), modifiedAt: 300 }, { ...track('mid', 'C:\\c.wav'), modifiedAt: 200 }, track('none', 'C:\\d.wav')];
  assert.deepEqual(sortTracks(songs, 'modified').map(t => t.id), ['new', 'mid', 'old', 'none']);
  assert.deepEqual(sortTracks(songs, 'file').map(t => t.id), ['old', 'new', 'mid', 'none']);
  const disc = [3, 1, 2].map(n => ({ ...track(`t${n}`, 'C:\\disc.flac'), album: 'CD', trackNumber: n, cue: { start: n * 10, end: 0, sheet: 'C:\\disc.cue' } }));
  assert.deepEqual(sortTracks(disc, 'added').map(t => t.id), ['t1', 't2', 't3']); // CUE tracks of one file follow the sheet
  assert.deepEqual(sortTracks(disc, 'album').map(t => t.id), ['t1', 't2', 't3']);
  assert.equal(asSort('nope'), 'added');
});

test('歌曲名称 can show file names, and switching back restores the tag titles', () => {
  const data = library();
  data.tracks[0] = { ...data.tracks[0], title: 'Tag Title', path: 'C:\\Music\\01 - File Name.flac' };
  data.tracks[1] = { ...data.tracks[1], title: 'Disc Song', path: 'C:\\Music\\disc.flac', cue: { start: 0, end: 10, sheet: 'C:\\Music\\disc.cue' } };
  assert.equal(fileTitle('C:\\Music\\01 - File Name.flac'), '01 - File Name');
  const files = withDisplayTitles({ ...data, settings: { ...data.settings, titleSource: 'file' } });
  assert.equal(files.tracks[0].title, '01 - File Name');
  assert.equal(files.tracks[1].title, 'Disc Song'); // a CUE track's file is the whole disc
  const tags = withDisplayTitles({ ...files, settings: { ...files.settings, titleSource: 'tag' } });
  assert.equal(tags.tracks[0].title, 'Tag Title');
  assert.equal(tags.tracks[0].tagTitle, undefined);
  assert.equal(withDisplayTitles(data), data); // nothing to do: same object
});