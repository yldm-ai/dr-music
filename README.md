# DR Music

suxiaofan 的完全本地音乐播放器。**C++20 / Win32 原生宿主 + WebView2 + Direct2D 标题栏**，React 与 Apple Music-like Lyrics 在 WebView2 中渲染。运行时不需要 Node.js，不打包浏览器，也不使用 Electron。

项目参考 [MySpotify](https://github.com/margbug01/MySpotify) 的产品结构和歌词分词逻辑，原始说明、原作者与许可证保留在 `docs/upstream/`、[LICENSE](LICENSE) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。原参考目录不作修改。原生窗口与桥接的分层方式参考同级 `dr-dict`。

## 一键编译运行

需要 Windows 10/11 x64、Visual Studio 的「使用 C++ 的桌面开发」、CMake 3.25+、Node.js 22.12+（仅构建前端时使用），以及 Microsoft Edge WebView2 Evergreen Runtime。

```powershell
.\scripts\build.ps1 -Run               # 安装依赖 → CMake 构建 → CTest → 启动
.\scripts\build.ps1                    # 只构建和测试
.\scripts\build.ps1 -Run -SkipInstall  # 已安装 npm 依赖时跳过 npm ci
.\scripts\build.ps1 -Package           # 构建、测试并生成 Windows 分发 zip
```

与 `../dr-dict/scripts/build.ps1` 一样，支持 `-Generator 'Visual Studio 18 2026'`。默认由 CMake 选择本机 Visual Studio，目标架构为 x64。脚本可从任意目录调用；失败立即停止，不启动旧产物。Node.js 与 npm 只负责前端构建和测试，音乐库与资源服务均由 C++ 实现。

编译输出：`build/bin/Release/dr-music.exe`。请保留旁边的 `web/`、`licenses/`、`LICENSE` 与声明文件。打包输出：`release/DR-Music-0.2.0-Windows-x64.zip`，解压后运行 `dr-music.exe`。

首次构建需要下载 npm 依赖及固定版本的 C++ 依赖（nlohmann/json 3.12.0、TagLib 2.3.2、zlib 1.4、WebView2 SDK 1.0.2903.40）。构建好的应用不需要联网；系统 WebView2 Runtime 的安装与维护由 Windows/Edge 管理。

## 功能

- 文件夹递归扫描与本地音频导入：先按文件名加入歌曲并允许播放，C++ / TagLib 在后台分批补齐标签、时长、音质、封面和内嵌歌词。后台读取期间仍可移除目录或修改设置，中断后下次启动会继续补齐。
- 元数据支持压缩 ID3 标签、多艺术家、专辑艺术家及曲目/碟片序号。封面优先读取有效的内嵌正面图片，再尝试歌曲同目录的同名图片、cover、folder、front、album 图片；由 C++ 识别内容并生成最长边 1024 的 PNG 缓存（系统缺少 WebP 解码器时保留通过容器检查的 WebP）。旧音乐库在升级后后台重新解析一次，无需重新导入。
- 搜索、排序、专辑与艺术家浏览、收藏、新建 / 重命名 / 删除歌单。
- 播放 / 暂停、切歌、音量、进度定位、随机与循环、播放队列、下一首播放。
- AMLL 歌词滚动与逐字高亮，点击歌词跳转、本地歌词关联与全局时间偏移。
- 深浅色主题。默认 Direct2D 原生自绘标题栏，可在「设置与关于 → 外观 → 原生标题栏」切换 WebView2 沉浸式标题栏或 Windows 系统标题栏，选择会保存。
- 网页界面与 D2D 标题栏统一首选 Noto Sans SC，依次回退至 Noto Sans CJK SC、微软雅黑、微软雅黑 UI 和 Segoe UI；使用本机已安装的字体。
- 原生标题栏支持拖动、双击最大化、最小化 / 最大化 / 还原 / 关闭、边缘缩放、DPI 变化。Direct2D 模式的最大化按钮保留 `HTMAXBUTTON` 命中语义，供系统 Snap Layouts 使用。
- 标题栏始终显示 DR Music；标题栏按钮与页面控件统一使用自绘悬浮提示，约 500ms 后显示，移开、点击或滚动即隐藏。
- 页面、列表、歌曲切换、播放状态、菜单、队列与弹窗使用 Motion 弹簧和进出场动画，并遵循系统的减少动态效果设置；强调色采用网易云音乐风格红色。
- 磁盘文件选择、文件定位、数据目录、索引保存均由 C++ 调用 Windows API。移除文件夹后界面立即清理该目录及子目录的歌曲、收藏/歌单引用、队列和受影响的播放/歌词；保存失败时恢复音乐库。只删除索引与缓存封面，不删除音频。损坏索引会先备份。

应用不包含账号、Spotify API/DRM、在线曲库、联网找歌词、遥测或自动更新。前端 CSP 和宿主资源过滤只允许打包页面与本地媒体虚拟主机；主机名是 WebView2 本地映射，不是远程服务。

## 音频与歌词

扫描：MP3、FLAC、WAV、M4A、AAC、OGG、OPUS、WEBM、MP4。实际播放由 WebView2 的 HTMLAudio 解码，兼容性取决于具体编码；TagLib 不支持的标签会回退为文件名。没有接入转码、WMA / APE / DSD 解码。

同名歌词放在歌曲旁边：

```text
Music/
  某首歌.flac
  某首歌.ttml
  某首歌.yrc
  某首歌.lrc
  某首歌.txt
```

优先级：手动关联 → 同名 TTML / YRC / LRC / TXT → 内嵌文本歌词。编码支持 UTF-8 与带 BOM 的 UTF-16LE；暂不自动识别 GBK。普通 LRC 估算字词时间并明确标注；TTML、YRC、增强 LRC 保留原字词时间。纯文本静态显示。标准 TTML 缺少 Apple 专用 key 时只在内存中补齐，不修改歌词文件。歌词偏移范围 ±30 秒，正值提前显示。

| 操作 | 快捷键 |
| --- | --- |
| 播放 / 暂停 | Space |
| 上一首 / 下一首 | Ctrl + ← / → |
| 打开 / 收起歌词页 | Ctrl + L |
| 喜欢当前歌曲 | Ctrl + D |
| 搜索 | Ctrl + F |
| 关闭面板 | Esc |

输入框内不会拦截空格。系统媒体键通过 WebView2 的 Media Session 接入，具体取决于 Windows / Runtime 支持。

## 架构与验证

```text
native/
  main.cpp            Win32/COM 启动、数据目录锁、命令行
  window.*            D2D/DirectWrite 标题栏、系统标题栏、命中测试与 DPI
  webview.*           WebView2 控制器、页面映射、消息与资源边界
  application.*       原生文件选择、固定命令分发、后台工作队列
  library.*           TagLib 扫描、收藏/歌单/设置、原子 JSON 保存
  media.*             Range 请求与可定位的有界 IStream 文件流
src/                  React 页面、WebView2 消息桥、HTMLAudio、AMLL
tests/native_tests.cpp 原生音乐库、元数据、文件流和标题栏交互测试
tests/*.test.ts        歌词与队列逻辑测试
scripts/build.ps1      一键原生构建 / 测试 / 运行 / 打包
scripts/smoke.mjs      通过 CDP 验证真实 C++ / WebView2 应用
docs/upstream/        原项目声明与来源文件
```

```powershell
npm test
ctest --test-dir build -C Release --output-on-failure
npm run test:smoke # 先构建；使用独立测试库和自动生成的 WAV
```

`npm run dev` 仅启动 Vite 界面预览；完整本地功能须从 C++ 程序打开。正式资源通过 `https://app.dr-music.local/` 映射，音频通过 `https://media.dr-music.local/audio/<已导入 ID>` 读取。桥接只接受主页面发送的固定命令，不提供任意脚本、命令或路径执行。

命令行支持 `--data-dir <目录>`、`--import <文件或文件夹>`、`--shell direct2d|webview2|system`。`--debug-port <端口>` 仅供显式开启本机 CDP 调试；正常启动不开放调试端口。

## 数据与当前边界

数据继续保存在 `%APPDATA%/DR Music/`，可以直接读取 0.1.0 的 `library.json`、收藏、歌单和封面。新版 WebView2 缓存位于其 `webview-profile/` 子目录，不影响原索引。只保存音频绝对路径，不复制或上传音频。

文件夹手动重新扫描，不持续监听；音频移动后重新导入并移除旧条目。队列和当前进度仅在本次运行保留。未接入系统托盘、全局热键、均衡器。关闭窗口退出程序。当前原生宿主仅支持 Windows x64。验证范围见 [验证记录](docs/VALIDATION.md)。
