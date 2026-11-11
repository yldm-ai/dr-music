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
await mkdir(path.join(root, 'songs'), { recursive: true });
const rate = 22050, bytes = rate * 16 * 2;
const wav = Buffer.alloc(44 + bytes);
wav.write('RIFF', 0); wav.writeUInt32LE(36 + bytes, 4); wav.write('WAVEfmt ', 8); wav.writeUInt32LE(16, 16);
wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22); wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 2, 28); wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write('data', 36); wav.writeUInt32LE(bytes, 40);
for (let i = 0; i < bytes / 2; i++) wav.writeInt16LE(Math.round(Math.sin(i * 2 * Math.PI * 220 / rate) * 100), 44 + i * 2);
for (const name of ['午后散步', '夜色温柔', '写给明天']) await writeFile(path.join(root, 'songs', `${name}.wav`), wav);
await writeFile(path.join(root, 'songs', '午后散步.lrc'), '[00:00.00]测试歌词第一句\n[00:04.00]测试歌词第二句\n[00:08.00]测试歌词第三句\n[00:12.00]最后一句');
await writeFile(path.join(root, 'songs', '夜色温柔.ttml'), '<tt xmlns="http://www.w3.org/ns/ttml"><body><div><p begin="00:01.000" end="00:03.000"><span begin="00:01.000" end="00:02.000">你好</span><span begin="00:02.000" end="00:03.000">世界</span></p></div></body></tt>');
const server = createServer(); server.listen(0, '127.0.0.1'); await once(server, 'listening'); const port = server.address().port; await new Promise(resolve => server.close(resolve));
let child, browser, page;
const errors = [];
try {
  child = spawn(executable, ['--data-dir', path.join(root, 'data'), '--import', path.join(root, 'songs'), '--debug-port', String(port)], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: false });
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
  assert.equal(await page.title(), 'DR Music');
  assert.equal(await page.evaluate(() => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim()), '#c65454');
  assert.equal((await page.evaluate(() => window.music.getLibrary())).tracks.length, 3);
  await page.screenshot({ path: path.join(root, 'library.png') });
  // The actual popover must show on hover and dismiss on keyboard input.
  await page.getByRole('button', { name: '新建歌单', exact: true }).hover();
  await page.getByRole('tooltip').filter({ hasText: '新建歌单' }).waitFor();
  await page.waitForFunction(() => getComputedStyle(document.getElementById('dr-hover-tip')).opacity === '1');
  await page.screenshot({ path: path.join(root, 'tooltip.png') });
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => !document.getElementById('dr-hover-tip').matches(':popover-open'));
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
  await page.getByRole('textbox', { name: '歌单名称' }).fill('原生测试歌单');
  await page.getByRole('button', { name: '保存歌单', exact: true }).click();
  await page.getByRole('button', { name: '午后散步 更多操作', exact: true }).click();
  await page.locator('.song-menu').getByRole('button', { name: '原生测试歌单', exact: true }).click();
  await page.getByRole('button', { name: /原生测试歌单 1/ }).click();
  await page.getByRole('button', { name: '播放 午后散步', exact: true }).click();
  await page.getByRole('button', { name: '暂停', exact: true }).waitFor();
  await page.waitForFunction(() => Number(document.querySelector('input[aria-label="播放进度"]').value) > .1);
  const check = await page.evaluate(async () => {
    const library = await window.music.getLibrary(); const id = library.tracks.find(t => t.title === '午后散步').id;
    const reply = await fetch(`https://media.dr-music.local/audio/${id}`, { headers: { Range: 'bytes=44-99' } });
    return { status: reply.status, size: (await reply.arrayBuffer()).byteLength, range: reply.headers.get('content-range') };
  });
  assert.equal(check.status, 206); assert.equal(check.size, 56); assert.match(check.range, /^bytes 44-99\//);
  await page.getByRole('button', { name: '打开歌词播放页', exact: true }).click();
  await page.locator('.now-playing').waitFor();
  await page.waitForTimeout(400);
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
  await page.locator('.amll-lyric-player').getByText('测试歌词第二句', { exact: false }).first().click({ force: true });
  await page.waitForFunction(() => Number(document.querySelector('.now-left input[aria-label="播放进度"]').value) >= 4);
  const toast = page.getByRole('button', { name: '关闭提示', exact: true }); if (await toast.count()) await toast.click();
  await page.screenshot({ path: path.join(root, 'lyrics.png') });
  await page.getByRole('button', { name: '暂停', exact: true }).last().click();
  await page.keyboard.press('Space'); await page.getByRole('button', { name: '暂停', exact: true }).last().waitFor();
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: '音乐库 3', exact: true }).click();
  await page.getByRole('button', { name: '播放 夜色温柔', exact: true }).click();
  await page.getByRole('button', { name: '打开歌词播放页', exact: true }).click();
  await page.getByText('逐字时间轴', { exact: true }).waitFor();
  await page.waitForFunction(() => document.querySelector('.amll-lyric-player')?.textContent.includes('你好世界'));
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: /设置与关于/ }).click();
  await page.getByRole('combobox', { name: '应用主题', exact: true }).selectOption('light');
  await page.locator('.folder-list span').first().hover();
  await page.getByRole('tooltip').filter({ hasText: 'songs' }).waitFor();
  assert.equal(await page.getByRole('tooltip').getAttribute('data-theme'), 'light');
  await page.getByRole('combobox', { name: '原生标题栏', exact: true }).selectOption('system');
  await page.waitForFunction(async () => (await window.music.getLibrary()).settings.titleBar === 'system');
  await page.screenshot({ path: path.join(root, 'system-caption-settings.png') });
  await page.getByRole('combobox', { name: '原生标题栏', exact: true }).selectOption('direct2d');
  await page.getByRole('button', { name: '重新扫描', exact: true }).click();
  await page.getByRole('status').filter({ hasText: '已扫描 3 首音乐' }).waitFor();
  await page.getByRole('button', { name: '关闭设置', exact: true }).click();
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
  console.log('PASS: real C++/WebView2 host, native scan and bridge, audio playback and Range stream, favorites/playlists, AMLL LRC click seeking, TTML rendering, rescan, theme/titlebar switching and persistence, native command validation and network isolation.');
  console.log(`Screenshots: ${root}`);
} catch (error) {
  if (page) { console.error(await page.locator('body').innerText().catch(() => 'No page')); await page.screenshot({ path: path.join(root, 'failure.png') }).catch(() => {}); }
  throw error;
} finally {
  if (browser) await browser.close();
  if (child && child.exitCode === null) { child.kill(); await once(child, 'exit').catch(() => {}); }
}
