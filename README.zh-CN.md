# DeepSeek Harness Electron 桌面启动器

最终启动入口是 `bin\deepseek-harness.exe`。该 C++ 启动器负责准备运行环境，随后启动 Electron 桌面应用；Electron 主进程使用自带 Node.js 启动 Harness，并在原生窗口中加载本地 Web UI。

## 运行结构

```text
bin\deepseek-harness.exe
  -> node_modules\electron\dist\electron.exe
     -> electron\main.cjs
        -> Electron Node.js 24.21.0
           -> node_modules\@deepseek-ai\dsh\lib\bin.js web
              -> BrowserWindow.loadURL(http://127.0.0.1:<随机端口>)
```

Electron 44.4.3 内置 Node.js `24.21.0`，满足 Harness 的 Node.js 版本要求。启动 Harness 时会附加 `--expose-internals`，以启用 Web profile 的 HMR 插件。

## 构建

构建 C++ 启动器需要 CMake、Ninja 和 MinGW g++：

```powershell
.\build.cmd
```

输出文件：

```text
bin\deepseek-harness.exe
```

首次冷启动会通过 `scripts\dsh.ps1 -InstallOnly` 准备依赖：

1. 从 Node.js 官方地址下载便携 Node.js。
2. 官方地址不可达时自动改用 `npmmirror`。
3. 通过 `registry.npmmirror.com` 执行项目内 `npm ci`。
4. Electron 二进制通过 `https://npmmirror.com/mirrors/electron/` 下载。

C++ 启动器使用 `CreateProcessW` 创建子进程专用环境块：

```text
PATH                   .runtime\node 与项目 node_modules\.bin 优先
DSH_HOME              .data\dsh
NPM_CONFIG_CACHE      .cache\npm
NPM_CONFIG_PREFIX     .runtime\npm-global
NPM_CONFIG_USERCONFIG .npmrc
```

这些值不会写入当前终端、用户配置或系统配置。

## 启动

双击：

```text
bin\deepseek-harness.exe
```

也可以运行：

```powershell
.\start.cmd
```

桌面窗口启动期间会先显示加载页。Harness 服务就绪后，Electron 自动解析带 token 的本地 URL，并通过 `BrowserWindow.loadURL()` 加载。

主窗口配置：

```text
初始大小: 1200 x 700
最小大小: 960 x 600
窗口图标: img\img.ico
托盘图标: img\img.ico
```

点击关闭按钮或最小化按钮时，窗口会隐藏到系统托盘，Harness 服务继续运行。托盘菜单提供：

- 显示主窗口
- 隐藏到托盘
- 在浏览器中打开
- 复制本地地址
- 退出

只有托盘菜单中的“退出”会真正结束 Electron 和 Harness 进程。

指定端口：

```powershell
.\start.cmd --port 3081
```

## CLI 模式

保留无界面 CLI 入口：

```powershell
.\dsh.cmd web --no-open
.\dsh.cmd --profile headless "summarize this workspace"
.\bin\deepseek-harness.exe --cli --version
```

如使用真实模型，将 `.env.example` 复制为 `.env` 并填写 `DEEPSEEK_API_KEY`。

## Electron 配置

`.npmrc` 已配置：

```ini
registry=https://registry.npmmirror.com/
```

Electron 二进制镜像由 `scripts\dsh.ps1` 在安装阶段通过临时环境变量 `ELECTRON_MIRROR` 指定，不使用全局环境变量。

Electron 主进程位于 `electron\main.cjs`，负责：

- 检测并补齐 Harness 依赖。
- 选择空闲的本地回环端口。
- 使用 Electron 内置 Node.js 启动 `dsh web`。
- 解析服务输出的 URL 和 token。
- 创建安全的 `BrowserWindow`，禁用页面侧 Node.js 集成。
- 在应用退出时终止 Harness 进程。

## WinTools 辅助层

`CMakeLists.txt` 默认读取：

```text
D:\WinTools1.12\mingwVersion
```

构建时会生成 `bin\dsh-wintools-helper.exe`，目前通过 WinTools 的 `FileManagement` API 提供项目内目录递归清理。Node.js 安装流程优先调用该辅助程序，失败时回退到 PowerShell。

WinTools 1.12 不提供 HTTP 下载接口，因此 Node.js、Electron 和 npm 依赖下载不由 WinTools 承担；Electron 使用 npmmirror，Node.js 使用官方源并在失败时回退到 npmmirror。

## 移植

完整离线包需要包含：

```text
bin\deepseek-harness.exe
electron
node_modules
package-lock.json
scripts\dsh.ps1
```

Electron 的 Windows x64 二进制不能直接复用到 Linux、macOS 或 Windows ARM64。
