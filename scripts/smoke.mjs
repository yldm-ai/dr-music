import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { mkdir, writeFile, rm, readFile } from 'node:fs/promises';
import { createServer } from 'node:net';
import { once } from 'node:events';
import path from 'node:path';
import assert from 'node:assert/strict';
const executable = path.resolve(process.env.DR_MUSIC_SMOKE_EXE || 'build/bin/Release/dr-music.exe');
const root = path.resolve('.test-data/native-smoke');
await rm(root, { recursive: true, force: true });
await mkdir(path.join(root, 'songs', 'nested'), { recursive: true });
const rate = 22050, bytes = rate * 16 * 2;
const wav = Buffer.alloc(44 + bytes);
wav.write('RIFF', 0); wav.writeUInt32LE(36 + bytes, 4); wav.write('WAVEfmt ', 8); wav.writeUInt32LE(16, 16);
wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22); wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 2, 28); wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write('data', 36); wav.writeUInt32LE(bytes, 40);
for (let i = 0; i < bytes / 2; i++) wav.writeInt16LE(Math.round(Math.sin(i * 2 * Math.PI * 220 / rate) * 100), 44 + i * 2);
for (const name of ['午后散步', '夜色温柔', '写给明天']) await writeFile(path.join(root, 'songs', `${name}.wav`), wav);
function coverBitmap(rgb) {
  const image = Buffer.alloc(70);
  image.write('BM'); image.writeUInt32LE(image.length, 2); image.writeUInt32LE(54, 10);
  image.writeUInt32LE(40, 14); image.writeInt32LE(2, 18); image.writeInt32LE(2, 22);
  image.writeUInt16LE(1, 26); image.writeUInt16LE(24, 28); image.writeUInt32LE(16, 34);
  for (const offset of [54, 57, 62, 65]) image.writeUIntLE(rgb, offset, 3);
  return image;
}
// Deliberately mismatched extension: native decoding must inspect the content.
await writeFile(path.join(root, 'songs', 'folder.jpg'), coverBitmap(0x123456));
// Online lyric providers served from local files through --online-lyrics-root (<host>/<path>):
// the app never goes online in tests. Providers without a fixture answer 404 ("not found").
const lyricsRoot = path.join(root, 'lyrics-fixture');
const amll = path.join(lyricsRoot, 'raw.githubusercontent.com', 'amll-dev', 'amll-ttml-db', 'main');
await mkdir(path.join(amll, 'metadata'), { recursive: true }); await mkdir(path.join(amll, 'raw-lyrics'), { recursive: true });
const amllEntry = (title, file) => JSON.stringify({ metadata: [['album', ['在线专辑']], ['artists', ['在线歌手']], ['musicName', [title]], ['ncmMusicId', ['1']]], rawLyricFile: file });
await writeFile(path.join(amll, 'metadata', 'raw-lyrics-index.jsonl'), [amllEntry('写给明天', '1700000000000-1-aaaa.ttml'), amllEntry('午后散步', '1700000000000-1-bbbb.ttml')].join('\n') + '\n');
const ttml = text => `<tt xmlns="http://www.w3.org/ns/ttml"><body><div><p begin="00:00.500" end="00:06.000"><span begin="00:00.500" end="00:06.000">${text}</span></p><p begin="00:06.000" end="00:12.000"><span begin="00:06.000" end="00:12.000">${text}第二句</span></p></div></body></tt>`;
await writeFile(path.join(amll, 'raw-lyrics', '1700000000000-1-aaaa.ttml'), ttml('在线歌词第一句'));
await writeFile(path.join(amll, 'raw-lyrics', '1700000000000-1-bbbb.ttml'), ttml('午后在线歌词'));
await mkdir(path.join(lyricsRoot, 'lrclib.net', 'api'), { recursive: true });
await writeFile(path.join(lyricsRoot, 'lrclib.net', 'api', 'search'), JSON.stringify([{ id: 9, trackName: '午后散步', artistName: '在线歌手', albumName: '', duration: 16, instrumental: false, syncedLyrics: '[00:00.50]LRCLIB 午后\n[00:06.00]LRCLIB 第二句', plainLyrics: '' }]));
// lrc.red answers in its own TTML dialect: lrc: attributes and transliterations inside <head>.
await mkdir(path.join(lyricsRoot, 'lrc.red', 's'), { recursive: true });
await writeFile(path.join(lyricsRoot, 'lrc.red', 'search.json'), JSON.stringify({ hits: [{ isrc: 'CNA000000001', title: '午后散步', artist: '在线歌手', album: '', duration: 16 }], next: null }));
await writeFile(path.join(lyricsRoot, 'lrc.red', 's', 'CNA000000001.ttml'), '<tt xmlns="http://www.w3.org/ns/ttml" xmlns:lrc="http://lrc.red/lyric-ttml-internal" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" lrc:timing="Word" xml:lang="zh-Hans"><head><metadata><ttm:agent type="person" xml:id="v1"/><sourceMetadata xmlns="http://lrc.red/lyric-ttml-internal"><transliterations><transliteration xml:lang="zh-Latn-pinyin"><text for="L1"><span xmlns="http://www.w3.org/ns/ttml" begin="0.5" end="6">wu hou</span></text></transliteration></transliterations></sourceMetadata></metadata></head><body dur="0:16.000"><div begin="0.5" end="12" lrc:songPart="Verse"><p begin="0.5" end="6" lrc:key="L1" ttm:agent="v1"><span begin="0.5" end="3">lrc.red </span><span begin="3" end="6">午后</span></p><p begin="6" end="12" lrc:key="L2" ttm:agent="v1"><span begin="6" end="12">lrc.red 第二句</span></p></div></body></tt>');
await writeFile(path.join(root, 'songs', '午后散步.lrc'), '[00:00.00]测试歌词第一句\n[00:04.00]测试歌词第二句\n[00:08.00]测试歌词第三句\n[00:12.00]最后一句');
await writeFile(path.join(root, 'songs', '夜色温柔.ttml'), '<tt xmlns="http://www.w3.org/ns/ttml"><body><div><p begin="00:01.000" end="00:03.000"><span begin="00:01.000" end="00:02.000">你好</span><span begin="00:02.000" end="00:03.000">世界</span></p></div></body></tt>');
const server = createServer(); server.listen(0, '127.0.0.1'); await once(server, 'listening'); const port = server.address().port; await new Promise(resolve => server.close(resolve));
let child, browser, page;
const errors = [];
try {
  child = spawn(executable, ['--data-dir', path.join(root, 'data'), '--import', path.join(root, 'songs'), '--import', path.join(root, 'songs', 'nested'), '--online-lyrics-root', lyricsRoot, '--debug-port', String(port)], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: false });
  child.on('error', error => errors.push(error.message));
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    try { if ((await fetch(`http://127.0.0.1:${port}/json/version`)).ok) break; } catch { /* host initializing */ }
    if (child.exitCode !== null) throw new Error(`Native host exited with code ${child.exitCode}`);
    await new Promise(resolve => setTimeout(resolve, 150));
  }
  browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
  const context = browser.contexts()[0]; page = context.pages()[0] || await context.waitForEvent('page');
  page.setDefaultTimeout(20000);
  page.on('pageerror', error => errors.push(error.message));
  await page.waitForURL('https://app.dr-music.local/index.html');
  await page.getByRole('button', { name: '播放 午后散步', exact: true }).waitFor();
  await page.waitForFunction(async () => (await window.music.getLibrary()).tracks.every(track => !track.metadataPending));
  const initialArtwork = (await page.evaluate(() => window.music.getLibrary())).tracks;
  assert.equal(initialArtwork.every(track => track.cover === 'image/png' && track.coverSource === 'folder.jpg'), true);
  await page.waitForFunction(() => Array.from(document.querySelectorAll('.track-row img')).filter(img => img.complete && img.naturalWidth === 2).length >= 3);
  assert.equal(await page.title(), 'DR Music');
  assert.equal(await page.evaluate(() => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim()), '#ec4141');
  assert.equal((await page.evaluate(() => window.music.getLibrary())).tracks.length, 3);
  const searchField = page.locator('.search');
  const searchBefore = await searchField.evaluate(element => ({ border: getComputedStyle(element).borderColor, height: element.getBoundingClientRect().height }));
  await page.getByRole('textbox', { name: '搜索音乐' }).focus();
  await page.waitForTimeout(420);
  const searchFocused = await searchField.evaluate(element => ({ border: getComputedStyle(element).borderColor, shadow: getComputedStyle(element).boxShadow, height: element.getBoundingClientRect().height }));
  assert.notEqual(searchFocused.border, searchBefore.border);
  assert.notEqual(searchFocused.shadow, 'none');
  assert.equal(searchFocused.height, searchBefore.height);
  await page.getByRole('textbox', { name: '搜索音乐' }).blur();
  await page.screenshot({ path: path.join(root, 'library.png') });
  // 简繁通搜: traditional input finds simplified titles only while the OpenCC-backed setting is on.
  assert.equal((await page.evaluate(() => window.music.chineseStatus())).available, true);
  assert.deepEqual(await page.evaluate(() => window.music.simplify(['寫給明天', 'Hello'])), ['写给明天', 'Hello']);
  assert.ok((await page.evaluate(() => window.music.chineseQuery('後來'))).includes('后来'));
  const search = page.getByRole('textbox', { name: '搜索音乐' });
  const visibleRows = () => page.locator('.track-table .track-row:not(.table-head)').count();
  await search.fill('寫給');
  await page.getByRole('heading', { name: '还没有找到这首音乐' }).waitFor();
  await search.fill('');
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  const chineseSwitch = page.getByRole('switch', { name: '简繁通搜', exact: true });
  assert.equal(await chineseSwitch.getAttribute('aria-checked'), 'false');
  await chineseSwitch.click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.chineseSearch === true);
  assert.equal(await chineseSwitch.getAttribute('aria-checked'), 'true');
  // The selected tab opens into the panel: nothing of the panel's top border shows beneath it.
  const tabSeam = await page.evaluate(() => {
    const tab = document.querySelector('.settings-tabs [aria-selected=true]').getBoundingClientRect();
    const body = document.querySelector('.settings-body').getBoundingClientRect();
    const hit = document.elementFromPoint(tab.left + tab.width / 2, body.top + .5);
    return { coveredByTab: !!hit?.closest('[role=tab]') };
  });
  assert.deepEqual(tabSeam, { coveredByTab: true });
  await page.screenshot({ path: path.join(root, 'settings-chinese-search.png') });
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
  await search.fill('寫給');
  await page.waitForFunction(() => [...document.querySelectorAll('.track-table .track-row:not(.table-head) strong')].map(e => e.textContent).join() === '写给明天');
  await search.fill('午後');
  await page.waitForFunction(() => document.querySelector('.track-table .track-row:not(.table-head) strong')?.textContent === '午后散步');
  assert.equal(await visibleRows(), 1);
  await search.fill('');
  await search.blur();
  // The actual popover must show on hover and dismiss on keyboard input.
  await page.getByRole('button', { name: '新建歌单', exact: true }).hover();
  await page.getByRole('tooltip').filter({ hasText: '新建歌单' }).waitFor();
  await page.waitForFunction(() => getComputedStyle(document.getElementById('dr-hover-tip')).opacity === '1');
  await page.screenshot({ path: path.join(root, 'tooltip.png') });
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => !document.getElementById('dr-hover-tip').matches(':popover-open'));
  await page.mouse.move(4, 4);
  // Third-party/dynamic title attributes use the same drawn tip, including
  // changes while hovered and removal of a hovered element.
  await page.evaluate(() => {
    const button = document.createElement('button'); button.id = 'tooltip-probe'; button.title = '动态提示'; button.textContent = '提示检查';
    button.style.cssText = 'position:fixed;right:8px;bottom:140px;z-index:80;padding:8px;background:#333'; document.body.append(button);
  });
  await page.locator('#tooltip-probe').hover();
  await page.getByRole('tooltip').filter({ hasText: '动态提示' }).waitFor();
  await page.locator('#tooltip-probe').evaluate(button => { button.title = '更新后的提示'; });
  await page.getByRole('tooltip').filter({ hasText: '更新后的提示' }).waitFor();
  assert.equal(await page.locator('#tooltip-probe').getAttribute('title'), null);
  await page.locator('#tooltip-probe').evaluate(button => button.remove());
  await page.waitForFunction(() => !document.getElementById('dr-hover-tip').matches(':popover-open'));
  await page.getByRole('button', { name: '喜欢 午后散步', exact: true }).click();
  await page.getByRole('button', { name: /我喜欢的/ }).click();
  await page.waitForFunction(() => document.querySelectorAll('.track-table .track-row:not(.table-head)').length === 1);
  await page.getByRole('button', { name: '新建歌单', exact: true }).click();
  assert.equal(await page.locator('form.small-modal').evaluate(form => form.noValidate), true);
  const playlistName = page.getByRole('textbox', { name: '歌单名称' });
  await playlistName.press('Enter');
  assert.equal(await page.getByRole('dialog', { name: '新建歌单' }).isVisible(), true);
  await playlistName.fill('原生测试歌单');
  await playlistName.focus();
  const nameFocus = await playlistName.evaluate(input => ({ outline: getComputedStyle(input).outlineStyle, ring: getComputedStyle(input).boxShadow }));
  assert.equal(nameFocus.outline, 'none', 'the playlist name field must not draw the global focus outline');
  assert.notEqual(nameFocus.ring, 'none');
  await page.waitForTimeout(200); await page.locator('form.small-modal').screenshot({ path: path.join(root, 'playlist-dialog-focus.png') });
  assert.equal(await playlistName.evaluate(input => getComputedStyle(input).fontFamily.includes('Noto Sans SC')), true);
  await page.getByRole('button', { name: '保存歌单', exact: true }).click();
  // Song menu: compact top-layer menu, playlists live in a submenu.
  await page.getByRole('button', { name: '午后散步 更多操作', exact: true }).click();
  const songMenu = page.getByRole('menu', { name: '午后散步 歌曲菜单', exact: true });
  await songMenu.getByRole('menuitem', { name: '添加到歌单', exact: true }).hover();
  await page.getByRole('menu', { name: '添加到歌单', exact: true }).getByRole('menuitem', { name: '原生测试歌单', exact: true }).click();
  await page.getByRole('menu').first().waitFor({ state: 'detached' });
  await page.getByRole('button', { name: /原生测试歌单 1/ }).click();
  await page.locator('.playlist-hero .playlist-cover').waitFor();
  const playlistRow = page.locator('.track-row:not(.table-head)').first();
  await playlistRow.hover();
  await page.waitForTimeout(260);
  assert.equal(await playlistRow.locator('.row-number-content>span').isVisible(), true);
  assert.equal(await playlistRow.locator('.track-cover-overlay').evaluate(element => getComputedStyle(element).opacity), '1');
  await page.screenshot({ path: path.join(root, 'playlist.png') });
  await playlistRow.click({ button: 'right' });
  const rowMenu = page.getByRole('menu', { name: '午后散步 歌曲菜单', exact: true });
  await rowMenu.getByRole('menuitem', { name: '复制歌名', exact: true }).waitFor();
  await rowMenu.getByRole('menuitem', { name: '复制歌名和歌手', exact: true }).waitFor();
  // Keyboard: arrows move, ArrowRight opens the playlist submenu, ArrowLeft returns, Escape closes.
  await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowDown');
  await page.keyboard.press('ArrowRight');
  await page.getByRole('menu', { name: '添加到歌单', exact: true }).waitFor();
  await page.waitForTimeout(160); await page.screenshot({ path: path.join(root, 'context-menu.png') });
  await page.keyboard.press('ArrowLeft');
  await page.getByRole('menu', { name: '添加到歌单', exact: true }).waitFor({ state: 'detached' });
  await page.keyboard.press('Escape');
  await rowMenu.waitFor({ state: 'detached' });
  assert.equal(await page.locator('.now-playing[data-open="true"]').count(), 0, 'Escape in the menu must not leak to global shortcuts');

  // Folders: directories holding music inside the added folders appear in the sidebar as playlist-like pages.
  // The folder section starts collapsed and toggles from its heading.
  const folderToggle = page.locator('.folder-label .section-toggle');
  assert.equal(await folderToggle.getAttribute('aria-expanded'), 'false');
  assert.equal(await page.locator('nav[aria-label="文件夹"]').count(), 0);
  await folderToggle.click();
  await page.locator('nav[aria-label="文件夹"]').waitFor();
  assert.equal(await folderToggle.getAttribute('aria-expanded'), 'true');
  await folderToggle.click();
  await page.locator('nav[aria-label="文件夹"]').waitFor({ state: 'detached' });
  await folderToggle.click();
  const folderLinks = page.locator('nav[aria-label="文件夹"] .nav-item');
  const expectedFolders = await page.evaluate(async () => {
    const library = await window.music.getLibrary();
    const counts = new Map();
    for (const track of library.tracks) { const dir = track.path.slice(0, track.path.lastIndexOf('\\')); counts.set(dir, (counts.get(dir) || 0) + 1); }
    return [...counts].map(([dir, count]) => ({ name: dir.split('\\').pop(), count }));
  });
  assert.equal(await folderLinks.count(), expectedFolders.length);
  const songsFolderInfo = expectedFolders.find(folder => folder.name === 'songs');
  await folderLinks.filter({ hasText: 'songs' }).click();
  await page.locator('.playlist-hero .eyebrow', { hasText: '文件夹' }).waitFor();
  await page.waitForFunction(count => document.querySelectorAll('.track-table .track-row:not(.table-head)').length === count, songsFolderInfo.count);
  assert.equal(await page.locator('.playlist-hero h1').innerText(), 'songs');
  assert.match(await page.locator('.hero-path').innerText(), /songs$/);
  assert.equal(await folderLinks.filter({ hasText: 'songs' }).getAttribute('class').then(c => c.includes('selected')), true);
  await page.screenshot({ path: path.join(root, 'folder-page.png') });

  // Search also matches folder names and offers them as shortcuts to the folder page.
  await page.getByRole('button', { name: '音乐库 3', exact: true }).click();
  await page.getByRole('textbox', { name: '搜索音乐' }).fill('song');
  const folderResults = page.getByRole('group', { name: '匹配的文件夹' });
  await folderResults.waitFor();
  assert.equal(await folderResults.getByRole('button').count(), 1);
  await page.waitForTimeout(700);
  const folderOverlap = await page.evaluate(() => { const card = document.querySelector('.folder-results').getBoundingClientRect(); const head = document.querySelector('.track-table .table-head').getBoundingClientRect(); return head.top < card.bottom; });
  assert.equal(folderOverlap, false, 'folder results must not overlap the song table');
  await page.screenshot({ path: path.join(root, 'folder-search.png') });
  await folderResults.getByRole('button', { name: '打开文件夹 songs', exact: true }).click();
  await page.locator('.playlist-hero .eyebrow', { hasText: '文件夹' }).waitFor();
  assert.equal(await page.getByRole('textbox', { name: '搜索音乐' }).inputValue(), '');
  await page.getByRole('textbox', { name: '搜索音乐' }).fill('不存在的目录名');
  await page.getByRole('group', { name: '匹配的文件夹' }).waitFor({ state: 'detached' });
  await page.getByRole('textbox', { name: '搜索音乐' }).fill('');
  // Multi-select: click, Ctrl+click and Shift+click, then batch-add through the selection bar.
  await page.getByRole('button', { name: '音乐库 3', exact: true }).click();
  const libraryRows = page.locator('.track-table .track-row:not(.table-head)');
  await libraryRows.nth(0).locator('.album-cell').click();
  assert.equal(await page.locator('.selection-bar').count(), 0, 'a single selected row should not show the batch bar');
  await libraryRows.nth(2).locator('.album-cell').click({ modifiers: ['Control'] });
  await page.locator('.selection-bar', { hasText: '2 首已选' }).waitFor();
  await libraryRows.nth(1).locator('.album-cell').click({ modifiers: ['Shift'] });
  await page.locator('.selection-bar', { hasText: '2 首已选' }).waitFor();
  // The anchor stays on the Ctrl+clicked row, so Shift+click on the first row spans all three.
  await libraryRows.nth(0).locator('.album-cell').click({ modifiers: ['Shift'] });
  await page.locator('.selection-bar', { hasText: '3 首已选' }).waitFor();
  assert.equal(await page.locator('.track-row.selected').count(), 3);
  await libraryRows.nth(1).click({ button: 'right', position: { x: 600, y: 20 } });
  const batchMenu = page.getByRole('menu', { name: '已选 3 首歌曲菜单', exact: true });
  await batchMenu.waitFor();
  const menuOnTop = await batchMenu.evaluate(menu => { const box = menu.getBoundingClientRect(); const hit = document.elementFromPoint(box.x + box.width / 2, box.y + 12); return menu.contains(hit); });
  assert.equal(menuOnTop, true, 'context menu must sit on the top layer');
  await page.keyboard.press('Escape');
  await batchMenu.waitFor({ state: 'detached' });
  // Menus close on scroll, so settle the sticky bar in view before opening one from it.
  const barAdd = page.locator('.selection-bar').getByRole('button', { name: '添加到歌单', exact: true });
  await barAdd.scrollIntoViewIfNeeded(); await page.waitForTimeout(80);
  await barAdd.click();
  await page.getByRole('menu', { name: '添加到歌单', exact: true }).getByRole('menuitem', { name: '原生测试歌单', exact: true }).click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).playlists[0].trackIds.length === 3);
  await page.screenshot({ path: path.join(root, 'multi-select.png') });
  await page.keyboard.press('Escape');
  await page.locator('.selection-bar').waitFor({ state: 'detached' });

  // Batch removal from the playlist restores the single-song playlist used below.
  await page.getByRole('button', { name: /原生测试歌单 3/ }).click();
  await page.locator('.playlist-hero .playlist-cover').waitFor();
  await page.keyboard.press('Control+A');
  await page.locator('.selection-bar', { hasText: '3 首已选' }).waitFor();
  const keep = page.locator('.track-table .track-row:not(.table-head)').filter({ hasText: '午后散步' });
  await keep.locator('.album-cell').click({ modifiers: ['Control'] });
  await page.locator('.selection-bar', { hasText: '2 首已选' }).waitFor();
  await page.locator('.selection-bar').getByRole('button', { name: '移出歌单', exact: true }).click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).playlists[0].trackIds.length === 1);
  await page.locator('.track-row.selected').first().waitFor({ state: 'detached' }).catch(() => {});
  assert.equal(await page.locator('.track-row.selected').count(), 0);
  assert.equal(await playlistRow.locator('.row-wave').count(), 0);
  await page.getByRole('button', { name: '播放 午后散步', exact: true }).click();
  await page.getByRole('button', { name: '暂停', exact: true }).waitFor();
  await playlistRow.locator('.row-wave[data-playing="true"]').waitFor();
  await playlistRow.locator('.row-number-content').waitFor({ state: 'detached' });
  await playlistRow.hover();
  await page.getByRole('button', { name: '暂停 午后散步', exact: true }).waitFor();
  await page.waitForFunction(() => Number(document.querySelector('input[aria-label="播放进度"]').value) > .1);
  const nativePlaying = await page.evaluate(() => window.music.playback({ action: 'state', request: 0 }));
  assert.equal(nativePlaying.playing, true); assert.ok(nativePlaying.position > 0); assert.ok(nativePlaying.duration >= 15.9);
  assert.ok(['WASAPI', 'DirectSound', 'WinMM'].includes(nativePlaying.backend));
  await page.getByRole('button', { name: '暂停', exact: true }).click();
  const nativePaused = await page.evaluate(() => window.music.playback({ action: 'state', request: 0 }));
  assert.equal(nativePaused.playing, false);
  await page.waitForTimeout(130);
  assert.equal((await page.evaluate(() => window.music.playback({ action: 'state', request: 0 }))).position, nativePaused.position);
  assert.equal(await page.evaluate(async () => {
    try { await window.music.playback({ action: 'play', id: 'not-a-library-track', request: 1 }); return false; } catch { return true; }
  }), true);
  await page.getByRole('button', { name: '播放', exact: true }).click();
  await page.getByRole('button', { name: '暂停', exact: true }).waitFor();
  const check = await page.evaluate(async () => {
    const library = await window.music.getLibrary(); const id = library.tracks.find(t => t.title === '午后散步').id;
    const reply = await fetch(`https://media.dr-music.local/audio/${id}`, { headers: { Range: 'bytes=44-99' } });
    return { status: reply.status, size: (await reply.arrayBuffer()).byteLength, range: reply.headers.get('content-range') };
  });
  assert.equal(check.status, 206); assert.equal(check.size, 56); assert.match(check.range, /^bytes 44-99\//);
  const volumeButton = page.getByRole('button', { name: '音量静音 / 恢复', exact: true });
  const volumeSlider = page.getByRole('slider', { name: '音量', exact: true });
  const volumeNow = slider => slider.evaluate(element => Number(element.getAttribute('aria-valuenow')));
  assert.equal(await volumeSlider.count(), 0, 'volume slider should stay folded until hover');
  await volumeButton.hover();
  await volumeSlider.waitFor();
  const barGeometry = await page.evaluate(() => {
    const button = document.querySelector('.player-tools .volume-toggle').getBoundingClientRect();
    const track = document.querySelector('.player-tools .volume-track').getBoundingClientRect();
    return { buttonTop: button.top, trackBottom: track.bottom, trackWidth: track.width, trackHeight: track.height };
  });
  assert.ok(barGeometry.trackBottom <= barGeometry.buttonTop, 'player bar volume should unfold upward');
  assert.ok(barGeometry.trackHeight > barGeometry.trackWidth * 3, 'player bar volume should be vertical');
  const initialVolume = await volumeNow(volumeSlider);
  await volumeSlider.hover();
  await page.mouse.wheel(0, -100);
  const increasedVolume = Math.min(100, initialVolume + 1);
  await page.waitForFunction(value => Number(document.querySelector('.player-tools .volume-track').getAttribute('aria-valuenow')) === value, increasedVolume);
  assert.equal(await page.locator('.player-tools .volume-value').innerText(), `${increasedVolume}`);
  await page.mouse.wheel(0, 100);
  await page.waitForFunction(value => Number(document.querySelector('.player-tools .volume-track').getAttribute('aria-valuenow')) === value, initialVolume);
  const trackBox = await volumeSlider.boundingBox();
  await page.mouse.click(trackBox.x + trackBox.width / 2, trackBox.y + trackBox.height / 2);
  assert.ok(Math.abs(await volumeNow(volumeSlider) - 50) <= 2, 'clicking the middle of the vertical track should set ~50%');
  await page.waitForTimeout(80);
  await page.screenshot({ path: path.join(root, 'volume-vertical.png') });
  await volumeSlider.focus(); await page.keyboard.press('ArrowUp');
  assert.ok(Math.abs(await volumeNow(volumeSlider) - 51) <= 2);
  // Icon follows the level: muted → crossed speaker, any audible level keeps at least one wave.
  const volumeIcon = () => page.locator('.player-tools .volume-toggle svg').getAttribute('class');
  await page.keyboard.press('Home'); assert.match(await volumeIcon(), /lucide-volume-x/);
  await page.keyboard.press('ArrowUp'); assert.match(await volumeIcon(), /lucide-volume1\b/);
  await page.keyboard.press('End'); assert.match(await volumeIcon(), /lucide-volume2\b/);
  await page.keyboard.press('Home'); for (let i = 0; i < 5; i++) await page.keyboard.press('PageUp'); await page.keyboard.press('ArrowUp');
  assert.equal(await volumeNow(volumeSlider), 51);
  assert.equal(await page.locator('.volume-meter').count(), 0);
  await volumeSlider.blur();
  await page.mouse.move(1, 1);
  await volumeSlider.waitFor({ state: 'hidden' });
  await page.mouse.move(1, 1);
  const playerEntry = await page.evaluate(() => new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('player entry animation timed out')), 2000);
    const player = document.querySelector('.now-playing');
    if (!player) { reject(new Error('pre-mounted player is missing')); return; }
    const visibilityBefore = getComputedStyle(player).visibility;
    const observer = new MutationObserver(() => {
      if (player.dataset.open !== 'true') return;
      requestAnimationFrame(() => {
        clearTimeout(timeout);
        observer.disconnect();
        const transform = getComputedStyle(player).transform;
        const startedAt = performance.now();
        let previous = startedAt;
        let maxFrameGap = 0;
        const sample = (now) => {
          maxFrameGap = Math.max(maxFrameGap, now - previous);
          previous = now;
          if (now - startedAt < 520) requestAnimationFrame(sample);
          else resolve({ visibilityBefore, transform, translateY: new DOMMatrixReadOnly(transform).m42, maxFrameGap });
        };
        requestAnimationFrame(sample);
      });
    });
    observer.observe(player, { attributes: true, attributeFilter: ['data-open'] });
    document.querySelector('button[aria-label="打开歌词播放页"]').click();
  }));
  assert.equal(playerEntry.visibilityBefore, 'hidden');
  assert.notEqual(playerEntry.transform, 'none');
  assert.ok(playerEntry.translateY > 0, 'player did not enter from below');
  assert.ok(playerEntry.maxFrameGap < 80, `player entry frame gap was ${playerEntry.maxFrameGap.toFixed(1)}ms`);
  await page.waitForFunction(() => {
    const transform = getComputedStyle(document.querySelector('.now-playing')).transform;
    return transform === 'none' || Math.abs(new DOMMatrixReadOnly(transform).m42) < .5;
  });
  await page.waitForTimeout(120);
  const immersiveButton = page.getByRole('button', { name: '播放页音量静音 / 恢复', exact: true });
  const immersiveVolume = page.getByRole('slider', { name: '播放页音量', exact: true });
  const transportEdge = await page.evaluate(() => {
    const transport = document.querySelector('.now-left .transport').getBoundingClientRect();
    const button = document.querySelector('.now-left .transport .volume-toggle').getBoundingClientRect();
    const play = document.querySelector('.now-left .play-button').getBoundingClientRect();
    return { right: transport.right - button.right, centered: Math.abs((play.left + play.right) / 2 - (transport.left + transport.right) / 2) };
  });
  assert.ok(transportEdge.right < 1, 'now playing volume should sit at the right edge of the transport row');
  assert.ok(transportEdge.centered < 1, 'play button should stay centered');
  assert.equal(await page.locator('.now-header-tools .volume-popover').count(), 0);
  // A transient toast from the playlist step can cover the transport row; let it expire first.
  await page.locator('.toast').waitFor({ state: 'detached', timeout: 8000 });
  await immersiveButton.hover();
  await immersiveVolume.waitFor();
  const immersiveBox = await immersiveVolume.boundingBox();
  assert.ok(immersiveBox.width > immersiveBox.height * 3, 'now playing volume should be horizontal');
  const immersiveBefore = await volumeNow(immersiveVolume);
  await immersiveVolume.hover(); await page.mouse.wheel(0, -100);
  const immersiveAfter = Math.min(100, immersiveBefore + 1);
  await page.waitForFunction(value => Number(document.querySelector('.now-left .volume-track').getAttribute('aria-valuenow')) === value, immersiveAfter);
  assert.equal(Number(await page.locator('.player-tools .volume-track').getAttribute('aria-valuenow')), immersiveAfter);
  assert.equal(await page.locator('.now-left .volume-value').innerText(), `${immersiveAfter}`);
  await page.waitForTimeout(80);
  await page.screenshot({ path: path.join(root, 'volume-horizontal.png') });
  await immersiveVolume.focus();
  await page.keyboard.press('End'); await page.mouse.wheel(0, -100);
  assert.equal(await volumeNow(immersiveVolume), 100);
  await page.keyboard.press('Home'); await page.mouse.wheel(0, 100);
  assert.equal(await volumeNow(immersiveVolume), 0);
  await immersiveButton.click();
  await page.waitForFunction(() => Number(document.querySelector('.now-left .volume-track').getAttribute('aria-valuenow')) > 0);
  await immersiveVolume.focus(); await page.keyboard.press('Escape');
  assert.equal(await immersiveVolume.evaluate(element => element === document.activeElement), false);
  assert.equal(await page.locator('.now-playing').getAttribute('data-open'), 'true', 'Escape on the volume slider should not close the player');
  await page.mouse.move(1, 1);
  await immersiveVolume.waitFor({ state: 'hidden' });
  assert.equal(await page.locator('.lyric-info').count(), 0);
  const positionClock = page.locator('.now-playing .progress-control>span').first();
  const clockBefore = await positionClock.evaluate(element => {
    element.dataset.stableClock = 'true';
    return { text: element.textContent, opacity: getComputedStyle(element).opacity, transform: getComputedStyle(element).transform };
  });
  await page.waitForTimeout(1100);
  const clockAfter = await positionClock.evaluate(element => ({
    text: element.textContent,
    stable: element.dataset.stableClock,
    opacity: getComputedStyle(element).opacity,
    transform: getComputedStyle(element).transform,
  }));
  assert.notEqual(clockAfter.text, clockBefore.text);
  assert.equal(clockAfter.stable, 'true');
  assert.equal(clockAfter.opacity, clockBefore.opacity);
  assert.equal(clockAfter.transform, clockBefore.transform);
  const progressSlider = page.locator('.now-playing input[aria-label="播放进度"]');
  const progressBefore = await progressSlider.evaluate(element => ({
    box: element.getBoundingClientRect().toJSON(),
    transform: getComputedStyle(element).transform,
  }));
  await progressSlider.hover();
  await page.waitForTimeout(240);
  const progressHover = await progressSlider.evaluate(element => {
    return {
      box: element.getBoundingClientRect().toJSON(),
      transform: getComputedStyle(element).transform,
    };
  });
  for (const key of ['x', 'y', 'width', 'height']) assert.ok(Math.abs(progressHover.box[key] - progressBefore.box[key]) < .5, `progress slider ${key} moved`);
  assert.equal(progressHover.transform, progressBefore.transform);
  // While hovered during playback the thumb keeps one crisp bar shape and steps every 200 ms,
  // always on whole device pixels (sub-pixel drift made the bar look like it kept swelling).
  const thumbSamples = await page.evaluate(() => new Promise(resolve => {
    const thumb = document.querySelector('.now-playing .progress-thumb'), bar = thumb.firstElementChild;
    const shapes = new Set(), positions = []; const start = performance.now();
    const sample = now => {
      const style = getComputedStyle(bar); shapes.add(style.width + 'x' + style.height);
      const x = thumb.getBoundingClientRect().x; if (positions[positions.length - 1] !== x) positions.push(x);
      if (now - start < 1500) requestAnimationFrame(sample);
      else resolve({ shapes: [...shapes], steps: positions.length, snapped: positions.every(value => Math.abs(value * devicePixelRatio - Math.round(value * devicePixelRatio)) < .01) });
    };
    requestAnimationFrame(sample);
  }));
  assert.deepEqual(thumbSamples.shapes, ['4pxx16px']);
  assert.ok(thumbSamples.steps >= 2 && thumbSamples.steps <= 9, `thumb should step at most every 200 ms, saw ${thumbSamples.steps} positions`);
  assert.equal(thumbSamples.snapped, true);
  await page.screenshot({ path: path.join(root, 'progress-slider-hover.png') });
  await page.mouse.move(1, 1);
  await page.getByRole('button', { name: '收起播放页', exact: true }).hover();
  await page.getByRole('tooltip').filter({ hasText: '收起播放页' }).waitFor();
  assert.equal(await page.getByRole('tooltip').getAttribute('data-theme'), 'dark');
  await page.mouse.move(1, 1);
  await page.locator('.amll-lyric-player').waitFor();
  await page.waitForFunction(() => {
    const player = document.querySelector('.amll-lyric-player');
    return player && player.querySelectorAll('.FmKaba_lyricLine').length > 0
      && !!document.querySelector('.FmKaba_lyricLine.FmKaba_active .FmKaba_lyricMainLine');
  });
  await page.locator('.now-playing .icon-button').first().hover();
  for (const delay of [40, 100, 220]) {
    await page.waitForTimeout(delay);
    assert.equal(await page.evaluate(() => {
      const unscaled = element => {
        const transform = getComputedStyle(element).transform;
        if (transform === 'none') return true;
        const matrix = new DOMMatrixReadOnly(transform);
        return Math.abs(Math.hypot(matrix.a, matrix.b) - 1) < .002
          && Math.abs(Math.hypot(matrix.c, matrix.d) - 1) < .002;
      };
      return [...document.querySelectorAll('button:not(.play-button),.FmKaba_lyricLine,.icon-button-glyph,.empty-icon,.brand-icon')].every(unscaled);
    }), true);
  }
  await page.mouse.move(1, 1);
  const textButton = page.locator('.now-playing .lyric-toolbar .text-button').first();
  const textButtonBefore = await textButton.evaluate(element => ({
    box: element.getBoundingClientRect().toJSON(),
    color: getComputedStyle(element).color,
    opacity: getComputedStyle(element).opacity,
    transform: getComputedStyle(element).transform,
  }));
  await page.waitForFunction(() => getComputedStyle(document.querySelector('.lyric-toolbar')).opacity === '0');
  await textButton.hover();
  await page.waitForFunction(() => getComputedStyle(document.querySelector('.lyric-toolbar')).opacity === '1');
  await page.waitForTimeout(280);
  const textButtonAfter = await textButton.evaluate(element => ({
    box: element.getBoundingClientRect().toJSON(),
    color: getComputedStyle(element).color,
    opacity: getComputedStyle(element).opacity,
    transform: getComputedStyle(element).transform,
  }));
  for (const key of ['x', 'y', 'width', 'height']) {
    assert.ok(Math.abs(textButtonAfter.box[key] - textButtonBefore.box[key]) < .5, `text button ${key} moved`);
  }
  assert.equal(textButtonAfter.transform, 'none');
  assert.equal(textButtonAfter.opacity, textButtonBefore.opacity);
  assert.notEqual(textButtonAfter.color, textButtonBefore.color);

  await page.mouse.move(1, 1);
  await page.waitForFunction(() => getComputedStyle(document.querySelector('.lyric-toolbar')).opacity === '0');
  await textButton.focus();
  await page.waitForFunction(() => getComputedStyle(document.querySelector('.lyric-toolbar')).opacity === '1');
  await textButton.blur();
  const playButton = page.locator('.now-playing .play-button');
  await playButton.hover();
  await page.waitForTimeout(180);
  assert.equal(await playButton.evaluate(element => {
    const matrix = new DOMMatrixReadOnly(getComputedStyle(element).transform);
    return matrix.a > 1.02 && matrix.d > 1.02;
  }), true);
  await page.mouse.move(1, 1);
  await page.waitForTimeout(480);
  assert.equal(await playButton.evaluate(element => {
    const transform = getComputedStyle(element).transform;
    if (transform === 'none') return true;
    const matrix = new DOMMatrixReadOnly(transform);
    return Math.abs(matrix.a - 1) < .002 && Math.abs(matrix.d - 1) < .002;
  }), true);
  await page.locator('.amll-lyric-player').getByText('测试歌词第二句', { exact: false }).first().click({ force: true });
  await page.waitForFunction(() => Number(document.querySelector('.now-left input[aria-label="播放进度"]').value) >= 4);
  const toast = page.getByRole('button', { name: '关闭提示', exact: true }); if (await toast.count()) await toast.click();
  await page.screenshot({ path: path.join(root, 'lyrics.png') });
  await page.getByRole('button', { name: '暂停', exact: true }).last().click();
  await page.keyboard.press('Space'); await page.getByRole('button', { name: '暂停', exact: true }).last().waitFor();
  await page.getByRole('button', { name: '收起播放页', exact: true }).click();
  await page.waitForFunction(() => {
    const player = document.querySelector('.now-playing');
    if (!player) return false;
    const style = getComputedStyle(player);
    return player.dataset.open === 'false' && style.transform !== 'none';
  });
  await page.waitForFunction(() => {
    const player = document.querySelector('.now-playing');
    if (!player) return false;
    const style = getComputedStyle(player);
    const translateY = style.transform === 'none' ? 0 : new DOMMatrixReadOnly(style.transform).m42;
    return style.visibility === 'hidden' && translateY >= innerHeight - 1;
  });
  assert.equal(await page.locator('.queue-drawer').count(), 0, 'queue drawer was already open');
  const queueShell = await page.evaluate(() => new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('queue drawer mount timed out')), 2000);
    const observer = new MutationObserver(() => {
      const drawer = document.querySelector('.queue-drawer');
      if (!drawer) return;
      clearTimeout(timeout);
      observer.disconnect();
      resolve({
        itemCount: drawer.querySelectorAll('.queue-item').length,
        transform: getComputedStyle(drawer).transform,
      });
    });
    observer.observe(document.body, { childList: true, subtree: true });
    document.querySelector('button[aria-label="播放队列"]').click();
  }));
  assert.equal(queueShell.itemCount, 0, 'queue content mounted with the drawer shell');
  assert.notEqual(queueShell.transform, 'none', 'queue drawer animation did not start');
  await page.locator('.queue-item').first().waitFor();
  const virtualQueue = await page.locator('.queue-list').evaluate(element => ({
    start: Number(element.dataset.virtualStart),
    end: Number(element.dataset.virtualEnd),
    rendered: element.querySelectorAll('.queue-item').length,
    height: element.querySelector('.queue-virtual-space')?.getBoundingClientRect().height,
  }));
  assert.equal(virtualQueue.rendered, virtualQueue.end - virtualQueue.start);
  assert.equal(virtualQueue.height, 60);
  assert.equal(await page.locator('.queue-item').first().evaluate(element => getComputedStyle(element).transform), 'none');
  await page.getByRole('button', { name: '关闭播放队列', exact: true }).click();
  await page.waitForFunction(() => {
    const drawer = document.querySelector('.queue-drawer');
    if (!drawer) return false;
    const style = getComputedStyle(drawer);
    return style.opacity !== '1' || style.transform !== 'none';
  });
  await page.locator('.queue-drawer').waitFor({ state: 'detached' });
  await page.getByRole('button', { name: '音乐库 3', exact: true }).click();
  await page.getByRole('button', { name: '播放 夜色温柔', exact: true }).click();
  await page.getByRole('button', { name: '打开歌词播放页', exact: true }).click();
  await page.waitForFunction(() => document.querySelector('.amll-lyric-player')?.textContent.includes('你好世界'));
  await page.keyboard.press('Escape');

  // Online lyrics: a song without its own lyric shows the AMLL TTML DB match automatically.
  const lyricText = text => page.waitForFunction(value => document.querySelector('.now-playing .amll-lyric-player')?.textContent.includes(value), text);
  await page.getByRole('button', { name: '播放 写给明天', exact: true }).click();
  await page.getByRole('button', { name: '打开歌词播放页', exact: true }).click();
  await lyricText('在线歌词第一句');
  await page.locator('.lyric-toolbar').hover();
  await page.locator('.lyric-toolbar', { hasText: 'AMLL TTML DB' }).waitFor();
  await page.keyboard.press('Escape');
  // A song with its own lyric keeps it by default; the picker lists every source with its match score
  // and the choice is saved per song.
  await page.getByRole('button', { name: '播放 午后散步', exact: true }).click();
  await page.getByRole('button', { name: '打开歌词播放页', exact: true }).click();
  await lyricText('测试歌词第一句');
  const openSources = async () => { await page.locator('.lyric-toolbar').hover(); await page.getByRole('button', { name: '歌词来源', exact: true }).click(); return page.getByRole('menu', { name: '歌词来源', exact: true }); };
  await page.waitForFunction(() => document.querySelector('.lyric-source-button')?.textContent.includes('4 个来源'));
  let sources = await openSources();
  const sourceItems = await sources.getByRole('menuitem').evaluateAll(items => items.map(item => item.getAttribute('aria-label')));
  assert.equal(sourceItems[0], '本地歌词');
  assert.deepEqual(sourceItems.slice(1, 4).sort(), ['AMLL TTML DB', 'LRCLIB', 'lrc.red']); // ordered by match score
  assert.deepEqual(sourceItems.slice(4), ['重新搜索在线歌词', '关联本地歌词…']);
  await page.waitForTimeout(150); await page.screenshot({ path: path.join(root, 'lyric-sources.png') });
  await sources.getByRole('menuitem', { name: 'LRCLIB', exact: true }).click();
  await lyricText('LRCLIB 午后');
  await page.waitForFunction(async () => (await window.music.getLibrary()).tracks.find(t => t.title === '午后散步').lyricPreference === 'lrclib');
  sources = await openSources();
  await sources.getByRole('menuitem', { name: 'lrc.red', exact: true }).click();
  await lyricText('lrc.red 第二句');
  await page.waitForFunction(async () => (await window.music.getLibrary()).tracks.find(t => t.title === '午后散步').lyricPreference === 'lrcred');
  sources = await openSources();
  await sources.getByRole('menuitem', { name: 'AMLL TTML DB', exact: true }).click();
  await lyricText('午后在线歌词');
  await page.waitForTimeout(250); await page.screenshot({ path: path.join(root, 'online-lyrics.png') });
  sources = await openSources();
  await sources.getByRole('menuitem', { name: '本地歌词', exact: true }).click();
  await lyricText('测试歌词第一句');
  // 原 / 简 / 繁: the lyric is rewritten by OpenCC, the choice and traditional standard persist.
  const scriptSwitch = page.getByRole('group', { name: '歌词写法', exact: true });
  await page.locator('.lyric-toolbar').hover();
  assert.equal(await scriptSwitch.getByRole('button', { name: '原', exact: true }).getAttribute('aria-pressed'), 'true');
  await scriptSwitch.getByRole('button', { name: '繁', exact: true }).click();
  await lyricText('測試歌詞第一句');
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.lyricScript === 't');
  await page.waitForTimeout(150); await page.screenshot({ path: path.join(root, 'lyric-script.png') });
  await page.locator('.lyric-toolbar').hover();
  await page.getByRole('button', { name: '选择繁体标准', exact: true }).click();
  await page.getByRole('menu', { name: '繁体标准', exact: true }).getByRole('menuitem', { name: '香港繁体', exact: true }).click();
  await page.waitForFunction(async () => { const s = (await window.music.getLibrary()).settings; return s.lyricScript === 't' && s.lyricVariant === 'hk'; });
  await lyricText('測試歌詞第一句');
  await page.locator('.lyric-toolbar').hover();
  await scriptSwitch.getByRole('button', { name: '原', exact: true }).click();
  await lyricText('测试歌词第一句');
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.lyricScript === 'o');
  await page.keyboard.press('Escape');
  // Disabling one provider drops it from the sources.
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  const lrclibSwitch = page.getByRole('switch', { name: 'LRCLIB', exact: true });
  assert.equal(await lrclibSwitch.getAttribute('aria-checked'), 'true');
  await lrclibSwitch.scrollIntoViewIfNeeded(); await lrclibSwitch.click();
  await page.waitForFunction(async () => !(await window.music.getLibrary()).settings.lyricProviders.includes('lrclib'));
  assert.deepEqual(await page.evaluate(async () => { const t = (await window.music.getLibrary()).tracks.find(x => x.title === '午后散步'); return (await window.music.onlineLyrics(t.id)).map(r => r.provider).sort(); }), ['amll', 'lrcred']);
  await lrclibSwitch.click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.lyricProviders.includes('lrclib'));
  assert.equal((await page.evaluate(() => window.music.lyricsStatus())).appleToken, false);
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();  // Turning the setting off stops online lookups.
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  const onlineSwitch = page.getByRole('switch', { name: '在线歌词', exact: true });
  assert.equal(await onlineSwitch.getAttribute('aria-checked'), 'true');
  await onlineSwitch.click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.onlineLyrics === false);
  assert.deepEqual(await page.evaluate(async () => { const t = (await window.music.getLibrary()).tracks.find(x => x.title === '写给明天'); return window.music.onlineLyrics(t.id); }), []);
  await onlineSwitch.click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.onlineLyrics === true);
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('tab', { name: '外观', exact: true }).click();
  await page.getByRole('combobox', { name: '应用主题', exact: true }).focus();
  await page.keyboard.press('ArrowDown');
  await page.getByRole('listbox', { name: '应用主题', exact: true }).waitFor();
  await page.waitForTimeout(180);
  await page.screenshot({ path: path.join(root, 'drawn-select-dark.png') });
  await page.keyboard.press('Escape');
  assert.equal(await page.getByRole('dialog', { name: '设置与关于', exact: true }).isVisible(), true);
  await page.getByRole('combobox', { name: '应用主题', exact: true }).click();
  await page.keyboard.press('End');
  await page.keyboard.press('Enter');
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'light');
  // Lyric typography: system fonts come from the native host and restyle the mounted AMLL player.
  const lyricStyle = () => page.evaluate(() => {
    const player = getComputedStyle(document.querySelector('.amll-lyric-player'));
    return { family: player.fontFamily, weight: player.fontWeight, size: parseFloat(player.fontSize) };
  });
  const lyricBefore = await lyricStyle();
  const fontList = await page.evaluate(() => window.music.fonts());
  assert.ok(fontList.length > 0 && fontList.some(font => font.family === 'Segoe UI'));
  await page.waitForFunction(() => !document.querySelector('[aria-label="歌词字体"]')?.textContent.includes('正在读取'));
  // The drawn Select closes on outside scrolling, so bring each trigger into view before opening it.
  const pick = async (name, option) => {
    const trigger = page.getByRole('combobox', { name, exact: true });
    await trigger.scrollIntoViewIfNeeded(); await page.waitForTimeout(60);
    await trigger.click();
    await page.getByRole('option', { name: option, exact: true }).click();
  };
  await pick('歌词字体', 'Segoe UI');
  await pick('歌词粗细', '粗 · 700');
  await pick('歌词大小', '120%');
  await page.waitForFunction(async () => {
    const { settings } = await window.music.getLibrary();
    return settings.lyricFont === 'Segoe UI' && settings.lyricWeight === 700 && settings.lyricScale === 1.2;
  });
  await page.waitForFunction(() => getComputedStyle(document.querySelector('.amll-lyric-player')).fontWeight === '700');
  const lyricAfter = await lyricStyle();
  assert.match(lyricAfter.family, /^"?Segoe UI"?,/);
  assert.equal(lyricAfter.weight, '700');
  assert.ok(Math.abs(lyricAfter.size / lyricBefore.size - 1.2) < .01, `lyric size ${lyricBefore.size} -> ${lyricAfter.size}`);
  const preview = await page.getByLabel('歌词字体预览').evaluate(element => getComputedStyle(element.querySelector('span')).fontWeight);
  assert.equal(preview, '700');
  await page.screenshot({ path: path.join(root, 'lyric-typography-settings.png') });
  for (const [name, option] of [['歌词字体', '默认 · 跟随应用'], ['歌词粗细', '默认'], ['歌词大小', '默认 · 100%']]) await pick(name, option);

  await page.waitForFunction(async () => {
    const { settings } = await window.music.getLibrary();
    return settings.lyricFont === '' && settings.lyricWeight === 0 && settings.lyricScale === 1;
  });
  await page.waitForFunction(before => { const style = getComputedStyle(document.querySelector('.amll-lyric-player')); return style.fontWeight === before.weight && style.fontFamily === before.family && parseFloat(style.fontSize) === before.size; }, lyricBefore);
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  await page.locator('.folder-list span').first().hover();
  await page.getByRole('tooltip').filter({ hasText: 'songs' }).waitFor();
  assert.equal(await page.getByRole('tooltip').getAttribute('data-theme'), 'light');
  const nestedFolder = page.locator('.folder-list>div').filter({ hasText: 'nested' });
  await nestedFolder.getByRole('button', { name: /^移除文件夹 / }).click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).folders.length === 1);
  assert.equal((await page.evaluate(() => window.music.getLibrary())).tracks.length, 3);
  await page.getByRole('tab', { name: '外观', exact: true }).click();
  await page.getByRole('combobox', { name: '原生标题栏', exact: true }).click();
  await page.getByRole('option', { name: 'Windows 系统标题栏', exact: true }).click();
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.titleBar === 'system');
  await page.screenshot({ path: path.join(root, 'system-caption-settings.png') });
  await page.getByRole('combobox', { name: '原生标题栏', exact: true }).click();
  await page.getByRole('option', { name: 'WebView2 沉浸式', exact: true }).click();
  await page.locator('.web-caption').waitFor();
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.titleBar === 'webview2');
  await page.evaluate(() => window.music.windowCommand('maximize'));
  await page.waitForFunction(() => document.querySelector('.web-caption button[aria-label="最大化或还原窗口"]')?.getAttribute('data-tooltip') === '向下还原');
  await page.evaluate(() => window.music.windowCommand('maximize'));
  await page.waitForFunction(() => document.querySelector('.web-caption button[aria-label="最大化或还原窗口"]')?.getAttribute('data-tooltip') === '最大化');
  assert.equal(await page.locator('select').count(), 0);
  await page.screenshot({ path: path.join(root, 'webview-caption-settings.png') });
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
  await page.locator('.settings-modal').waitFor({ state: 'detached' });
  await page.screenshot({ path: path.join(root, 'webview-caption.png') });
  // The immersive playback page paints behind the transparent WebView2 caption.
  await page.getByRole('button', { name: '打开歌词播放页', exact: true }).click();
  await page.waitForFunction(() => document.querySelector('.now-playing')?.getAttribute('data-open') === 'true');
  await page.waitForTimeout(650);
  assert.equal(await page.locator('.now-playing').evaluate(element => Math.round(element.getBoundingClientRect().top)), 0);
  assert.equal(await page.locator('.web-caption').evaluate(element => getComputedStyle(element).backgroundColor), 'rgba(0, 0, 0, 0)');
  await page.screenshot({ path: path.join(root, 'webview-caption-immersive.png') });
  await page.getByRole('button', { name: '收起播放页', exact: true }).click();
  await page.waitForFunction(() => document.querySelector('.now-playing')?.getAttribute('data-open') === 'false');
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('combobox', { name: '原生标题栏', exact: true }).click();
  await page.getByRole('option', { name: 'Direct2D 自绘', exact: true }).click();
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  // A previous image error must not suppress a repaired cover with the same MIME.
  const failedArtworkSource = await page.locator('.track-row img').first().evaluate(img => {
    const source = img.src; img.dispatchEvent(new Event('error')); return source;
  });
  await page.waitForFunction(source => !Array.from(document.querySelectorAll('.track-row img')).some(img => img.src === source), failedArtworkSource);
  await writeFile(path.join(root, 'songs', 'folder.jpg'), coverBitmap(0xabcdef));
  await page.evaluate(() => {
    const original = window.music;
    window.__metadataEvents = [];
    original.onMetadata(tracks => window.__metadataEvents.push(...tracks));
    window.music = { ...original, rescan: async () => {
      const result = await original.rescan();
      window.__quickImport = result;
      return result;
    } };
  });
  await page.getByRole('button', { name: '重新扫描', exact: true }).click();
  await page.waitForFunction(() => window.__quickImport?.library.tracks.length === 3);
  assert.equal(await page.evaluate(() => window.__quickImport.library.tracks.every(track => track.metadataPending)), true);
  await page.waitForFunction(() => new Set(window.__metadataEvents.filter(track => !track.metadataPending).map(track => track.id)).size === 3);
  await page.waitForFunction(async () => (await window.music.getLibrary()).tracks.every(track => !track.metadataPending));
  const refreshedArtwork = (await page.evaluate(() => window.music.getLibrary())).tracks;
  assert.equal(refreshedArtwork.every(track => track.coverRevision !== initialArtwork.find(old => old.id === track.id).coverRevision), true);
  assert.equal((await page.evaluate(() => window.music.getLibrary())).folders.length, 1);
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
  await page.waitForFunction(tracks => tracks.every(track => Array.from(document.querySelectorAll('.track-row img')).some(img => img.src.endsWith(`${track.id}?v=${track.coverHash || track.coverRevision}`) && img.complete && img.naturalWidth === 2)), refreshedArtwork);
  // A warm scan keeps artwork URLs and cached bytes stable.
  const cachedCoverFiles = await Promise.all(refreshedArtwork.map(track => readFile(path.join(root, 'data', `${track.id}.cover`))));
  await page.locator('.track-row img').first().evaluate(img => img.dispatchEvent(new Event('error')));
  await page.waitForFunction(() => document.querySelectorAll('.track-row img').length === 2);
  await page.evaluate(() => { window.__metadataEvents = []; });
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  await page.getByRole('button', { name: '重新扫描', exact: true }).click();
  await page.waitForFunction(() => new Set(window.__metadataEvents.filter(track => !track.metadataPending).map(track => track.id)).size === 3);
  const warmArtwork = (await page.evaluate(() => window.music.getLibrary())).tracks;
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
  await page.waitForFunction(() => Array.from(document.querySelectorAll('.track-row img')).filter(img => img.complete && img.naturalWidth === 2).length === 3);
  assert.equal(warmArtwork.every(track => track.coverRevision === refreshedArtwork.find(previous => previous.id === track.id).coverRevision && track.coverHash === refreshedArtwork.find(previous => previous.id === track.id).coverHash), true);
  for (const [index, track] of refreshedArtwork.entries()) assert.deepEqual(await readFile(path.join(root, 'data', `${track.id}.cover`)), cachedCoverFiles[index]);
  const security = await page.evaluate(async () => {
    let blocked = false; try { await fetch('https://example.com'); } catch { blocked = true; }
    const id = 987654;
    const response = new Promise(resolve => {
      const handler = e => { if (e.data?.id === id) { window.chrome.webview.removeEventListener('message', handler); resolve(!!e.data.error); } };
      window.chrome.webview.addEventListener('message', handler);
    });
    window.chrome.webview.postMessage({ id, method: 'eval', params: { code: '1+1' } });
    return { blocked, rejected: await response, node: typeof window.require, host: typeof window.chrome.webview.postMessage };
  });
  assert.equal(security.blocked, true); assert.equal(security.rejected, true); assert.equal(security.node, 'undefined'); assert.equal(security.host, 'function');
  const library = await page.evaluate(() => window.music.getLibrary());
  assert.equal(library.tracks.length, 3); assert.equal(library.settings.titleBar, 'direct2d'); assert.equal(library.settings.theme, 'light');
  const saved = JSON.parse(await readFile(path.join(root, 'data', 'library.json'), 'utf8'));
  assert.equal(saved.favorites.length, 1); assert.equal(saved.playlists[0].trackIds.length, 1); assert.deepEqual(errors, []);
  assert.equal(await page.locator('[title]').count(), 0);
  // Deliberately hold the native removal request: cached UI state, playback,
  // lyrics and queue must clear before any persistence result can arrive.
  await page.getByRole('button', { name: '音乐库 3', exact: true }).click();
  await page.getByRole('button', { name: '播放 午后散步', exact: true }).click();
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('tab', { name: '音乐库', exact: true }).click();
  await page.evaluate(() => {
    const original = window.music;
    window.music = { ...original, removeFolder: folder => new Promise((resolve, reject) => {
      window.__releaseRemoval = fail => fail ? reject(new Error('simulated save failure')) : original.removeFolder(folder).then(resolve, reject);
    }) };
  });
  const removeFolderButton = page.locator('.folder-list').getByRole('button', { name: /^移除文件夹 / });
  await removeFolderButton.click();
  await page.waitForFunction(() => document.querySelectorAll('.folder-list>div').length === 0 && document.querySelectorAll('.track-row:not(.table-head)').length === 0);
  assert.equal((await page.evaluate(() => window.music.getLibrary())).tracks.length, 3);
  await page.waitForFunction(() => navigator.mediaSession.metadata === null && navigator.mediaSession.playbackState !== 'playing');
  await page.evaluate(() => window.__releaseRemoval(true));
  await removeFolderButton.waitFor();
  await page.waitForFunction(() => document.querySelectorAll('.track-row:not(.table-head)').length === 3);
  await removeFolderButton.click();
  await page.waitForFunction(() => document.querySelectorAll('.folder-list>div').length === 0 && document.querySelectorAll('.track-row:not(.table-head)').length === 0);
  await page.evaluate(() => window.__releaseRemoval(false));
  await page.waitForFunction(async () => (await window.music.getLibrary()).tracks.length === 0);
  const cleared = JSON.parse(await readFile(path.join(root, 'data', 'library.json'), 'utf8'));
  assert.deepEqual(cleared.tracks, []); assert.deepEqual(cleared.folders, []); assert.deepEqual(cleared.favorites, []);
  assert.equal(cleared.playlists.every(playlist => playlist.trackIds.length === 0), true);
  assert.equal((await readFile(path.join(root, 'songs', '午后散步.wav'))).length > 44, true);
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
  await page.getByRole('button', { name: /播放队列/ }).click();
  await page.waitForFunction(() => document.querySelector('.drawer-heading p')?.textContent === '0 首音乐');
  console.log('PASS: real C++/WebView2 host, native scan and bridge, audio playback and Range stream, favorites/playlists, AMLL LRC click seeking, TTML rendering, rescan, theme/titlebar switching and persistence, native command validation and network isolation.');
  console.log(`Screenshots: ${root}`);
} catch (error) {
  if (page) { console.error(await page.locator('body').innerText().catch(() => 'No page')); await page.screenshot({ path: path.join(root, 'failure.png') }).catch(() => {}); }
  throw error;
} finally {
  if (browser) await browser.close();
  if (child && child.exitCode === null) { child.kill(); await once(child, 'exit').catch(() => {}); }
}
