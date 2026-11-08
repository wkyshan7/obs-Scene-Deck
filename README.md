# OBS 场景导播台 / Scene Deck

OBS 原生 C++ 插件：在可停靠面板中展示场景实时预览，并用键盘完成选位、预监、转场和切换。

从 **0.4.0** 起提供 Windows、Linux 和 macOS 的原生适配、构建与打包流程，要求 **OBS 30+ / Qt 6.4+ / C++17**。Windows 和 Linux 已实际编译测试；macOS 的 Intel / Apple Silicon 构建流程已配置，尚未在 macOS 验证。插件使用公开 API，不依赖 OBS 内部的 Qt 界面类，也不需要启用 WebSocket。各平台环境、安装位置及兼容范围见 [跨平台构建说明](CROSS_PLATFORM.md)。

## 已实现

- 可停靠场景网格、可调整行列、分页和按名称搜索。
- 可见卡片使用 OBS 原生 GPU 显示；隐藏面板、翻页和销毁卡片时释放显示与 showing 引用。
- 画面内左上角显示场景名称、右上角显示本页编号，名称过长时自动省略；不额外占用预览行。
- 红框标记正式输出、绿框标记预监、细黄框标记键盘选中；正式输出与预监重合时保留红绿双框。
- 行列、实时预览、切换方式、场景及面板快捷键、音量显示集中于“设置”窗口，更改立即生效并随集合保存。
- 每个画面侧边可显示双声道音量电平，支持开关、左右位置和宽度设置。
- 安全模式：激活场景仅送入预监，默认空格确认转场；预监操作需要先开启 OBS 工作室模式。
- 直接模式：点击、激活快捷键或数字键直接切换正式输出。
- “设置预览、激活选中场景、预监上播”三个面板动作可独立设置、清空快捷键，支持单键或组合键。
- 右键画面编辑固定快捷键，录入时临时暂停 OBS 后台热键，保存时检查 OBS 已注册组合键冲突。
- 本页前九个场景批量绑定 Ctrl+Alt+1…9；发现冲突整批取消。
- 右键查看名称、数字位置与快捷键，可交换本页编号、设置分组、前移/后移；不修改 OBS 原场景列表顺序。
- 布局、分组、排序和固定快捷键保存在场景集合的插件配置中，以场景 UUID 绑定。
- 中文和英文界面，跟随 OBS 语言。

## 使用

Windows 构建产物 `dist/obs-scene-deck-0.4.0-windows-x64.zip` 包含标准插件目录：

```text
obs-scene-deck/
  bin/64bit/obs-scene-deck.dll
  data/locale/...
```

关闭 OBS 后，将整个 `obs-scene-deck` 文件夹放入 `%PROGRAMDATA%\obs-studio\plugins\`，再启动 OBS，在“停靠窗口 / Docks”菜单勾选“场景导播台”。此目录写入可能需要管理员权限。项目构建脚本仅生成安装包，不自动修改 OBS 安装目录。

Linux 将解压后的内层 `obs-scene-deck` 文件夹放入 `~/.config/obs-studio/plugins/`；macOS 将构建得到的 `obs-scene-deck.plugin` 放入 `~/Library/Application Support/obs-studio/plugins/`。每种操作系统需要对应的二进制包，不能混用 Windows DLL。

点击卡片可选择并激活该场景，同时将键盘焦点交给面板。点击搜索框或其他输入控件后，导航键不会抢占输入。

主面板保留搜索、分组筛选、“设置”入口、翻页和上播操作。行列和切换设置移入设置窗口；场景名称和本页编号直接叠加在画面左上角和右上角。右键画面仍可快速编辑该场景的快捷键、调整编号、设置分组与排序。编号调整会交换本页对应的两个场景位置，数字键仍按照当前页的位置选择，固定组合键始终跟随原场景。

设置窗口包含“画面布局、切换操作、快捷键、音量显示”四页；快捷键页可编辑聚焦面板、设置预览、激活选中场景和预监上播的按键，也可选择场景修改固定按键，或批量绑定当前页。“音量显示”可关闭电平条，或调整其左右位置和宽度。

音量条显示各场景可见音源的最大输出电平，包含嵌套场景并去重共享音源，静音源不计入；全局桌面/麦克风只有加入该场景时才计入。电平范围为 -60 至 0 dB，使用绿、黄、红标识。这是该场景音源的电平指示，并非最终混音总电平；关闭显示或隐藏面板时释放音频监听。

| 按键 | 行为 |
|---|---|
| 1–9 | 选择本页对应卡片；直接模式同时切换正式输出 |
| 左右方向键 | 按场景顺序连续选位，跨过页首/页尾自动翻页 |
| 上下方向键 | 按列移动，自动显示选中项所在页 |
| Enter（默认，可修改） | 激活选中场景，具体进入预监或正式输出由模式决定 |
| 未绑定（可自定义） | 设置预览：始终只改变预监，在直接模式下也不会切换正式输出 |
| Space（默认，可修改） | 将 **当前 OBS 预监场景** 按 OBS 当前转场上播 |
| PageUp / PageDown | 翻页 |
| Escape | 退出面板键盘焦点 |

数字键只有面板本身获得焦点时生效。固定快捷键通过 OBS 注册，可按 OBS 焦点策略在其他位置生效。固定快捷键至少包含一个修饰键；OBS 自带快捷键设置中仍可编辑这些动作。面板中的“保存”会替换该动作的全部绑定。

三个面板动作只在面板本身有焦点时执行，输入框和设置对话框中不会触发。清空后该动作停用，重新加载集合也不会恢复默认键。编辑器保留无修饰键的数字、方向、翻页、Tab 和 Escape 用于导航，并检测 OBS 已注册快捷键冲突。macOS 的 Command / Control / Option 使用对应平台的实际键位。

默认使用 **Ctrl+Alt+D** 唤出并聚焦面板；若已有 OBS 热键使用该组合，插件不覆盖它。可在 OBS“设置 → 快捷键 → 场景导播台：聚焦场景面板”中修改。

## 构建

通用要求为 CMake 3.28+、Ninja，以及与目标 OBS 匹配的 OBS / Qt 开发文件。以下为 Windows x64；Linux 和 macOS 命令见 [CROSS_PLATFORM.md](CROSS_PLATFORM.md)。

在 Visual Studio x64 Developer PowerShell 中运行：

```powershell
./scripts/prepare-sdk.ps1 -ObsVersion 33.0.1
./scripts/build.ps1 -ObsPath 'C:\Program Files\obs-studio'
```

`prepare-sdk.ps1` 从 OBS 官方仓库获取头文件，并按该版本 `CMakePresets.json` 中的 SHA256 校验官方 Qt SDK。下载和解压均位于 `.deps`。`build.ps1` 从已安装 OBS 的 DLL 导出表生成导入库，链接公开 API，不会重新构建 OBS；SDK 与运行版本必须匹配。

若 Ninja 不在 PATH，使用 `-Ninja '完整路径/ninja.exe'`。若 Qt、OBS 源码不在默认位置，使用 `-QtRoot`、`-ObsSource`。如果项目目录已准备 `.deps/tools/msvc/setup_x64.bat`，脚本可临时载入该便携工具链；不会修改全局环境变量。

也支持标准 OBS Development SDK：将 OBS SDK 和 Qt 加入 `CMAKE_PREFIX_PATH`，使用 CMake 配置构建即可。

核心逻辑测试无需 OBS / Qt：

```powershell
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

实际 libobs / D3D11 集成验证（使用模拟前端，不读取用户 OBS 配置）：

```powershell
./scripts/build.ps1 -RuntimeTests
./scripts/test-runtime.ps1
```

集成测试检查预监与正式输出隔离、数字选位、固定快捷键、重命名后绑定保持、配置恢复、集合切换、隐藏后停止渲染及再次显示恢复渲染。它仍不能替代在真实 OBS 界面中的安装验收。

## 实现与限制

- 渲染回调不访问 Qt 对象。显示销毁同步完成后，才释放场景与 showing 引用。
- 场景集合切换先释放旧场景，再加载新配置；使用代际标记丢弃过期快捷键回调。
- 可见预览可能使采集设备和浏览器源持续运行；它不会主动让所有媒体源播放，也不会把场景设为正式输出。
- 默认每页 3×2，最多 6×4；数字键仅覆盖前九个位置。大量复杂场景会增加 GPU/源处理负载，可关闭“实时预览”或减少行列。
- 快捷键冲突检查覆盖 OBS 已注册的绑定，不负责探测其他软件或系统保留的全局按键。
- 第一版不包含脚本和图形化自动化；后续可接入场景激活动作接口。
- Linux 的 X11 / Wayland、macOS 原生预览均有独立窗口适配；实际平台会话、设备、DPI 和发行版兼容性仍需要对应环境验收。已验证范围见 [VALIDATION.md](VALIDATION.md)。

参考：[OBS 插件模板](https://github.com/obsproject/obs-plugintemplate)、[Frontend API](https://docs.obsproject.com/reference-frontend-api)、[显示与源渲染](https://docs.obsproject.com/frontends#displays)、[Source Dock](https://github.com/exeldro/obs-source-dock)。

许可证：GPL-2.0-or-later，见 LICENSE。
