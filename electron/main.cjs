const {
  app,
  BrowserWindow,
  clipboard,
  dialog,
  Menu,
  nativeImage,
  shell,
  Tray,
} = require("electron");
const { spawn } = require("child_process");
const fs = require("fs");
const net = require("net");
const path = require("path");

const projectRoot = path.resolve(__dirname, "..");
const dataRoot = path.join(projectRoot, ".data");
const logPath = path.join(dataRoot, "electron.log");
const appIcon = path.join(projectRoot, "img", "img.ico");
const bootstrapScript = path.join(projectRoot, "scripts", "dsh.ps1");
const childProcessPatch = path.join(__dirname, "hide-child-process.cjs");
const dshCli = path.join(
  projectRoot,
  "node_modules",
  "@deepseek-ai",
  "dsh",
  "lib",
  "bin.js",
);

let harnessProcess = null;
let mainWindow = null;
let splashWindow = null;
let tray = null;
let harnessUrl = null;
let shuttingDown = false;
let quitting = false;

fs.mkdirSync(dataRoot, { recursive: true });
const logStream = fs.createWriteStream(logPath, { flags: "a" });

function log(message) {
  const line = `[${new Date().toISOString()}] ${message}\n`;
  logStream.write(line);
  process.stdout.write(line);
}

function fileExists(filePath) {
  try {
    return fs.statSync(filePath).isFile();
  } catch {
    return false;
  }
}

function createHarnessEnvironment() {
  const localNodeRoot = path.join(projectRoot, ".runtime", "node");
  const localBin = path.join(projectRoot, "node_modules", ".bin");
  const originalPath = process.env.PATH || "";

  return {
    ...process.env,
    PATH: [localNodeRoot, localBin, originalPath].filter(Boolean).join(path.delimiter),
    DSH_HOME: path.join(dataRoot, "dsh"),
    NPM_CONFIG_CACHE: path.join(projectRoot, ".cache", "npm"),
    NPM_CONFIG_PREFIX: path.join(projectRoot, ".runtime", "npm-global"),
    NPM_CONFIG_USERCONFIG: path.join(projectRoot, ".npmrc"),
    NPM_CONFIG_UPDATE_NOTIFIER: "false",
    ELECTRON_RUN_AS_NODE: "1",
  };
}

function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      const port = typeof address === "object" && address ? address.port : 0;
      server.close(() => resolve(port));
    });
  });
}

function runBootstrap() {
  const powershell = path.join(
    process.env.SystemRoot || "C:\\Windows",
    "System32",
    "WindowsPowerShell",
    "v1.0",
    "powershell.exe",
  );

  log("Harness dependencies are missing; running bootstrap.");
  const result = spawn(
    powershell,
    [
      "-NoLogo",
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      bootstrapScript,
      "-InstallOnly",
    ],
    {
      cwd: projectRoot,
      env: createHarnessEnvironment(),
      stdio: "inherit",
      windowsHide: true,
    },
  );

  return new Promise((resolve, reject) => {
    result.once("error", reject);
    result.once("exit", (code) => {
      if (code === 0 && fileExists(dshCli)) {
        resolve();
        return;
      }
      reject(new Error(`Bootstrap exited with code ${code}.`));
    });
  });
}

function createSplashWindow() {
  log("Creating splash window.");
  splashWindow = new BrowserWindow({
    width: 520,
    height: 300,
    frame: false,
    autoHideMenuBar: true,
    resizable: false,
    show: true,
    backgroundColor: "#101218",
    icon: appIcon,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  splashWindow.webContents.once("did-finish-load", () => {
    log("Splash window loaded.");
  });
  splashWindow.loadFile(path.join(__dirname, "loading.html"));
}

function closeSplash() {
  if (splashWindow && !splashWindow.isDestroyed()) {
    splashWindow.close();
  }
  splashWindow = null;
}

function createMainWindow(url) {
  log(`Creating main window for ${url}.`);
  harnessUrl = url;
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 700,
    minWidth: 960,
    minHeight: 600,
    autoHideMenuBar: true,
    show: false,
    backgroundColor: "#101218",
    icon: appIcon,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  mainWindow.webContents.setWindowOpenHandler(({ url: targetUrl }) => {
    if (/^https?:/i.test(targetUrl) && !targetUrl.startsWith("http://127.0.0.1:")) {
      shell.openExternal(targetUrl);
    }
    return { action: "deny" };
  });

  mainWindow.once("ready-to-show", () => {
    log("Main window is ready to show.");
    closeSplash();
    showMainWindow();
  });

  mainWindow.webContents.once("did-finish-load", () => {
    log("Harness page loaded.");
    closeSplash();
    if (!mainWindow.isVisible()) {
      showMainWindow();
    }
  });

  mainWindow.webContents.on("did-fail-load", (_event, code, description) => {
    log(`Harness page failed to load: ${code} ${description}`);
  });

  mainWindow.on("closed", () => {
    mainWindow = null;
  });

  mainWindow.on("minimize", (event) => {
    event.preventDefault();
    hideMainWindow();
  });

  mainWindow.on("close", (event) => {
    if (!quitting) {
      event.preventDefault();
      hideMainWindow();
    }
  });

  mainWindow.on("show", rebuildTrayMenu);
  mainWindow.on("hide", rebuildTrayMenu);

  mainWindow.loadURL(url).catch((error) => {
    log(`loadURL failed: ${error.stack || error.message}`);
  });
}

function showMainWindow() {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  if (mainWindow.isMinimized()) {
    mainWindow.restore();
  }
  mainWindow.show();
  mainWindow.focus();
  rebuildTrayMenu();
}

function hideMainWindow() {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  mainWindow.hide();
  rebuildTrayMenu();
}

function openHarnessInBrowser() {
  if (harnessUrl) {
    shell.openExternal(harnessUrl);
  }
}

function copyHarnessUrl() {
  if (harnessUrl) {
    clipboard.writeText(harnessUrl);
  }
}

function buildTrayMenu() {
  const windowVisible = Boolean(mainWindow && !mainWindow.isDestroyed() && mainWindow.isVisible());
  return Menu.buildFromTemplate([
    {
      label: "显示主窗口",
      enabled: Boolean(mainWindow) && !windowVisible,
      click: showMainWindow,
    },
    {
      label: "隐藏到托盘",
      enabled: windowVisible,
      click: hideMainWindow,
    },
    { type: "separator" },
    {
      label: "在浏览器中打开",
      enabled: Boolean(harnessUrl),
      click: openHarnessInBrowser,
    },
    {
      label: "复制本地地址",
      enabled: Boolean(harnessUrl),
      click: copyHarnessUrl,
    },
    { type: "separator" },
    {
      label: "退出",
      click: () => {
        quitting = true;
        app.quit();
      },
    },
  ]);
}

function rebuildTrayMenu() {
  if (tray) {
    tray.setContextMenu(buildTrayMenu());
  }
}

function createTray() {
  const image = nativeImage.createFromPath(appIcon);
  if (image.isEmpty()) {
    log(`Tray icon could not be loaded: ${appIcon}`);
    return;
  }

  tray = new Tray(image);
  tray.setToolTip("DeepSeek Harness");
  tray.setContextMenu(buildTrayMenu());
  tray.on("click", showMainWindow);
  tray.on("double-click", showMainWindow);
  log("Tray icon created.");
}

function showStartupError(error) {
  const message = error instanceof Error ? error.stack || error.message : String(error);
  log(`Startup failed: ${message}`);
  closeSplash();
  dialog.showErrorBox("DeepSeek Harness failed to start", message);
  app.quit();
}

async function startHarness() {
  if (!fileExists(dshCli)) {
    await runBootstrap();
  }

  const rawArguments = process.argv.slice(app.isPackaged ? 1 : 2);
  const extraArguments = [];
  let requestedPort = null;

  for (let index = 0; index < rawArguments.length; index += 1) {
    const value = rawArguments[index];
    if (value === "web" || value === "--no-open" || value === "." || value.endsWith("main.cjs")) {
      continue;
    }
    if (value === "--port" && /^\d+$/.test(rawArguments[index + 1] || "")) {
      requestedPort = Number(rawArguments[index + 1]);
      index += 1;
      continue;
    }
    if (value === "--host") {
      index += 1;
      continue;
    }
    extraArguments.push(value);
  }

  const port = requestedPort || await findFreePort();

  harnessProcess = spawn(
    process.execPath,
    [
      "--expose-internals",
      "--require",
      childProcessPatch,
      dshCli,
      "web",
      "--no-open",
      "--host",
      "127.0.0.1",
      "--port",
      String(port),
      ...extraArguments,
    ],
    {
      cwd: projectRoot,
      env: createHarnessEnvironment(),
      stdio: ["ignore", "pipe", "pipe"],
      windowsHide: true,
    },
  );

  let output = "";
  let resolvedUrl = false;

  const readOutput = (chunk, streamName) => {
    const text = chunk.toString();
    output += text;
    for (const line of text.split(/\r?\n/)) {
      if (line) {
        log(`${streamName}: ${line}`);
      }
    }

    const match = output.match(
      /(http:\/\/127\.0\.0\.1:\d+\/\?token=[A-Za-z0-9_-]+)/,
    );
    if (match && !resolvedUrl) {
      resolvedUrl = true;
      createMainWindow(match[1]);
    }
  };

  harnessProcess.stdout.on("data", (chunk) => readOutput(chunk, "dsh"));
  harnessProcess.stderr.on("data", (chunk) => readOutput(chunk, "dsh:err"));

  harnessProcess.once("error", (error) => {
    if (!resolvedUrl) {
      showStartupError(error);
    }
  });

  harnessProcess.once("exit", (code) => {
    harnessProcess = null;
    if (!shuttingDown && code !== 0) {
      showStartupError(new Error(`Harness exited with code ${code}.`));
    }
  });
}

function stopHarness() {
  if (!harnessProcess || harnessProcess.killed) {
    return;
  }

  shuttingDown = true;
  const processId = harnessProcess.pid;
  harnessProcess.kill();

  if (process.platform === "win32" && processId) {
    spawn("taskkill", ["/pid", String(processId), "/t", "/f"], {
      windowsHide: true,
      stdio: "ignore",
    }).unref();
  }
}

function destroyTray() {
  if (tray) {
    tray.destroy();
    tray = null;
  }
}

const singleInstance = app.requestSingleInstanceLock();
if (!singleInstance) {
  app.quit();
} else {
  app.on("second-instance", () => {
    showMainWindow();
  });

  app.whenReady().then(async () => {
    log("Electron is ready.");
    Menu.setApplicationMenu(null);
    app.setAppUserModelId("com.deepseek.harness.local");
    createTray();
    createSplashWindow();
    try {
      await startHarness();
    } catch (error) {
      showStartupError(error);
    }
  });

  app.on("before-quit", () => {
    quitting = true;
    stopHarness();
    destroyTray();
  });

  app.on("window-all-closed", () => {
    if (quitting || !tray) {
      app.quit();
    }
  });

  app.on("activate", () => {
    showMainWindow();
  });
}
