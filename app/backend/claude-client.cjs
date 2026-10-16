'use strict';
const { t, localizedError } = require('./i18n.cjs');
const path = require('node:path');
const { spawn } = require('node:child_process');

class ClaudeClient {
  constructor({ nodeExecutable, executable, dataDir, workerPath = path.join(__dirname, 'claude-worker.cjs'),
    openExternal = () => {}, onUpdate = () => {}, spawnProcess = spawn } = {}) {
    this.nodeExecutable = nodeExecutable;
    this.executable = executable;
    this.dataDir = dataDir;
    this.workerPath = workerPath;
    this.openExternal = openExternal;
    this.onUpdate = onUpdate;
    this.spawnProcess = spawnProcess;
    this.child = null;
    this.pending = new Map();
    this.buffer = '';
    this.nextId = 1;
    this.stopping = false;
    this.state = { id: 'claude', status: 'signed_out', account: null, plan: null,
      windows: [], updatedAt: null, error: null };
  }

  getState() { return structuredClone(this.state); }

  emit(state) {
    this.state = state;
    try { this.onUpdate(this.getState()); } catch {}
    return this.getState();
  }

  connect() {
    if (this.child) return;
    this.stopping = false;
    const env = { ...process.env };
    for (const key of Object.keys(env)) {
      if (/^(OPENAI_|CODEX_|ANTHROPIC_|CLAUDE_|CLAUDECODE$|NODE_OPTIONS$|ELECTRON_RUN_AS_NODE$)/i.test(key)) delete env[key];
    }
    const child = this.spawnProcess(this.nodeExecutable, [this.workerPath,
      '--executable', this.executable, '--data-dir', this.dataDir], {
      cwd: path.dirname(this.workerPath), env, windowsHide: true, shell: false,
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.child = child;
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', data => this.receive(data));
    child.stderr.on('data', () => {});
    child.stdin.on('error', () => this.fail(child));
    child.on('error', () => this.fail(child));
    child.on('exit', () => this.fail(child));
  }

  fail(child) {
    if (this.child !== child) return;
    this.child = null;
    this.buffer = '';
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error(t('Claude Code 读取服务已断开。')));
    }
    this.pending.clear();
    if (!this.stopping) this.emit({ ...this.state, status: 'error', error: t('Claude Code 读取服务已断开，请刷新重试。') });
  }

  receive(data) {
    this.buffer += data;
    if (this.buffer.length > 1024 * 1024) { this.child?.kill(); return; }
    let newline;
    while ((newline = this.buffer.indexOf('\n')) >= 0) {
      const line = this.buffer.slice(0, newline);
      this.buffer = this.buffer.slice(newline + 1);
      let message;
      try { message = JSON.parse(line); } catch { continue; }
      if (message.event === 'state' && message.state?.id === 'claude') this.emit(message.state);
      else if (message.event === 'openExternal' && typeof message.url === 'string') {
        Promise.resolve(this.openExternal(message.url)).catch(() => {});
      } else if (this.pending.has(message.id)) {
        const pending = this.pending.get(message.id);
        this.pending.delete(message.id);
        clearTimeout(pending.timer);
        if (message.error) pending.reject(new Error(t('Claude Code 请求未完成。')));
        else pending.resolve(message.result);
      }
    }
  }

  request(method, params) {
    this.connect();
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(t('Claude Code 响应超时，请重试。')));
      }, 35000);
      this.pending.set(id, { resolve, reject, timer });
      try { this.child.stdin.write(`${JSON.stringify({ id, method, params })}\n`); }
      catch {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(new Error(t('Claude Code 读取服务不可用。')));
      }
    });
  }

  async call(method, params) {
    try { return this.emit(await this.request(method, params)); }
    catch (error) { return this.emit({ ...this.state, status: 'error', error: localizedError(error, 'Claude Code 请求未完成。') }); }
  }
  start() { return this.call('start'); }
  login() { return this.call('login'); }
  refresh() { return this.call('refresh'); }
  logout() { return this.call('logout'); }
  openUsagePage() { return this.call('openUsagePage'); }
  openSetup() { return this.call('openSetup'); }
  importStatusLine(data) { return this.call('importStatusLine', data); }

  async stop() {
    this.stopping = true;
    const child = this.child;
    if (!child) return;
    try {
      const id = this.nextId++;
      child.stdin.end(`${JSON.stringify({ id, method: 'stop' })}\n`);
    } catch {}
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error(t('Claude Code 服务已停止。')));
    }
    this.pending.clear();
    this.child = null;
    const timer = setTimeout(() => { try { child.kill(); } catch {} }, 1500);
    timer.unref();
  }
}

module.exports = { ClaudeClient };
