'use strict';
const { t, localizedError } = require('./i18n.cjs');

const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { createRequire } = require('node:module');

const OFFICIAL_USAGE_URL = 'https://claude.ai/settings/usage';
const MAX_OUTPUT = 256 * 1024;
const STALE_AFTER_MS = 10 * 60 * 1000;

// Only documented CLI commands and documented statusLine fields are consumed.
// This provider never reads an OAuth token file or calls a private HTTP API.
// TUI extraction is experimental: an unfamiliar screen yields no quota value.
function createEnvironment(configDirectory, source = process.env) {
  const environment = { ...source };
  for (const key of Object.keys(environment)) {
    if (/^(ANTHROPIC_|CLAUDE_|CLAUDECODE$)/i.test(key)) delete environment[key];
  }
  environment.CLAUDE_CONFIG_DIR = configDirectory;
  environment.DISABLE_AUTOUPDATER = '1';
  environment.CLAUDE_CODE_SKIP_PROMPT_HISTORY = '1';
  environment.LANG = 'en_US.UTF-8';
  environment.LC_ALL = 'en_US.UTF-8';
  return environment;
}

function validPercent(value) {
  return typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100;
}

function statusLineWindows(data, now = Date.now()) {
  const definitions = [
    ['five_hour', t('5 小时'), 300],
    ['seven_day', t('每周'), 10080],
  ];
  const windows = [];
  for (const [id, label, windowMinutes] of definitions) {
    const value = data?.rate_limits?.[id];
    if (!value || !validPercent(value.used_percentage)) continue;
    const resetMs = Number(value.resets_at) * 1000;
    // An elapsed reset is unknown until the official application sends new data.
    if (!Number.isFinite(resetMs) || resetMs <= now) continue;
    windows.push({ id, label, usedPercent: value.used_percentage,
      remainingPercent: 100 - value.used_percentage,
      resetsAt: new Date(resetMs).toISOString(), windowMinutes });
  }
  return windows;
}

function parseUsageScreen(screen, now = Date.now()) {
  const lines = String(screen).split(/\r?\n/).map(line => line.trim()).filter(Boolean);
  const headings = [
    { id: 'five_hour', label: t('5 小时'), windowMinutes: 300,
      match: /^Current session(?:\s*\([^)]*\))?$/i },
    { id: 'seven_day', label: t('每周'), windowMinutes: 10080,
      match: /^Current week(?:\s*\(all models\))?$/i },
    { id: 'seven_day_sonnet', label: t('每周 · Sonnet'), windowMinutes: 10080,
      match: /^Current week\s*\(Sonnet(?: only)?\)$/i },
  ];
  const windows = [];
  for (let index = 0; index < lines.length; index += 1) {
    const definition = headings.find(item => item.match.test(lines[index]));
    if (!definition) continue;
    const body = [];
    for (let next = index + 1; next < Math.min(lines.length, index + 9); next += 1) {
      if (/^(?:Current (?:session|week)|Extra usage|Usage credits|Session cost|Activity)\b/i.test(lines[next])) break;
      body.push(lines[next]);
    }
    const text = body.join('\n');
    const match = text.match(/(?:^|\s)(\d{1,3}(?:\.\d+)?)\s*%\s*(used|remaining)\b/i);
    if (!match) continue;
    const percentage = Number(match[1]);
    if (!validPercent(percentage)) continue;
    const usedPercent = match[2].toLowerCase() === 'remaining' ? 100 - percentage : percentage;
    const resetLabel = body.find(line => /^Resets\b/i.test(line)) || null;
    // Only relative timestamps are unambiguous without guessing a timezone.
    let resetsAt = null;
    const relative = resetLabel?.match(/^Resets in\s+(?:(\d+)\s*(?:h|hr|hrs|hours?)\s*)?(?:(\d+)\s*(?:m|min|mins|minutes?)\s*)?$/i);
    if (relative && (relative[1] || relative[2])) {
      resetsAt = new Date(now + (Number(relative[1] || 0) * 60 + Number(relative[2] || 0)) * 60000).toISOString();
    }
    const result = { id: definition.id, label: definition.label, usedPercent,
      remainingPercent: 100 - usedPercent, resetsAt,
      resetLabel, windowMinutes: definition.windowMinutes };
    const existing = windows.findIndex(item => item.id === result.id);
    if (existing >= 0) windows[existing] = result;
    else windows.push(result);
  }
  // A percentage elsewhere in /usage (context, spend, activity) is not quota.
  return windows;
}

function terminalScreen(terminal) {
  const buffer = terminal.buffer.active;
  const lines = [];
  for (let index = buffer.baseY; index < buffer.baseY + terminal.rows; index += 1) {
    lines.push(buffer.getLine(index)?.translateToString(true) || '');
  }
  return lines.join('\n');
}

class ClaudeProvider {
  constructor({ executable, dataDir, openExternal = () => {}, onUpdate = () => {}, spawnProcess = spawn } = {}) {
    if (!dataDir) throw new Error('ClaudeProvider requires an isolated dataDir.');
    this.executable = executable || null;
    this.dataDir = path.resolve(dataDir);
    this.configDirectory = path.join(this.dataDir, 'claude-profile');
    this.workingDirectory = path.join(this.dataDir, 'claude-session');
    this.openExternal = openExternal;
    this.onUpdate = onUpdate;
    this.spawnProcess = spawnProcess;
    this.state = { id: 'claude', status: 'signed_out', account: null, plan: null,
      windows: [], updatedAt: null, error: null, source: 'claude-official-cli',
      notice: t('使用官方 Claude Code 登录后读取额度。'), needsSetup: false };
    this.loginProcess = null;
    this.usageProcess = null;
    this.setupProcess = null;
    this.refreshPromise = null;
    this.stopped = false;
    this.operation = 0;
  }

  getState() {
    const copy = structuredClone(this.state);
    const age = copy.updatedAt ? Date.now() - Date.parse(copy.updatedAt) : 0;
    copy.stale = age > STALE_AFTER_MS;
    copy.windows = copy.windows.filter(window => !window.resetsAt || Date.parse(window.resetsAt) > Date.now());
    if (this.state.windows.length && !copy.windows.length && copy.status === 'connected') {
      copy.status = 'quota_pending';
      copy.notice = t('额度窗口已重置，等待官方数据刷新。');
    }
    return copy;
  }

  emit(patch = {}) {
    Object.assign(this.state, patch);
    const state = this.getState();
    this.onUpdate(state);
    return state;
  }

  prepare() {
    fs.mkdirSync(this.configDirectory, { recursive: true });
    fs.mkdirSync(this.workingDirectory, { recursive: true });
    return Boolean(this.executable && fs.existsSync(this.executable));
  }

  async start() {
    this.stopped = false;
    if (!this.prepare()) {
      return this.emit({ status: 'signed_out', notice: t('尚未安装官方 Claude Code 组件，可先打开官方用量页。'), error: null });
    }
    return this.refresh();
  }

  run(arguments_, timeout = 12000) {
    return new Promise((resolve, reject) => {
      const child = this.spawnProcess(this.executable, arguments_, {
        cwd: this.workingDirectory, env: createEnvironment(this.configDirectory),
        windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
      });
      let output = '';
      let settled = false;
      const finish = (error, result) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        if (error) reject(error); else resolve(result);
      };
      const timer = setTimeout(() => {
        child.kill();
        finish(new Error(t('官方 Claude Code 响应超时，请重试。')));
      }, timeout);
      child.stdout.on('data', data => { output = (output + data.toString()).slice(-MAX_OUTPUT); });
      // Never log CLI output: authentication flows can contain one-use codes.
      child.stderr.on('data', () => {});
      child.on('error', () => finish(new Error(t('无法启动官方 Claude Code 组件。'))));
      child.on('close', code => finish(null, { code, output }));
    });
  }

  async login() {
    if (this.loginProcess) return this.getState();
    if (!this.prepare()) {
      return this.emit({ status: 'signed_out', error: t('官方 Claude Code 组件缺失。'),
        notice: t('可在官方用量页登录并查看额度；这不会自动连接到显示器。') });
    }
    this.stopped = false;
    this.operation += 1;
    const operation = this.operation;
    this.usageProcess?.kill();
    this.emit({ status: 'awaiting_login', account: null, plan: null, windows: [], updatedAt: null, error: null, needsSetup: false,
      notice: t('请在官方 Claude Code 打开的浏览器中完成登录。') });
    const child = this.spawnProcess(this.executable, ['auth', 'login'], {
      cwd: this.workingDirectory, env: createEnvironment(this.configDirectory),
      windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
    });
    this.loginProcess = child;
    let completed = false;
    const timeout = setTimeout(() => {
      child.kill();
      if (!completed) this.emit({ status: 'signed_out', error: t('登录等待超时，请重新点击登录。') });
    }, 5 * 60 * 1000);
    const finish = async code => {
      if (completed) return;
      completed = true;
      clearTimeout(timeout);
      if (this.loginProcess === child) this.loginProcess = null;
      if (this.stopped || operation !== this.operation) return;
      if (code === 0) {
        if (this.refreshPromise) await this.refreshPromise;
        if (!this.stopped && operation === this.operation) await this.refresh();
      }
      else this.emit({ status: 'signed_out', error: t('官方登录未完成，请重新登录。'),
        notice: t('也可以打开官方用量页查看额度。') });
    };
    // Browser opening and token storage are performed by the official binary.
    child.stdout.on('data', () => {});
    child.stderr.on('data', () => {});
    child.on('error', () => finish(1));
    child.on('close', code => finish(code));
    return this.getState();
  }

  refresh() {
    if (this.refreshPromise) return this.refreshPromise;
    this.refreshPromise = this.refreshInternal().finally(() => { this.refreshPromise = null; });
    return this.refreshPromise;
  }

  async refreshInternal() {
    if (this.loginProcess || this.setupProcess || this.stopped) return this.getState();
    if (!this.prepare()) return this.start();
    const operation = this.operation;
    try {
      const response = await this.run(['auth', 'status', '--json']);
      if (this.stopped || operation !== this.operation) return this.getState();
      let authentication;
      try { authentication = JSON.parse(response.output); }
      catch { throw new Error(t('官方 Claude Code 未返回可识别的登录状态。')); }
      if (!authentication.loggedIn) {
        return this.emit({ status: 'signed_out', account: null, plan: null,
          windows: [], updatedAt: null, error: null, needsSetup: false, notice: t('使用 Claude Code 账号登录。') });
      }
      const account = authentication.email || authentication.emailAddress || authentication.account?.email || 'Claude Code';
      const plan = authentication.subscriptionType || authentication.plan || null;
      if (!['claude.ai', 'oauth_token'].includes(authentication.authMethod)) {
        return this.emit({ status: 'quota_pending', account, plan, windows: [], error: null,
          notice: t('当前官方组件使用 API 计费方式，未提供订阅剩余额度。') });
      }
      this.emit({ status: this.state.windows.length ? 'connected' : 'quota_pending', account, plan,
        error: null, notice: t('正在读取官方 Claude Code 用量界面。') });
      const result = await this.readUsage();
      if (this.stopped || operation !== this.operation) return this.getState();
      if (result.windows.length) {
        return this.emit({ status: 'connected', windows: result.windows,
          updatedAt: new Date().toISOString(), error: null, needsSetup: false,
          source: 'claude-cli-usage', notice: t('来自官方 Claude Code 用量界面。') });
      }
      return this.emit({ status: this.state.windows.length ? 'connected' : 'quota_pending', error: null,
        needsSetup: Boolean(result.needsSetup),
        notice: result.notice || t('已登录；暂未读取到额度，可打开官方用量页查看。') });
    } catch (error) {
      if (this.stopped || operation !== this.operation) return this.getState();
      return this.emit({ status: this.state.windows.length ? 'connected' : 'error',
        error: localizedError(error, 'Claude Code 额度读取失败。'), notice: t('可重试，或打开官方用量页。') });
    }
  }

  async readUsage() {
    let pty;
    let Terminal;
    try {
      const runtimeRequire = createRequire(path.join(__dirname, 'claude-runtime', 'package.json'));
      pty = runtimeRequire('node-pty');
      ({ Terminal } = runtimeRequire('@xterm/headless'));
    } catch {
      return { windows: [], notice: t('用量读取组件不可用，请打开官方用量页查看。') };
    }
    return new Promise(resolve => {
      const terminal = new Terminal({ cols: 140, rows: 70, scrollback: 100, allowProposedApi: true });
      let child;
      let finished = false;
      let selectedTheme = false;
      let lastWindows = [];
      let stableTimer;
      const finish = (windows = [], notice = null, needsSetup = false) => {
        if (finished) return;
        finished = true;
        clearTimeout(timeout);
        clearTimeout(stableTimer);
        try { child?.kill(); } catch {}
        if (this.usageProcess === child) this.usageProcess = null;
        terminal.dispose();
        resolve({ windows, notice, needsSetup });
      };
      const timeout = setTimeout(() => finish([], t('已登录；官方用量界面尚未就绪，可打开官方用量页查看。')), 18000);
      try {
        child = pty.spawn(this.executable, [
          '--safe-mode', '--ax-screen-reader', '--strict-mcp-config',
          '--mcp-config', '{"mcpServers":{}}', '--tools', '', '--', '/usage',
        ], { name: 'xterm-256color', cols: 140, rows: 70, cwd: this.workingDirectory,
          env: createEnvironment(this.configDirectory), useConpty: true });
        this.usageProcess = child;
        child.onData(data => {
          if (finished) return;
          terminal.write(data, () => {
            if (finished) return;
            const screen = terminalScreen(terminal);
            // Only the harmless display preference is selected automatically.
            if (!selectedTheme && /Choose the text style|Select with numbers \[1-7\]/i.test(screen)) {
              selectedTheme = true;
              child.write('2\r');
              return;
            }
            if (/Do you trust|trust this folder|Yes, I trust|Terms of Service|Choose how to sign in|Select login method|Safety (?:check|notes)|Security (?:notes|reminders)|Press Enter to continue/i.test(screen)) {
              finish([], t('请完成官方 Claude Code 首次设置，完成后刷新额度。'), true);
              return;
            }
            const windows = parseUsageScreen(screen);
            if (!windows.length) return;
            lastWindows = windows;
            clearTimeout(stableTimer);
            stableTimer = setTimeout(() => finish(lastWindows), 650);
          });
        });
        child.onExit(() => finish(lastWindows));
      } catch {
        finish([], t('无法打开官方用量界面，请打开官方用量页查看。'));
      }
    });
  }

  importStatusLine(data) {
    const windows = statusLineWindows(data);
    if (!windows.length) {
      return this.emit({ notice: t('这份官方状态行数据没有有效的订阅额度。') });
    }
    return this.emit({ status: 'connected', windows, updatedAt: new Date().toISOString(), error: null, needsSetup: false,
      source: 'claude-official-statusline', notice: t('来自官方 Claude Code 状态行；显示最近一次收到的数据。') });
  }

  async openUsagePage() {
    await this.openExternal(OFFICIAL_USAGE_URL);
    return this.getState();
  }

  async openSetup() {
    if (this.setupProcess) return this.getState();
    if (!this.state.needsSetup) return this.getState();
    if (!this.prepare()) return this.emit({ error: t('官方 Claude Code 组件缺失。') });
    this.operation += 1;
    const operation = this.operation;
    this.usageProcess?.kill();
    const powershell = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32',
      'WindowsPowerShell', 'v1.0', 'powershell.exe');
    const child = this.spawnProcess(powershell, ['-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
      '-File', path.join(__dirname, 'open-claude-setup.ps1'),
      '-Executable', this.executable, '-ProfileDirectory', this.configDirectory,
      '-WorkingDirectory', this.workingDirectory], {
      cwd: this.workingDirectory, env: createEnvironment(this.configDirectory),
      windowsHide: true, shell: false, stdio: ['ignore', 'ignore', 'ignore'],
    });
    this.setupProcess = child;
    this.emit({ status: 'quota_pending', needsSetup: true, error: null,
      notice: t('请在已打开的官方终端完成首次设置；关闭终端后会自动刷新。') });
    let completed = false;
    const finish = async failed => {
      if (completed) return;
      completed = true;
      if (this.setupProcess === child) this.setupProcess = null;
      if (this.stopped || operation !== this.operation) return;
      if (failed) {
        this.emit({ status: 'quota_pending', needsSetup: true,
          error: t('官方设置窗口未能完成，请重试或打开官方用量页。') });
        return;
      }
      if (this.refreshPromise) await this.refreshPromise;
      if (!this.stopped && operation === this.operation) await this.refresh();
    };
    child.on('error', () => finish(true));
    // Ctrl+C / closing the official window may return nonzero after successful
    // first setup, so only our launcher's explicit errors suppress a refresh.
    child.on('close', code => finish(code >= 202 && code <= 205));
    return this.getState();
  }

  async logout() {
    this.operation += 1;
    this.loginProcess?.kill();
    this.loginProcess = null;
    this.usageProcess?.kill();
    this.setupProcess?.kill();
    this.setupProcess = null;
    if (this.prepare()) {
      try {
        const result = await this.run(['auth', 'logout']);
        if (result.code !== 0) throw new Error('Official logout failed.');
      }
      catch { return this.emit({ status: 'error', windows: [], updatedAt: null,
        error: t('官方组件退出登录失败，请重试。') }); }
    }
    return this.emit({ status: 'signed_out', account: null, plan: null,
      windows: [], updatedAt: null, error: null, needsSetup: false, notice: t('使用 Claude Code 账号登录。') });
  }

  stop() {
    this.operation += 1;
    this.stopped = true;
    this.loginProcess?.kill();
    this.loginProcess = null;
    this.usageProcess?.kill();
    this.usageProcess = null;
    this.setupProcess?.kill();
    this.setupProcess = null;
    return this.getState();
  }
}

module.exports = { ClaudeProvider, createEnvironment, parseUsageScreen, statusLineWindows, OFFICIAL_USAGE_URL };
