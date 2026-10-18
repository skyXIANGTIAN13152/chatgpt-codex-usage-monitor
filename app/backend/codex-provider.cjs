'use strict';
const { t } = require('./i18n.cjs');

const fs = require('node:fs/promises');
const path = require('node:path');
const { RpcClient, rpcError } = require('./rpc-client.cjs');
const { CoreBridge } = require('./core-bridge.cjs');

const LOGIN_HOSTS = new Set(['auth.openai.com', 'auth0.openai.com', 'chatgpt.com']);
const PLANS = new Set(['free', 'go', 'plus', 'pro', 'prolite', 'team', 'business',
  'enterprise', 'edu', 'edu_plus', 'edu_pro', 'unknown', 'ent26',
  'self_serve_business_prolite', 'self_serve_business_usage_based',
  'enterprise_cbp_automation', 'enterprise_cbp_usage_based']);
const EMPTY = () => ({ id: 'codex', status: 'signed_out', account: null, plan: null,
  windows: [], updatedAt: null, error: null });

function createIsolatedEnvironment(dataDir, source = process.env) {
  const env = {};
  for (const [key, value] of Object.entries(source)) {
    if (/^(OPENAI_|CODEX_|CHATGPT_|AZURE_OPENAI_|ANTHROPIC_|CLAUDE_)/i.test(key) ||
        /^(ACCESS_TOKEN|API_KEY|NODE_OPTIONS|NODE_EXTRA_CA_CERTS)$/i.test(key) ||
        /(?:API_?KEY|AUTH_TOKEN|ACCESS_TOKEN|REFRESH_TOKEN|BEARER_TOKEN)$/i.test(key)) continue;
    env[key] = value;
  }
  env.CODEX_HOME = path.resolve(dataDir, 'codex');
  return env;
}

function isAllowedAuthUrl(value) {
  try {
    const url = new URL(value);
    return url.protocol === 'https:' && LOGIN_HOSTS.has(url.hostname) &&
      !url.username && !url.password && (!url.port || url.port === '443');
  } catch { return false; }
}

function publicError(error) {
  switch (error && error.code) {
    case 'LAUNCH_FAILED': return t('无法启动随附的 Codex 服务。');
    case 'TIMEOUT': return t('Codex 服务响应超时，请重试。');
    case 'LOGIN_TIMEOUT': return t('登录已超时，请重新登录。');
    case 'LOGIN_FAILED': return t('登录未完成，请重新登录。');
    case 'INVALID_AUTH_URL': return t('登录地址未通过官方域名检查。');
    case 'OPEN_BROWSER_FAILED': return t('无法打开浏览器，请检查默认浏览器后重试。');
    case 'UNAUTHORIZED': return t('登录已过期，请重新登录。');
    case 'CLOSED': return t('Codex 服务已断开，请刷新重试。');
    default: return t('暂时无法读取 Codex 数据，请稍后重试。');
  }
}

class CodexProvider {
  constructor({ executable, dataDir, openExternal, onUpdate = () => {}, coreExecutable,
    spawnProcess, requestTimeoutMs = 15000, loginTimeoutMs = 5 * 60 * 1000 } = {}) {
    if (!executable || !dataDir || typeof openExternal !== 'function') {
      throw new TypeError('executable, dataDir and openExternal are required');
    }
    this.executable = path.resolve(executable);
    this.dataDir = path.resolve(dataDir);
    this.home = path.join(this.dataDir, 'codex');
    this.openExternal = openExternal;
    this.onUpdate = onUpdate;
    this.spawnProcess = spawnProcess;
    this.requestTimeoutMs = requestTimeoutMs;
    this.loginTimeoutMs = loginTimeoutMs;
    this.core = new CoreBridge({ executable: coreExecutable });
    this.state = EMPTY();
    this.rpc = null;
    this.connectPromise = null;
    this.refreshPromise = null;
    this.loginPromise = null;
    this.loginAttempt = null;
    this.epoch = 0;
    this.stopped = false;
  }

  getState() { return structuredClone(this.state); }

  publish(patch) {
    this.state = { ...this.state, ...patch };
    try { this.onUpdate(this.getState()); } catch {}
    return this.getState();
  }

  async connect() {
    if (this.rpc && !this.rpc.closed) return this.rpc;
    if (this.connectPromise) return this.connectPromise;
    const epoch = this.epoch;
    this.connectPromise = (async () => {
      await fs.mkdir(this.home, { recursive: true, mode: 0o700 });
      if (this.stopped || epoch !== this.epoch) throw rpcError('STOPPED');
      let rpc;
      rpc = new RpcClient({ executable: this.executable,
        args: ['app-server', '-c', 'cli_auth_credentials_store="file"'],
        cwd: this.home, env: createIsolatedEnvironment(this.dataDir),
        spawnProcess: this.spawnProcess, timeoutMs: this.requestTimeoutMs,
        onNotification: (method, params) => {
          if (this.rpc === rpc && !this.stopped) {
            Promise.resolve(this.notification(method, params)).catch(() => {});
          }
        },
        onClose: () => {
          if (this.rpc !== rpc || this.stopped) return;
          if (this.loginAttempt) this.finishLogin(rpcError('CLOSED'));
          this.publish({ status: 'error', error: publicError(rpcError('CLOSED')) });
        },
      });
      this.rpc = rpc;
      try {
        await rpc.request('initialize', { clientInfo: {
          name: 'tiga_usage_monitor', title: 'Tiga Usage Monitor', version: '2.0.1' },
        capabilities: { experimentalApi: false } });
        if (this.stopped || epoch !== this.epoch) throw rpcError('STOPPED');
        rpc.notify('initialized');
        return rpc;
      } catch (error) {
        rpc.stop();
        if (this.rpc === rpc) this.rpc = null;
        throw error;
      }
    })().finally(() => { this.connectPromise = null; });
    return this.connectPromise;
  }

  async start() { this.stopped = false; return this.refresh(); }

  refresh() {
    if (this.loginAttempt || this.loginPromise) return Promise.resolve(this.getState());
    if (this.refreshPromise) return this.refreshPromise;
    this.stopped = false;
    const epoch = this.epoch;
    this.publish({ status: 'connecting', error: null });
    this.refreshPromise = this.readState(epoch).finally(() => { this.refreshPromise = null; });
    return this.refreshPromise;
  }

  async readState(epoch) {
    for (let attempt = 0; attempt < 2; attempt++) {
      try {
        const rpc = await this.connect();
        const response = await rpc.request('account/read', { refreshToken: false });
        if (this.stopped || epoch !== this.epoch) return this.getState();
        const account = response && response.account;
        if (!account || account.type !== 'chatgpt') {
          await this.core.clear();
          return this.publish(EMPTY());
        }
        const email = typeof account.email === 'string' &&
          /^[^\s@]{1,128}@[^\s@]{1,128}$/.test(account.email) ? account.email : 'ChatGPT';
        this.publish({ status: 'connected', account: email,
          plan: PLANS.has(account.planType) ? account.planType : null, error: null });
        const limits = await rpc.request('account/rateLimits/read');
        if (this.stopped || epoch !== this.epoch) return this.getState();
        const parsed = await this.core.parseRateLimits(limits);
        if (this.stopped || epoch !== this.epoch) return this.getState();
        return this.publish({ status: 'connected', windows: parsed.windows,
          plan: PLANS.has(parsed.plan) ? parsed.plan : this.state.plan,
          updatedAt: parsed.windows.length ? new Date().toISOString() : null,
          error: parsed.windows.length ? null : t('官方暂未提供额度数据。') });
      } catch (error) {
        if (this.stopped || epoch !== this.epoch || error.code === 'STOPPED') return this.getState();
        if (error.code === 'UNAUTHORIZED') {
          this.core.stop();
          return this.publish({ ...EMPTY(), error: publicError(error) });
        }
        if (attempt === 0 && ['TIMEOUT', 'CLOSED', 'PROTOCOL'].includes(error.code)) {
          if (this.rpc) this.rpc.stop();
          this.rpc = null;
          continue;
        }
        return this.publish({ status: 'error', error: publicError(error) });
      }
    }
    return this.getState();
  }

  login() {
    if (this.loginPromise) return this.loginPromise;
    if (this.state.status === 'connected') return Promise.resolve(this.getState());
    this.stopped = false;
    this.loginPromise = this.performLogin().finally(() => { this.loginPromise = null; });
    return this.loginPromise;
  }

  async performLogin() {
    const epoch = this.epoch;
    this.publish({ status: 'connecting', error: null });
    let loginAttempt;
    try {
      if (this.refreshPromise) await this.refreshPromise;
      const rpc = await this.connect();
      if (this.stopped || epoch !== this.epoch) return this.getState();
      // A new authorization must never reuse another session's cached windows.
      this.core.stop();
      let settle;
      const completion = new Promise((resolve) => { settle = resolve; });
      loginAttempt = { loginId: null, settle, early: [], timer: null, rpc };
      this.loginAttempt = loginAttempt;
      const result = await rpc.request('account/login/start', { type: 'chatgpt' });
      if (this.loginAttempt !== loginAttempt || epoch !== this.epoch) return this.getState();
      if (!result || result.type !== 'chatgpt' || typeof result.loginId !== 'string' ||
          !result.loginId || !isAllowedAuthUrl(result.authUrl)) throw rpcError('INVALID_AUTH_URL');
      loginAttempt.loginId = result.loginId;
      loginAttempt.timer = setTimeout(() => {
        this.cancelAttempt(loginAttempt, rpcError('LOGIN_TIMEOUT')).catch(() => {});
      }, this.loginTimeoutMs);
      this.publish({ ...EMPTY(), status: 'awaiting_login' });
      try { await this.openExternal(result.authUrl); } catch { throw rpcError('OPEN_BROWSER_FAILED'); }
      for (const params of loginAttempt.early) this.completeNotification(params);
      const outcome = await completion;
      if (this.stopped || epoch !== this.epoch || outcome.cancelled) return this.getState();
      if (outcome.error) throw outcome.error;
      // loginPromise is still active, so use the internal refresh path here.
      return await this.readState(epoch);
    } catch (error) {
      if (loginAttempt && this.loginAttempt === loginAttempt) {
        await this.cancelAttempt(loginAttempt, error);
      }
      if (this.stopped || epoch !== this.epoch || error.code === 'STOPPED') return this.getState();
      return this.publish({ ...EMPTY(), status: 'error', error: publicError(error) });
    }
  }

  finishLogin(error = null, cancelled = false) {
    const attempt = this.loginAttempt;
    if (!attempt) return;
    clearTimeout(attempt.timer);
    this.loginAttempt = null;
    attempt.settle({ error, cancelled });
  }

  async cancelAttempt(attempt, error = null) {
    if (this.loginAttempt !== attempt) return;
    this.finishLogin(error, !error);
    if (attempt.loginId && !attempt.rpc.closed) {
      try { await attempt.rpc.request('account/login/cancel', { loginId: attempt.loginId }, 2000); } catch {}
    } else if (!attempt.loginId) {
      // The start response may itself be stuck; terminate its callback server too.
      attempt.rpc.stop();
      if (this.rpc === attempt.rpc) this.rpc = null;
    }
  }

  async cancelLogin() {
    this.epoch++;
    this.publish(EMPTY());
    const attempt = this.loginAttempt;
    if (attempt) await this.cancelAttempt(attempt);
    return this.getState();
  }

  completeNotification(params) {
    const attempt = this.loginAttempt;
    if (!attempt) return;
    if (!attempt.loginId) {
      if (attempt.early.length < 4) attempt.early.push(params);
      return;
    }
    if (params.loginId !== attempt.loginId) return;
    this.finishLogin(params.success === true ? null : rpcError('LOGIN_FAILED'));
  }

  async notification(method, params) {
    if (method === 'account/login/completed') return this.completeNotification(params);
    if (method === 'account/updated') {
      if (this.loginAttempt || this.loginPromise) return;
      if (params.authMode === null) {
        this.epoch++;
        this.publish(EMPTY());
        await this.core.clear();
      }
      else this.refresh().catch(() => {});
    } else if (method === 'account/rateLimits/updated' && this.state.status === 'connected') {
      const epoch = this.epoch;
      const rpc = this.rpc;
      const parsed = await this.core.parseRateLimits(params, true);
      if (this.stopped || epoch !== this.epoch || rpc !== this.rpc ||
          this.state.status !== 'connected') return;
      this.publish({ windows: parsed.windows,
        plan: PLANS.has(parsed.plan) ? parsed.plan : this.state.plan,
        updatedAt: parsed.windows.length ? new Date().toISOString() : null,
        error: parsed.windows.length ? null : t('官方暂未提供额度数据。') });
    }
  }

  async logout() {
    this.epoch++;
    this.publish(EMPTY());
    if (this.loginAttempt) await this.cancelAttempt(this.loginAttempt);
    try {
      this.stopped = false;
      const rpc = await this.connect();
      await rpc.request('account/logout');
      await this.core.clear();
      return this.publish(EMPTY());
    } catch (error) {
      return this.publish({ ...EMPTY(), status: 'error', error: publicError(error) });
    }
  }

  async stop() {
    this.stopped = true;
    this.epoch++;
    this.publish(EMPTY());
    const cancellation = this.loginAttempt ? this.cancelAttempt(this.loginAttempt) : null;
    if (this.rpc) this.rpc.stop();
    this.rpc = null;
    this.core.stop();
    if (cancellation) await cancellation;
  }
}

module.exports = { CodexProvider, createIsolatedEnvironment, isAllowedAuthUrl };
