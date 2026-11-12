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
await writeFile(path.join(root, 'songs', '午后散步.lrc'), '[00:00.00]测试歌词第一句\n[00:04.00]测试歌词第二句\n[00:08.00]测试歌词第三句\n[00:12.00]最后一句');
await writeFile(path.join(root, 'songs', '夜色温柔.ttml'), '<tt xmlns="http://www.w3.org/ns/ttml"><body><div><p begin="00:01.000" end="00:03.000"><span begin="00:01.000" end="00:02.000">你好</span><span begin="00:02.000" end="00:03.000">世界</span></p></div></body></tt>');
const server = createServer(); server.listen(0, '127.0.0.1'); await once(server, 'listening'); const port = server.address().port; await new Promise(resolve => server.close(resolve));
let child, browser, page;
const errors = [];
try {
  child = spawn(executable, ['--data-dir', path.join(root, 'data'), '--import', path.join(root, 'songs'), '--import', path.join(root, 'songs', 'nested'), '--debug-port', String(port)], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: false });
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
  await page.getByRole('textbox', { name: '搜索本地音乐' }).focus();
  await page.waitForTimeout(420);
  const searchFocused = await searchField.evaluate(element => ({ border: getComputedStyle(element).borderColor, shadow: getComputedStyle(element).boxShadow, height: element.getBoundingClientRect().height }));
  assert.notEqual(searchFocused.border, searchBefore.border);
  assert.notEqual(searchFocused.shadow, 'none');
  assert.equal(searchFocused.height, searchBefore.height);
  await page.getByRole('textbox', { name: '搜索本地音乐' }).blur();
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
  assert.equal(await page.locator('form.small-modal').evaluate(form => form.noValidate), true);
  const playlistName = page.getByRole('textbox', { name: '歌单名称' });
  await playlistName.press('Enter');
  assert.equal(await page.getByRole('dialog', { name: '新建歌单' }).isVisible(), true);
  await playlistName.fill('原生测试歌单');
  await page.getByRole('button', { name: '保存歌单', exact: true }).click();
  await page.getByRole('button', { name: '午后散步 更多操作', exact: true }).click();
  await page.locator('.song-menu').getByRole('button', { name: '原生测试歌单', exact: true }).click();
  await page.getByRole('button', { name: /原生测试歌单 1/ }).click();
  await page.locator('.playlist-hero .playlist-cover').waitFor();
  assert.equal(await page.locator('.playlist-page .library-footer').count(), 0);
  const playlistRow = page.locator('.track-row:not(.table-head)').first();
  await playlistRow.hover();
  await page.waitForTimeout(260);
  assert.equal(await playlistRow.locator('.row-number-content>span').isVisible(), true);
  assert.equal(await playlistRow.locator('.track-cover-overlay').evaluate(element => getComputedStyle(element).opacity), '1');
  await page.screenshot({ path: path.join(root, 'playlist.png') });
  await playlistRow.click({ button: 'right' });
  await page.locator('.song-menu').getByRole('button', { name: '复制歌名', exact: true }).waitFor();
  await page.locator('.song-menu').getByRole('button', { name: '复制歌名和歌手', exact: true }).waitFor();
  await page.keyboard.press('Escape');
  await page.locator('.song-menu').waitFor({ state: 'detached' });
  await page.getByRole('button', { name: '播放 午后散步', exact: true }).click();
  await page.getByRole('button', { name: '暂停', exact: true }).waitFor();
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
  const volumeSlider = page.getByRole('slider', { name: '音量', exact: true });
  const volumeBefore = await volumeSlider.evaluate(element => ({
    box: element.getBoundingClientRect().toJSON(),
    transform: getComputedStyle(element).transform,
  }));
  assert.ok((await volumeSlider.boundingBox()).width >= 100);
  const initialVolume = Number(await volumeSlider.inputValue());
  await volumeSlider.hover();
  await page.mouse.wheel(0, -100);
  const increasedVolume = Math.min(1, Math.round((initialVolume + .05) * 100) / 100);
  await page.waitForFunction(value => Math.abs(Number(document.querySelector('.player-tools .volume-slider').value) - value) < .001, increasedVolume);
  assert.equal(await page.locator('.player-tools .volume-value').innerText(), `${Math.round(increasedVolume * 100)}%`);
  await page.mouse.wheel(0, 100);
  await page.waitForFunction(value => Math.abs(Number(document.querySelector('.player-tools .volume-slider').value) - value) < .001, initialVolume);
  await page.waitForTimeout(240);
  const volumeHover = await volumeSlider.evaluate(element => {
    return {
      box: element.getBoundingClientRect().toJSON(),
      transform: getComputedStyle(element).transform,
      cursor: getComputedStyle(element).cursor,
    };
  });
  const sliderThumbHoverStyle = await page.evaluate(() => {
    const rule = [...document.styleSheets]
      .flatMap(sheet => [...sheet.cssRules])
      .find(rule => rule instanceof CSSStyleRule && rule.selectorText.includes('.volume-slider:hover::-webkit-slider-thumb'));
    return { width: rule?.style.width, height: rule?.style.height, borderRadius: rule?.style.borderRadius };
  });
  for (const key of ['x', 'y', 'width', 'height']) assert.ok(Math.abs(volumeHover.box[key] - volumeBefore.box[key]) < .5, `volume slider ${key} moved`);
  assert.equal(volumeHover.transform, volumeBefore.transform);
  assert.equal(volumeHover.cursor, 'pointer');
  assert.deepEqual(sliderThumbHoverStyle, { width: '5px', height: '16px', borderRadius: '999px' });
  const volumeValue = Number(await volumeSlider.inputValue());
  const edgeX = volumeHover.box.x + 4 + (volumeHover.box.width - 8) * volumeValue + 2;
  const edgeY = volumeHover.box.y + volumeHover.box.height - 1;
  await page.mouse.move(edgeX, edgeY);
  await page.waitForTimeout(220);
  const volumeEdge = await volumeSlider.evaluate((element, point) => {
    const hit = document.elementFromPoint(point.x, point.y);
    return { hover: element.matches(':hover'), cursor: getComputedStyle(element).cursor, hit: hit === element || element.contains(hit) };
  }, { x: edgeX, y: edgeY });
  assert.deepEqual(volumeEdge, { hover: true, cursor: 'pointer', hit: true });
  await page.screenshot({ path: path.join(root, 'volume-slider-hover.png') });
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
  await page.getByLabel('调节播放页音量', { exact: true }).click();
  const immersiveVolume = page.getByRole('slider', { name: '播放页音量', exact: true });
  await immersiveVolume.waitFor();
  const immersiveBefore = Number(await immersiveVolume.inputValue());
  await immersiveVolume.hover(); await page.mouse.wheel(0, -100);
  const immersiveAfter = Math.min(1, Math.round((immersiveBefore + .05) * 100) / 100);
  await page.waitForFunction(value => Math.abs(Number(document.querySelector('.now-volume-panel input').value) - value) < .001, immersiveAfter);
  assert.ok(Math.abs(Number(await volumeSlider.inputValue()) - immersiveAfter) < .001);
  assert.equal(await page.locator('.now-volume-panel output').innerText(), `${Math.round(immersiveAfter * 100)}%`);
  await immersiveVolume.press('End'); await page.mouse.wheel(0, -100);
  assert.equal(Number(await immersiveVolume.inputValue()), 1);
  await immersiveVolume.press('Home'); await page.mouse.wheel(0, 100);
  assert.equal(Number(await immersiveVolume.inputValue()), 0);
  await page.getByRole('button', { name: '播放页音量静音 / 恢复', exact: true }).click();
  await page.waitForFunction(() => Number(document.querySelector('.now-volume-panel input').value) > 0);
  await immersiveVolume.press('Escape');
  assert.equal(await page.locator('.now-volume').evaluate(element => element.open), false);
  assert.equal(await page.locator('.lyric-info').count(), 0);
  const positionClock = page.locator('.now-playing .progress-control span').first();
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
  await page.waitForFunction(() => document.querySelector('.drawer-heading p')?.textContent === '0 首本地音乐');
  console.log('PASS: real C++/WebView2 host, native scan and bridge, audio playback and Range stream, favorites/playlists, AMLL LRC click seeking, TTML rendering, rescan, theme/titlebar switching and persistence, native command validation and network isolation.');
  console.log(`Screenshots: ${root}`);
} catch (error) {
  if (page) { console.error(await page.locator('body').innerText().catch(() => 'No page')); await page.screenshot({ path: path.join(root, 'failure.png') }).catch(() => {}); }
  throw error;
} finally {
  if (browser) await browser.close();
  if (child && child.exitCode === null) { child.kill(); await once(child, 'exit').catch(() => {}); }
}
