# DR Music — 原项目与第三方声明

DR Music 是 suxiaofan 的个人音乐播放器。当前宿主为独立实现的 C++20 / Win32 / WebView2，标题栏使用 Direct2D / DirectWrite；React 与 AMLL 作为打包网页运行。原作者的贡献及声明不因名称与技术栈变更而转移。

## MySpotify（原项目）

- 来源：https://github.com/margbug01/MySpotify
- 原作者 / 维护者：margbug01 及原项目贡献者。
- 本地参考版本：0.4.19，提交 `8d86be5ab36830aa862d831e37355186bf2d5a49`。
- 许可证：GNU Affero General Public License v3.0，完整正文保存在根目录 LICENSE。
- 原始中文说明及声明：`docs/upstream/MySpotify.README.md`。
- 原始英文说明及声明：`docs/upstream/MySpotify.README.en.md`。
- 原始歌词实现：`docs/upstream/MySpotify.lyrics.go`，保留为来源记录，不参与本项目编译。

本项目保留原项目的侧栏、音乐库、底部播放器及正在播放页面的产品结构思路。`src/lib/lyrics.ts` 移植其 SpreadWords 分词与字词时间分配思路，并注明来源。原项目关于 Spotify 独立性、账号、内部接口和风险的声明完整保存在原始 README 中；这些内容描述原始 MySpotify。本项目不包含对应在线链路。

## DR Dict / DR Reader（原生架构参考）

本机同级 `../dr-dict`：`ui/src/window.cpp`、`ui/include/dr/window.h`、`app/src/webview.cpp`、`web/bridge.ts`、`web/tooltip.ts`、`app/CMakeLists.txt` 与 `scripts/build.ps1`。

本机同级 `../dr-reader`：`app/src/application.cpp` 与 `web/{app.ts,tooltip.ts}`。

参考其原生窗口与 WebView2 分离、D2D 标题栏、非客户区命中测试、DPI 处理、虚拟主机映射、JSON 消息桥、标题栏按钮到网页顶层提示的事件桥和 CMake 构建模式。本项目实现位于 `native/`、`src/{bridge.ts,tooltips.ts}` 与 `scripts/build.ps1`，不依赖参考目录中的词典、阅读器引擎或运行时资源。

## Apple Music-like Lyrics（AMLL）

- 来源：https://github.com/amll-dev/applemusic-like-lyrics
- 作者：AMLL 项目作者及贡献者（amll-dev）。
- 使用：`@applemusic-like-lyrics/core 0.6.0`、`@applemusic-like-lyrics/lyric 1.1.0` 及其依赖。
- 用途：真实 DOM 歌词渲染、弹簧滚动、逐字高亮与本地歌词解析。
- 许可证：AGPL-3.0-only，原许可与版权文字收录于 `licenses/DEPENDENCY_LICENSES.txt`。

组件和资源均随应用打包，不访问 CDN 或歌词服务器；上游权利保留。

## C++ / Windows 依赖

- **miniaudio 0.11.26**：https://github.com/mackron/miniaudio 。David Reid 及贡献者，采用 Public Domain / MIT-0 双重许可；用于原生音频设备输出，许可证位于 `licenses/native/miniaudio-LICENSE`。
- **FFmpeg 8.1.2**：https://ffmpeg.org/ 。本软件使用 FFmpeg 项目的 `avformat`、`avcodec`、`avutil`、`swresample` 动态库，采用 LGPL-2.1-or-later；构建未启用 GPL/nonfree 组件。只启用本地音频解封装与解码，不启用网络、编码器、视频解码或 FFmpeg 命令行程序。许可、对应源码压缩包、vcpkg 补丁和构建配方随分发保存在 `licenses/native/ffmpeg/`。构建配方基于 microsoft/vcpkg（MIT），保留其版权与许可。
- **TagLib 2.3.2**：https://taglib.org/ / https://github.com/taglib/taglib 。本地元数据、封面与内嵌文本歌词读取。版权归 Scott Wheeler、TagLib 作者及贡献者；提供 LGPL-2.1 / MPL-1.1 双重许可，原文随应用保存在 `licenses/native/COPYING.LGPL` 和 `COPYING.MPL`。
- **OpenCC 1.4.2**：https://github.com/BYVoid/OpenCC 。用于「简繁通搜」的繁体→简体转换（t2s、tw2s、tw2sp、hk2s 配置及其 .ocd2 词典，随应用位于 `opencc/`）。版权归 Carbo Kuo 及 OpenCC 贡献者，Apache License 2.0，原文随应用保存在 `licenses/native/OpenCC-LICENSE`。集成方式参考 `../dr-dict`。OpenCC 静态链接其内置依赖：marisa-trie 0.3.1（BSD-2-Clause OR LGPL-2.1-or-later，`licenses/native/marisa-COPYING.md`）、darts-clone 0.32h（BSD-2-Clause，`licenses/native/darts-clone-COPYING.md`）与 RapidJSON 1.1.0（MIT，版权归 THL A29 Limited / Tencent 及 Milo Yip）。
- **AMLL TTML DB**：https://github.com/amll-dev/amll-ttml-db 。「在线歌词」从该库在线获取逐词 TTML 歌词（运行时下载，不随应用分发），歌词数据采用 CC0 1.0 Universal。
- **BetterLyrics**（GPL-3.0）：https://github.com/jayfunc/BetterLyrics 。在线歌词的整体设计参考其 `LyricsSearchService` / `MetadataComparer` / `Providers/AppleMusic`：各歌词源并行查询，读取全部标题、艺术家、专辑别名，按标题 30%、艺术家 30%、专辑 10%、时长 30% 的 Jaro-Winkler 加权评分选用最佳结果，无标签歌曲按文件名词序指纹匹配，Apple Music 使用网页播放器令牌与用户自己的 media-user-token。本项目为独立的 C++ 实现。
- **Lyricify Lyrics Helper**（Apache-2.0）：https://github.com/WXRIW/Lyricify-Lyrics-Helper （BetterLyrics 使用的 jayfunc 分支）。QQ 音乐、网易云音乐、酷狗音乐、LRCLIB 的请求方式与解码逻辑移植自其 `Providers/Web/*` 与 `Decrypter/*`，其中 QQ 音乐 QRC 所用的非标准 Triple-DES（`DESHelper`）、KRC 的 XOR 密钥、网易云 EAPI 的加密流程逐行移植到 `native/lyric_crypto.cpp` / `native/lyric_providers.cpp`。许可原文随应用保存在 `licenses/Lyricify-Lyrics-Helper-LICENSE.txt`。
- **Better Lyrics**（GPL-3.0）：https://github.com/better-lyrics/better-lyrics 。其 BiniLyrics 歌词源来自社区 TTML 歌词库 lrc.red（https://lrc.red ，原 BiniLyrics）；本项目直接调用 lrc.red 的公开接口（`/search.json` 与 `/s/<ISRC>.ttml`），未复制 Better Lyrics 代码。
- **CheemsAutoTranslate**（MIT）：https://github.com/firebugbugs/CheemsAutoTranslate 。本地混元翻译的做法参考其 `docs/models/Modelfile.Hy-MT2` 与 `TranslationService`：Hy-MT2 的对话标记、单轮用户提示与腾讯推荐的采样参数（temperature 0.7、top_p 0.6、top_k 20、repeat_penalty 1.05）。本项目为独立的 C++ 实现（`native/local_translator.cpp`）。
- **小牛翻译开放平台**：https://niutrans.com 。歌词翻译可选用其文本翻译 API v2（`https://api.niutrans.com/v2/text/translate`），按其文档实现签名；需用户自己的 App ID / API Key，使用须遵守小牛翻译的服务协议。
- **腾讯混元 Hy-MT2 1.8B**（腾讯发布的模型许可，见模型页面）：https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF 。应用不包含模型文件，由用户在设置中通过本机 Ollama 自行下载，使用须遵守其许可。
- **zlib 1.4**：https://zlib.net/ 。用于 TagLib 解压压缩 ID3 标签；版权归 Jean-loup Gailly、Mark Adler 及贡献者，采用 zlib 许可证，原文随应用保存在 `licenses/native/zlib-LICENSE`。
- **utfcpp**：TagLib 发行源码内含的 Unicode 支持库；原许可证收录于 `licenses/native/utfcpp-LICENSE`。
- **nlohmann/json 3.12.0**：https://github.com/nlohmann/json 。Niels Lohmann 及贡献者，MIT，原文位于 `licenses/native/json-LICENSE.MIT`。
- **Microsoft WebView2 SDK 1.0.2903.40**：https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.2903.40 。静态 loader 和 SDK，适用 Microsoft 原始许可，位于 `licenses/native/WebView2-LICENSE.txt`。运行时使用用户系统安装的 Microsoft Edge WebView2 Evergreen Runtime，本应用不重新打包 Chromium。
- Windows、Win32、Direct2D、DirectWrite、DWM 等系统组件属于各自权利人。

## 网页及构建依赖

React / React DOM（MIT）、Motion / Framer Motion 14.12.0（MIT）、Lucide（ISC）和其他打包依赖的版本由 `package-lock.json` 锁定。Motion 用于页面、列表、播放状态、菜单和弹窗的 React 动画。原许可与版权文字由 `scripts/licenses.mjs` 收集至 `licenses/DEPENDENCY_LICENSES.txt`。Vite、TypeScript、Node.js 与 Playwright 仅用于构建或测试，不随应用作为后端运行。

歌词注音（「音」）：粤拼来自 ToJyutping（CanCLID，BSD-2-Clause）；日语罗马音和假名由 kuromoji.js 的 `@sglkc/kuromoji` 1.1.0 分支（Takuya Asano / Atilika Inc.，Apache-2.0）分词，读音转写使用 wanakana 5.3.1（MIT）。kuromoji 附带的 IPADIC 词典（mecab-ipadic-2.7.0-20070801，© 2000–2003 Nara Institute of Science and Technology）随应用分发于 `web/kuromoji/`，NAIST 的版权声明与免责条款原文见 `licenses/DEPENDENCY_LICENSES.txt` 中 `@sglkc/kuromoji` 的 NOTICE.md。

## 修改与分发

- 2026-11-11，0.1.0：创建音乐播放器，移植歌词分词逻辑，接入 AMLL，新增音乐库、收藏、歌单和队列。最初采用的 Electron 宿主已在后续版本移除。
- 2026-11-11，0.2.0：按项目所有者要求迁移为 C++ / WebView2。新增原生 D2D / 系统标题栏、TagLib 后端、原生文件选择、后台扫描、原子 JSON 存储和 Range 文件流，保留 0.1.0 索引兼容性。

本项目按 AGPL-3.0-only 提供。分发时保留 LICENSE、原项目声明、适用的第三方许可及对应源代码；不要将原项目及第三方代码声明为独占原创。
