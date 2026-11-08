# 跨平台构建与安装

0.4.0 使用同一套 C++17 / Qt Widgets 源码，在每个平台分别编译 OBS 原生插件。最低源码接口要求是 OBS 30、Qt 6.4、CMake 3.28；实际依赖必须匹配目标 OBS 的 Qt 主版本、架构和平台 ABI。

| 平台 | 原生显示 | 打包 | 当前验证 |
|---|---|---|---|
| Windows x64 | HWND / OBS D3D11 | ZIP，DLL + data | OBS 33.0.1、Qt 6.11.1，编译及运行时集成测试通过 |
| Linux x86_64 | OBS X11 EGL / Wayland 窗口 | TAR.GZ，SO + data | WSL Ubuntu 26.04.1、OBS SDK 32.1.0、Qt 6.10.2，编译及核心测试通过 |
| macOS Apple Silicon | NSView / OBS 原生显示 | ZIP，`.plugin` bundle | 适配和 CI 已配置，未在 macOS 编译运行 |
| macOS Intel | NSView / OBS 原生显示 | ZIP，`.plugin` bundle | 适配和 CI 已配置，未在 macOS 编译运行 |

源码接口兼容范围不代表一个二进制能跨所有 OBS 版本使用。建议针对目标 OBS 版本及其 Qt 构建；Linux 还受 glibc、libstdc++ 和发行版包版本约束。本机 Ubuntu 26.04 构建产物不承诺可运行于 Ubuntu 24.04。其他 CPU 架构可以使用通用 CMake 构建，但尚未测试。

## Windows

安装 Visual Studio 2022 的“使用 C++ 的桌面开发”（MSVC x64、Windows SDK）、Git、CMake、Ninja 和目标 OBS。在 x64 Developer PowerShell 中运行：

```powershell
./scripts/prepare-sdk.ps1 -ObsVersion 33.0.1
./scripts/build.ps1 -ObsPath 'C:\Program Files\obs-studio'
```

脚本获取匹配版本的公开 OBS 头文件，校验官方 Qt SDK 的 SHA256，并从目标 OBS DLL 生成导入库。产物是 `dist/obs-scene-deck-0.4.0-windows-x64.zip`。自定义 SDK 和 Ninja 路径可使用 `-ObsSource`、`-QtRoot`、`-Ninja`。

关闭 OBS，将 ZIP 中的 `obs-scene-deck` 目录复制到 `%PROGRAMDATA%\obs-studio\plugins\`。标准目录中的 `bin/64bit/obs-scene-deck.dll` 与 `data/locale` 必须一同保留。

## Linux

Ubuntu 24.04+ 的发行版 OBS / Qt 开发包示例：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libobs-dev qt6-base-dev libgl1-mesa-dev libx11-dev
bash scripts/build-unix.sh
```

从 `dist/obs-scene-deck-0.4.0-Linux-x86_64.tar.gz` 解压，关闭 OBS，将内层 `obs-scene-deck` 目录复制到 `~/.config/obs-studio/plugins/`。归档会多包含一个带版本号的外层目录。

若 OBS 来自 PPA 或自编译，开发包也应来自同一套构建；可通过 `CMAKE_PREFIX_PATH` 或 CMake 参数指定 SDK：

```bash
bash scripts/build-unix.sh -DCMAKE_PREFIX_PATH=/path/to/obs-and-qt-sdk
```

发行版系统安装布局另提供 `SCENE_DECK_SYSTEM_INSTALL=ON`，把 SO 和资源分别安装到前缀下的库目录和共享数据目录：

```bash
bash scripts/build-unix.sh -DSCENE_DECK_SYSTEM_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr
sudo cmake --install build/unix
```

Flatpak 的隔离依赖和扩展安装方式不同，当前脚本面向原生 OBS；不将原生包视为 Flatpak 扩展包。X11 / Wayland 预览复用 OBS 已有的平台与显示连接，不另开显示连接。

## macOS

需要 Xcode Command Line Tools、Git、CMake、Ninja、Python 3.12+，以及官方 OBS.app。Homebrew 示例：

```bash
xcode-select --install
brew install cmake ninja python@3.12
brew install --cask obs
python3.12 scripts/prepare-macos-sdk.py --obs-app /Applications/OBS.app
bash scripts/build-unix.sh
```

准备脚本从 OBS.app 读取版本，获取同一 tag 的 OBS 公开头文件，并按该版本声明下载、校验官方 universal Qt SDK，生成 `.deps/macos-sdk.cmake`。构建使用本机架构，Apple Silicon 为 arm64，Intel 为 x86_64；分别产生对应 ZIP，不将它们标为 universal 插件。

关闭 OBS，将 ZIP 中的 `obs-scene-deck.plugin` 复制到：

```text
~/Library/Application Support/obs-studio/plugins/
```

Bundle 包含动态模块、Info.plist 及 `Contents/Resources/locale`。构建直接链接 OBS.app 的 libobs framework 与 frontend API，运行时从宿主 Frameworks 解析依赖。当前脚本生成开发构建，未配置 Developer ID 发布签名与公证；正式分发时需要在 macOS 完成签名、公证和安装验收。

已有独立 OBS / Qt Development SDK 时，可以不运行准备脚本，直接传递 `-DCMAKE_PREFIX_PATH=...` 给 `build-unix.sh`。SDK 必须同时提供 libobs 和 obs-frontend-api。

## CI 与检查

`.github/workflows/build.yml` 定义 Windows x64、Ubuntu 24.04、macOS 15 ARM / Intel 四种构建任务，分别编译、运行核心测试并上传安装包。当前目录未配置远程仓库，工作流文件尚未在 GitHub Actions 实际执行。macOS 暂不能视为已验证发布版本。

核心逻辑测试可不依赖 OBS / Qt 单独运行：

```bash
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

Windows 另有使用真实 libobs / D3D11、模拟 frontend 的运行时测试：

```powershell
./scripts/build.ps1 -RuntimeTests
./scripts/test-runtime.ps1
```

安装后从 OBS“停靠窗口 / Docks → 场景导播台”打开。平台验收应检查实时画面、中文叠字、红绿框、音量条、快捷键、分页、隐藏恢复，以及切换场景集合和退出 OBS；macOS 还需分别检查 Command / Control，Linux 需分别检查 X11 与 Wayland。
