# DR Music 0.2.0 原生迁移验证

目标环境：Windows x64、MSVC / CMake、系统 WebView2 Evergreen Runtime。0.1.0 的 Electron 验证不能替代当前版本的验证。

自动化入口：

```powershell
.\scripts\build.ps1 -SkipInstall
ctest --test-dir build -C Release --output-on-failure
npm run test:smoke
```

原生测试使用自动生成的带标签 WAV 和独立临时目录，覆盖：

- C++ / TagLib 元数据、时长、递归扫描、去重、收藏/歌单/主题/标题栏保存与重新读取。
- 本地歌词查找、UTF-16LE 解码、无效 ID、损坏索引备份、移除索引时保留音频。
- Range 解析、非法/越界范围、IStream 有界读取、定位、克隆与长度报告；资源 URL 不接受任意路径。
- 原生 D2D 标题栏命中测试、最大化/还原、系统标题栏切换与原生窗口标题。
- 标题栏鼠标离开事件回归：隐藏窗口后派发离开通知，验证不会反复注册离开通知、形成消息循环并饿死绘制消息。

前端逻辑测试覆盖 LRC 时间轴/翻译/分词、YRC 时间、纯文本歌词，以及队列循环与随机边界。

桌面测试直接启动 `build/bin/Release/dr-music.exe`，使用显式本机 CDP 端口连接系统 WebView2，不启动 Electron，也不使用模拟的 native bridge。检查本地扫描、歌曲播放、Range 读取、收藏和歌单保存、AMLL 点击定位、TTML 渲染、重新扫描，以及主题与原生标题栏切换。数据位于 `.test-data/native-smoke/`，不接触正式音乐库。

设置 `DR_MUSIC_SMOKE_EXE` 可用同一套桌面测试验证分发目录中的原生可执行文件。正常启动不开放 CDP 调试端口。

范围限制：音频实际解码以生成的 WAV 为主；其他编码的所有变体、超大音乐库、多显示器跨 DPI、Snap Layouts 弹出行为与系统媒体键在不同 Windows / Runtime 版本上的表现仍需更多真机覆盖。当前不提供 macOS / Linux 原生宿主，不含额外音频转码器。

原生外观检查曾发现最大化后标题栏重绘被鼠标离开消息循环阻塞，已修复该循环并将 D2D 绘制区域限制为独立标题栏 HWND。修复后的自动化回归可重复运行；本次最终手动外观复查因用户按 Esc 停止桌面自动化而未继续。
