import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
const [testExe, ffmpeg] = process.argv.slice(2);
const root = await mkdtemp(path.join(tmpdir(), 'dr-music-playback-'));
try {
  const formats = [
    ['pcm.wav', 'pcm_s16le'], ['float.wav', 'pcm_f32le'], ['music.mp3', 'libmp3lame'], ['music.flac', 'flac'],
    ['music.aac', 'aac'], ['music.m4a', 'aac'], ['lossless.m4a', 'alac'], ['music.ogg', 'libvorbis'],
    ['music.opus', 'libopus'], ['music.webm', 'libopus'],
  ];
  for (const [name, codec] of formats) execFileSync(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100:duration=2', '-c:a', codec, path.join(root, name)], { windowsHide: true });
  await writeFile(path.join(root, 'broken.wav'), 'not an audio file');
  execFileSync(testExe, [root], { stdio: 'inherit', windowsHide: true });
} finally { await rm(root, { recursive: true, force: true }); }
