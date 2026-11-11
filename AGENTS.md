# DR Music development

- Architecture: C++20 / Win32 + WebView2, with a native Direct2D/DirectWrite caption and optional system caption. React / AMLL are bundled web assets. Do not reintroduce Electron or a Node.js runtime/helper process.
- Reference the shell, hit tests, WebView2 message boundaries and CMake patterns in `../dr-dict` before changing window/host behavior. The reference directory stays unchanged.
- Preserve `LICENSE`, `THIRD_PARTY_NOTICES.md` and the original files in `docs/upstream/`.
- Keep file access, dialogs, metadata, indexing, persistence and resource serving in `native/`. `src/bridge.ts` exposes only fixed WebView2 messages, validated against the bundled main page by the C++ host.
- Keep the app local-only: no Spotify, accounts, remote lyric search, analytics or online updates. Node.js is only a build/test tool.
- Build and run: `./scripts/build.ps1 -Run`; add `-SkipInstall` when npm dependencies are already installed. Output: `build/bin/Release/dr-music.exe`.
- Verify with CTest (native library/media/caption tests and frontend lyric/queue tests). For bridge/playback/UI changes, also run `npm run test:smoke` against the actual WebView2 host. Set `DR_MUSIC_SMOKE_EXE` to test a packaged executable.
- Tests generate their own audio and isolated data. Never modify the user's music or production library for testing. Test behavior and state changes, not wording/CSS snapshots.
- Generate frontend notices with `npm run notices`; CMake also copies native dependency licenses. `./scripts/build.ps1 -Package` produces the native Windows distribution zip.
