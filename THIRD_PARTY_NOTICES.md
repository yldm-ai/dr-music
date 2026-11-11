# DR Music — 原项目与第三方声明

DR Music 是 suxiaofan 的个人本地音乐播放器。当前宿主为独立实现的 C++20 / Win32 / WebView2，标题栏使用 Direct2D / DirectWrite；React 与 AMLL 作为打包网页运行。原作者的贡献及声明不因名称与技术栈变更而转移。

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

- **TagLib 2.1.1**：https://taglib.org/ / https://github.com/taglib/taglib 。本地元数据、封面与内嵌文本歌词读取。版权归 Scott Wheeler、TagLib 作者及贡献者；提供 LGPL-2.1 / MPL-1.1 双重许可，原文随应用保存在 `licenses/native/COPYING.LGPL` 和 `COPYING.MPL`。
- **utfcpp**：TagLib 发行源码内含的 Unicode 支持库；原许可证收录于 `licenses/native/utfcpp-LICENSE`。
- **nlohmann/json 3.12.0**：https://github.com/nlohmann/json 。Niels Lohmann 及贡献者，MIT，原文位于 `licenses/native/json-LICENSE.MIT`。
- **Microsoft WebView2 SDK 1.0.2903.40**：https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.2903.40 。静态 loader 和 SDK，适用 Microsoft 原始许可，位于 `licenses/native/WebView2-LICENSE.txt`。运行时使用用户系统安装的 Microsoft Edge WebView2 Evergreen Runtime，本应用不重新打包 Chromium。
- Windows、Win32、Direct2D、DirectWrite、DWM 等系统组件属于各自权利人。

## 网页及构建依赖

React / React DOM（MIT）、Lucide（ISC）和其他打包依赖的版本由 `package-lock.json` 锁定。原许可与版权文字由 `scripts/licenses.mjs` 收集至 `licenses/DEPENDENCY_LICENSES.txt`。Vite、TypeScript、Node.js 与 Playwright 仅用于构建或测试，不随应用作为后端运行。

## 修改与分发

- 2026-11-11，0.1.0：创建本地音乐播放器，移植歌词分词逻辑，接入 AMLL，新增音乐库、收藏、歌单和队列。最初采用的 Electron 宿主已在后续版本移除。
- 2026-11-11，0.2.0：按项目所有者要求迁移为 C++ / WebView2。新增原生 D2D / 系统标题栏、TagLib 后端、原生文件选择、后台扫描、原子 JSON 存储和 Range 文件流，保留 0.1.0 索引兼容性。

本项目按 AGPL-3.0-only 提供。分发时保留 LICENSE、原项目声明、适用的第三方许可及对应源代码；不要将原项目及第三方代码声明为独占原创。
