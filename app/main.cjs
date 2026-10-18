'use strict';
const { APP_LOCALE, t, localizedError } = require('./backend/i18n.cjs');

const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const { app, BrowserWindow, Menu, Tray, nativeImage, ipcMain, shell, screen, session, powerMonitor } = require('electron');
const { SettingsStore } = require('./backend/settings-store.cjs');
const { CodexProvider } = require('./backend/codex-provider.cjs');
const { ClaudeClient } = require('./backend/claude-client.cjs');
const { CoreBridge } = require('./backend/core-bridge.cjs');
const { selectEnergyTarget } = require('./backend/energy-selection.cjs');
const { RefreshScheduler } = require('./backend/refresh-scheduler.cjs');
const { WidgetPosition } = require('./backend/widget-position.cjs');
const { WIDGET_MIN_WIDTH, WIDGET_MIN_HEIGHT, widgetDimensions, validateShapeRects,
  scaleShapeRects } = require('./backend/window-shape.cjs');

const APP_DIRECTORY = __dirname;
const ENTRY_FILE = path.join(APP_DIRECTORY, 'ui', 'index.html');
const ENTRY_URL = pathToFileURL(ENTRY_FILE).href;
const SETTINGS_FILE = path.join(APP_DIRECTORY, 'ui', 'settings.html');
const SETTINGS_URL = pathToFileURL(SETTINGS_FILE).href;
const RUNTIME_DIRECTORY = path.join(APP_DIRECTORY, 'runtime');
const PROVIDER_IDS = new Set(['codex', 'claude']);
const EXTERNAL_HOSTS = new Set(['auth.openai.com', 'auth0.openai.com', 'chatgpt.com',
  'claude.ai', 'code.claude.com', 'support.claude.com']);

function cliArgument(name) {
  const index = process.argv.indexOf(name);
  return index < 0 ? null : process.argv[index + 1];
}
const testDataDirectory = cliArgument('--test-data-dir');
const noProviders = process.argv.includes('--no-providers');
const testMode = Boolean(testDataDirectory || noProviders);
const localAppData = process.env.LOCALAPPDATA || app.getPath('appData');
const dataDirectory = testDataDirectory ? path.resolve(testDataDirectory) : path.join(localAppData, 'TigaEnergyMonitorV2');
fs.mkdirSync(dataDirectory, { recursive: true });
app.setPath('userData', dataDirectory);
app.setAppUserModelId('TigaEnergyMonitorV2');

const settingsStore = new SettingsStore({ dataDir: dataDirectory,
  legacyPath: testMode ? null : path.join(localAppData, 'ChatGPTCodexUsageMonitor', 'settings.json') });
let settings = settingsStore.load();
let mainWindow = null;
let settingsWindow = null;
let tray = null;
let quitting = false;
let refreshScheduler = null;
let refreshOperation = null;
let expiryTimer = null;
let positionTimer = null;
let previewScene = null;
// null keeps the initial native window rectangular until its artwork is ready.
let shapeRects = null;
const widgetPosition = new WidgetPosition(dataDirectory);
let snapshotRevision = 0;
let settingsQueue = Promise.resolve();
const unknownEnergy = () => ({ scene: 'unknown', visual: 'unavailable', fullCycleMs: 0, illuminated: false });
let energy = unknownEnergy();
let energySignature = null;
let classifiedEnergySignature = null;
let pendingEnergyClassification = null;
const emptyProvider = id => ({ id, status: 'signed_out', account: null, plan: null,
  windows: [], updatedAt: null, error: null });
const states = { codex: emptyProvider('codex'), claude: emptyProvider('claude') };
let energyTarget = selectEnergyTarget(states, settings);
const providers = {};
const core = new CoreBridge({ executable: path.join(RUNTIME_DIRECTORY, 'monitor-core.exe') });

function safeExternalUrl(value) {
  try {
    const url = new URL(value);
    return url.protocol === 'https:' && EXTERNAL_HOSTS.has(url.hostname) &&
      !url.username && !url.password && (!url.port || url.port === '443');
  } catch { return false; }
}

async function openExternal(url) {
  if (!safeExternalUrl(url)) throw new Error(t('链接未通过官方地址检查。'));
  await shell.openExternal(url);
}

function getSnapshot() {
  synchronizeEnergyTarget();
  return structuredClone({ providers: states, settings, energy, energyTarget, previewScene, locale: APP_LOCALE, version: app.getVersion() });
}

function sendSnapshot() {
  const snapshot = getSnapshot();
  for (const window of [mainWindow, settingsWindow]) {
    if (window && !window.isDestroyed() && !window.webContents.isDestroyed()) {
      window.webContents.send('monitor:snapshot', snapshot);
    }
  }
}

function synchronizeEnergyTarget() {
  const target = selectEnergyTarget(states, settings);
  const signature = JSON.stringify([target, settings.warningThreshold]);
  if (signature !== energySignature) {
    snapshotRevision += 1;
    energySignature = signature;
    classifiedEnergySignature = null;
    energy = unknownEnergy();
  }
  energyTarget = target;
}

function updateEnergy() {
  if (quitting) return Promise.resolve();
  // Publish providers, settings and their newly computed target together. The
  // native result is allowed to lag, but must never describe an older target.
  sendSnapshot();
  const signature = energySignature;
  if (classifiedEnergySignature === signature) return Promise.resolve();
  if (pendingEnergyClassification?.signature === signature) return pendingEnergyClassification.promise;
  const request = { signature, revision: snapshotRevision, remainingPercent: energyTarget.remainingPercent,
    warningThreshold: settings.warningThreshold, promise: null };
  const operation = (async () => {
    let next;
    let classified = true;
    try { next = await core.classifyEnergy(request.remainingPercent, request.warningThreshold); }
    catch { next = unknownEnergy(); classified = false; }
    if (quitting) return;
    // Recheck freshness as well as mutations: a reset can elapse while the
    // native classifier is busy even without a new provider notification.
    synchronizeEnergyTarget();
    if (request.revision !== snapshotRevision || request.signature !== energySignature) {
      updateEnergy().catch(() => {});
      return;
    }
    energy = next;
    classifiedEnergySignature = classified ? signature : null;
    sendSnapshot();
  })();
  request.promise = operation.finally(() => {
    if (pendingEnergyClassification === request) pendingEnergyClassification = null;
  });
  pendingEnergyClassification = request;
  return request.promise;
}

function providerUpdate(id, state) {
  if (!state || state.id !== id) return;
  states[id] = state;
  refreshScheduler?.update();
  updateEnergy().catch(() => {});
}

function performRefreshAll() {
  if (quitting) return Promise.resolve();
  if (refreshOperation) return refreshOperation;
  refreshOperation = (async () => {
    const results = noProviders ? [] : await Promise.allSettled(
      Object.values(providers).map(provider => provider.refresh()));
    await updateEnergy();
    // Providers normally publish their own safe error state. Unexpected rejected
    // operations still need scheduler backoff, without forwarding raw diagnostics.
    if (results.some(result => result.status === 'rejected')) throw new Error(t('操作未能完成，请重试。'));
    return getSnapshot();
  })().finally(() => { refreshOperation = null; });
  return refreshOperation;
}

function refreshAll() {
  return refreshScheduler ? refreshScheduler.refreshNow() : performRefreshAll();
}

function scheduleRefresh() {
  if (noProviders || quitting) return;
  if (!refreshScheduler) {
    refreshScheduler = new RefreshScheduler({ refresh: performRefreshAll,
      getSettings: () => settings, getStates: () => states });
    refreshScheduler.start();
  } else refreshScheduler.update();
}

function resumeRefresh() {
  if (refreshScheduler) refreshScheduler.resume().catch(() => {});
}

function constrainBounds(bounds) {
  const workArea = screen.getDisplayMatching(bounds).workArea;
  const width = Math.min(bounds.width, workArea.width);
  const height = Math.min(bounds.height, workArea.height);
  return { width, height,
    x: Math.max(workArea.x, Math.min(bounds.x, workArea.x + workArea.width - width)),
    y: Math.max(workArea.y, Math.min(bounds.y, workArea.y + workArea.height - height)) };
}

function applyWindowShape() {
  if (!mainWindow || mainWindow.isDestroyed() || shapeRects === null) return;
  mainWindow.setShape(scaleShapeRects(shapeRects, settings.floatingScale));
}

function applyWindowSettings(previous = {}) {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  mainWindow.setAlwaysOnTop(settings.alwaysOnTop, 'floating');
  if (settingsWindow && !settingsWindow.isDestroyed()) settingsWindow.setAlwaysOnTop(settings.alwaysOnTop, 'floating');
  if (previous.floatingScale !== settings.floatingScale) {
    const current = mainWindow.getBounds();
    const factor = settings.floatingScale / 100;
    mainWindow.setBounds(constrainBounds({ ...current, ...widgetDimensions(settings.floatingScale) }));
    mainWindow.webContents.setZoomFactor(factor);
    applyWindowShape();
  }
  updateTray();
}

async function configureWatcher(nextSettings = settings) {
  const modulePath = path.join(APP_DIRECTORY, 'backend', 'launch-watcher.cjs');
  if (!fs.existsSync(modulePath)) {
    if (nextSettings.launchWithCodex || nextSettings.launchWithClaude) throw new Error(t('自动唤起组件未就绪。'));
    return;
  }
  const { configureLaunchWatcher } = require(modulePath);
  await configureLaunchWatcher({ settings: nextSettings, settingsPath: settingsStore.settingsPath,
    appExecutable: process.execPath, watcherExecutable: path.join(RUNTIME_DIRECTORY, 'tiga-launch-watcher.exe'),
    appDirectory: path.dirname(process.execPath), testMode: testMode || !app.isPackaged });
}

async function persistSettingsPatch(patch) {
  const previous = settings;
  const nextSettings = settingsStore.update(patch);
  try { await configureWatcher(nextSettings); }
  catch (error) {
    settingsStore.update(previous);
    throw new Error(localizedError(error, '自动唤起设置未能保存。'));
  }
  settings = nextSettings;
  applyWindowSettings(previous);
  refreshScheduler?.update();
  await updateEnergy();
  return getSnapshot();
}

function updateSettings(patch) {
  // Keep persistence, watcher registration and rollback in one ordered change.
  // Fast toggles must not leave an older registry operation running last.
  const operation = settingsQueue.then(() => persistSettingsPatch(patch));
  settingsQueue = operation.catch(() => {});
  return operation;
}

function gemIcon() {
  const iconPath = path.join(APP_DIRECTORY, 'ui', 'assets', 'app-icon.png');
  if (fs.existsSync(iconPath)) {
    const icon = nativeImage.createFromPath(iconPath);
    if (!icon.isEmpty()) return icon.resize({ width: 32, height: 32 });
  }
  const size = 32;
  const pixels = Buffer.alloc(size * size * 4);
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const diamond = Math.abs((x - 15.5) / 11) + Math.abs((y - 15.5) / 15);
      if (diamond > 1) continue;
      const offset = (y * size + x) * 4;
      const light = 1 - diamond;
      pixels[offset] = 245;
      pixels[offset + 1] = Math.round(140 + light * 100);
      pixels[offset + 2] = Math.round(36 + light * 168);
      pixels[offset + 3] = 255;
    }
  }
  return nativeImage.createFromBitmap(pixels, { width: size, height: size, scaleFactor: 1 });
}

function showWindow() {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.showInactive();
}

function updateTray() {
  if (!tray) return;
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: t('打开额度显示器'), click: showWindow },
    { label: t('账号与设置'), click: () => showSettingsWindow() },
    { label: t('始终置顶'), type: 'checkbox', checked: settings.alwaysOnTop,
      click: () => { updateSettings({ alwaysOnTop: !settings.alwaysOnTop }).catch(() => {}); } },
    { type: 'separator' },
    { label: t('退出额度显示器'), click: () => app.quit() },
  ]));
}

function validSender(event) {
  for (const [window, url] of [[mainWindow, ENTRY_URL], [settingsWindow, SETTINGS_URL]]) {
    if (!window || window.isDestroyed() || event.sender !== window.webContents) continue;
    const frame = event.senderFrame;
    return Boolean(frame && frame === window.webContents.mainFrame && frame.url.split('#')[0] === url);
  }
  return false;
}

function registerIpc() {
  const handle = (name, action) => ipcMain.handle(`monitor:${name}`, async (event, ...arguments_) => {
    if (!validSender(event)) throw new Error(t('此窗口不能执行该操作。'));
    try { return await action(...arguments_, event); }
    catch (error) { throw new Error(localizedError(error)); }
  });
  const provider = id => {
    if (!PROVIDER_IDS.has(id)) throw new Error(t('账号类型无效。'));
    if (noProviders) throw new Error(t('预览测试中未启用账号服务。'));
    return providers[id];
  };
  handle('getSnapshot', async () => { await updateEnergy(); return getSnapshot(); });
  handle('login', async id => {
    const selected = provider(id);
    // Login completion arrives through subscription events; do not hold the UI.
    selected.login().catch(() => {});
    return getSnapshot();
  });
  handle('logout', async id => { await provider(id).logout(); await updateEnergy(); return getSnapshot(); });
  handle('refresh', refreshAll);
  handle('updateSettings', updateSettings);
  handle('updateShape', async (rects, event) => {
    if (event.sender !== mainWindow.webContents) throw new Error(t('只有悬浮窗能更新点击区域。'));
    const next = validateShapeRects(rects);
    mainWindow.setShape(scaleShapeRects(next, settings.floatingScale));
    shapeRects = next;
    return { applied: true, rectangles: next.length };
  });
  handle('openUsage', async id => {
    if (!PROVIDER_IDS.has(id)) throw new Error(t('账号类型无效。'));
    await openExternal(id === 'codex' ? 'https://chatgpt.com/codex/settings/usage' : 'https://claude.ai/settings/usage');
    return getSnapshot();
  });
  handle('completeClaudeSetup', async () => {
    await provider('claude').openSetup();
    return getSnapshot();
  });
  handle('windowAction', async (action, event) => {
    if (!['minimize', 'close', 'compact', 'pin', 'settings', 'preview-full', 'preview-low', 'preview-stone', 'preview-auto'].includes(action)) throw new Error(t('窗口操作无效。'));
    const senderWindow = BrowserWindow.fromWebContents(event.sender);
    if (action === 'minimize') senderWindow.minimize();
    if (action === 'close') {
      if (senderWindow === settingsWindow) senderWindow.close();
      else mainWindow.hide();
    }
    if (action === 'compact') await updateSettings({ floatingScale: settings.floatingScale === 100 ? 125 : 100 });
    if (action === 'pin') await updateSettings({ alwaysOnTop: !settings.alwaysOnTop });
    if (action === 'settings') showSettingsWindow();
    if (action.startsWith('preview-')) {
      previewScene = action === 'preview-auto' ? null : action.slice('preview-'.length);
      sendSnapshot();
    }
    return getSnapshot();
  });
}

function createWindow() {
  const factor = settings.floatingScale / 100;
  const initial = widgetDimensions(settings.floatingScale);
  const workArea = screen.getPrimaryDisplay().workArea;
  const savedPosition = widgetPosition.read();
  const initialBounds = constrainBounds({ ...initial,
    x: savedPosition?.x ?? workArea.x + Math.max(0, workArea.width - initial.width - 24),
    y: savedPosition?.y ?? workArea.y + Math.max(0, workArea.height - initial.height - 24) });
  mainWindow = new BrowserWindow({ ...initialBounds, minWidth: Math.min(WIDGET_MIN_WIDTH, workArea.width),
    minHeight: Math.min(WIDGET_MIN_HEIGHT, workArea.height), show: false, frame: false, title: t('迪迦额度显示器'),
    resizable: false, maximizable: false, skipTaskbar: true,
    transparent: true, backgroundColor: '#00000000', hasShadow: false,
    icon: fs.existsSync(path.join(APP_DIRECTORY, 'ui', 'assets', 'app-icon.ico'))
      ? path.join(APP_DIRECTORY, 'ui', 'assets', 'app-icon.ico') : gemIcon(), autoHideMenuBar: true,
    webPreferences: { preload: path.join(APP_DIRECTORY, 'preload.cjs'), contextIsolation: true, partition: 'tiga-widget',
      additionalArguments: [`--tiga-locale=${APP_LOCALE}`],
      sandbox: true, nodeIntegration: false, webSecurity: true,
      allowRunningInsecureContent: false, spellcheck: false },
  });
  mainWindow.setMenu(null);
  mainWindow.setAlwaysOnTop(settings.alwaysOnTop, 'floating');
  mainWindow.webContents.setZoomFactor(factor);
  protectWindow(mainWindow, ENTRY_URL);
  mainWindow.webContents.on('did-finish-load', () => mainWindow.webContents.setZoomFactor(settings.floatingScale / 100));
  mainWindow.once('ready-to-show', () => {
    // Win32 can round initial scaled bounds differently from setBounds. Apply
    // the saved size once after initialization, before the widget is visible.
    applyWindowSettings();
    showWindow();
    sendSnapshot();
  });
  mainWindow.on('close', event => {
    if (!quitting) { event.preventDefault(); mainWindow.hide(); }
  });
  mainWindow.on('move', () => {
    clearTimeout(positionTimer);
    positionTimer = setTimeout(saveWidgetPosition, 200);
  });
  mainWindow.on('resize', applyWindowShape);
  // Caption messages come only from app-region drag areas. Buttons and links
  // use no-drag, so their clicks never hide the widget or open settings.
  if (process.platform === 'win32') {
    mainWindow.hookWindowMessage(0x00A3, () => mainWindow.hide());
    mainWindow.hookWindowMessage(0x00A5, () => showSettingsWindow());
  }
  mainWindow.loadFile(ENTRY_FILE);
}

function saveWidgetPosition() {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  const [x, y] = mainWindow.getPosition();
  try { widgetPosition.save({ x, y }); } catch {}
}

function protectWindow(window, permittedUrl) {
  window.webContents.session.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
  window.webContents.session.setPermissionCheckHandler(() => false);
  window.webContents.setWindowOpenHandler(({ url }) => {
    if (safeExternalUrl(url)) openExternal(url).catch(() => {});
    return { action: 'deny' };
  });
  window.webContents.on('will-navigate', (event, url) => {
    if (url.split('#')[0] === permittedUrl) return;
    event.preventDefault();
    if (safeExternalUrl(url)) openExternal(url).catch(() => {});
  });
}

function showSettingsWindow() {
  if (settingsWindow && !settingsWindow.isDestroyed()) {
    if (settingsWindow.isMinimized()) settingsWindow.restore();
    settingsWindow.show();
    settingsWindow.focus();
    return;
  }
  const widgetBounds = mainWindow.getBounds();
  const workArea = screen.getDisplayMatching(widgetBounds).workArea;
  const bounds = constrainBounds({ width: 560, height: 720,
    x: workArea.x + Math.max(0, Math.floor((workArea.width - 560) / 2)),
    y: workArea.y + Math.max(0, Math.floor((workArea.height - 720) / 2)) });
  settingsWindow = new BrowserWindow({ ...bounds, minWidth: Math.min(440, workArea.width),
    minHeight: Math.min(400, workArea.height), show: false, frame: false,
    parent: mainWindow, modal: false, skipTaskbar: false, title: t('迪迦额度显示器 · 账号与设置'),
    backgroundColor: '#101a28', icon: gemIcon(), autoHideMenuBar: true,
    webPreferences: { preload: path.join(APP_DIRECTORY, 'preload.cjs'), contextIsolation: true,
      additionalArguments: [`--tiga-locale=${APP_LOCALE}`],
      partition: 'tiga-settings', sandbox: true, nodeIntegration: false, webSecurity: true,
      allowRunningInsecureContent: false, spellcheck: false },
  });
  settingsWindow.setMenu(null);
  settingsWindow.setAlwaysOnTop(settings.alwaysOnTop, 'floating');
  settingsWindow.webContents.setZoomFactor(1);
  protectWindow(settingsWindow, SETTINGS_URL);
  settingsWindow.once('ready-to-show', () => {
    settingsWindow.show(); settingsWindow.focus(); sendSnapshot();
  });
  settingsWindow.on('closed', () => { settingsWindow = null; });
  settingsWindow.loadFile(SETTINGS_FILE);
}

async function initializeProviders() {
  if (noProviders) { await updateEnergy(); return; }
  providers.codex = new CodexProvider({ executable: path.join(RUNTIME_DIRECTORY, 'codex.exe'),
    coreExecutable: path.join(RUNTIME_DIRECTORY, 'monitor-core.exe'), dataDir: dataDirectory,
    openExternal, onUpdate: state => providerUpdate('codex', state) });
  providers.claude = new ClaudeClient({ nodeExecutable: path.join(RUNTIME_DIRECTORY, 'node.exe'),
    executable: path.join(APP_DIRECTORY, 'native', 'claude', 'claude.exe'), dataDir: dataDirectory,
    openExternal, onUpdate: state => providerUpdate('claude', state) });
  await Promise.allSettled(Object.values(providers).map(item => item.start()));
  if (quitting) return;
  scheduleRefresh();
}

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', () => {
    showWindow();
    sendSnapshot();
  });
  app.on('activate', () => showWindow());
  app.on('window-all-closed', () => { if (quitting) app.quit(); });
  app.on('before-quit', () => {
    quitting = true;
    refreshScheduler?.stop();
    powerMonitor.removeListener('resume', resumeRefresh);
    clearInterval(expiryTimer);
    clearTimeout(positionTimer);
    saveWidgetPosition();
    for (const provider of Object.values(providers)) Promise.resolve(provider.stop()).catch(() => {});
    core.stop();
    tray?.destroy();
    tray = null;
  });
  app.whenReady().then(async () => {
    session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
    session.defaultSession.setPermissionCheckHandler(() => false);
    registerIpc();
    createWindow();
    tray = new Tray(gemIcon());
    tray.setToolTip(t('迪迦额度显示器'));
    tray.on('double-click', showWindow);
    tray.on('click', showWindow);
    updateTray();
    settingsQueue = settingsQueue.then(() => configureWatcher()).catch(() => {});
    initializeProviders().catch(() => {});
    powerMonitor.on('resume', resumeRefresh);
    expiryTimer = setInterval(() => { updateEnergy().catch(() => {}); }, 5000);
    expiryTimer.unref();
  }).catch(() => app.quit());
}
