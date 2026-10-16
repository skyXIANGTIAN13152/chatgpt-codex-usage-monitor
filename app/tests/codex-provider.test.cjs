'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const { PassThrough, Writable } = require('node:stream');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { CodexProvider, createIsolatedEnvironment, isAllowedAuthUrl } =
  require('../backend/codex-provider.cjs');
const { CoreBridge } = require('../backend/core-bridge.cjs');

const turn = () => new Promise((resolve) => setImmediate(resolve));
async function waitFor(predicate) {
  const end = Date.now() + 1000;
  while (!predicate()) {
    if (Date.now() > end) throw new Error('Timed out waiting for fake-process event');
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}

class FakeChild extends EventEmitter {
  constructor(handler) {
    super();
    this.stdout = new PassThrough();
    this.stderr = new PassThrough();
    this.requests = [];
    this.killed = false;
    let buffer = '';
    this.stdin = new Writable({ write: (chunk, encoding, done) => {
      buffer += chunk.toString();
      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const request = JSON.parse(buffer.slice(0, newline));
        buffer = buffer.slice(newline + 1);
        this.requests.push(request);
        queueMicrotask(() => handler(request, this));
      }
      done();
    } });
  }
  send(value) { if (!this.killed) this.stdout.write(JSON.stringify(value) + '\n'); }
  result(request, result) { this.send({ id: request.id, result }); }
  notice(method, params) { this.send({ method, params }); }
  kill() { this.killed = true; queueMicrotask(() => this.emit('exit', 0)); return true; }
}

async function harness(t, options = {}) {
  const dataDir = await fs.mkdtemp(path.join(os.tmpdir(), 'tiga-codex-test-'));
  const children = [];
  const spawnCalls = [];
  const updates = [];
  let signedIn = options.signedIn || false;
  const limits = options.limits === undefined ? { rateLimits: { limitId: 'codex', planType: 'plus',
    primary: { usedPercent: 25, windowDurationMins: 300, resetsAt: 1800000000 },
    secondary: { usedPercent: 62, windowDurationMins: 10080, resetsAt: 1800500000 } } } : options.limits;
  const defaultHandler = (request, child) => {
    if (request.id === undefined) return;
    switch (request.method) {
      case 'initialize': return child.result(request, { userAgent: 'fake' });
      case 'account/read': return child.result(request, { requiresOpenaiAuth: true,
        account: signedIn ? { type: 'chatgpt', email: 'test@example.com', planType: 'plus',
          accessToken: 'DO_NOT_FORWARD_ACCOUNT_TOKEN' } : null });
      case 'account/rateLimits/read': return child.result(request, limits);
      case 'account/login/start': return child.result(request, { type: 'chatgpt', loginId: 'login-one',
        authUrl: options.authUrl || 'https://auth.openai.com/oauth/authorize?state=test',
        accessToken: 'DO_NOT_FORWARD_LOGIN_TOKEN' });
      case 'account/login/cancel': return child.result(request, { status: 'canceled' });
      case 'account/logout': signedIn = false; return child.result(request, {});
      default: return child.result(request, {});
    }
  };
  const opened = [];
  const provider = new CodexProvider({ executable: path.join(dataDir, 'bundled', 'codex.exe'), dataDir,
    requestTimeoutMs: options.requestTimeoutMs || 150,
    loginTimeoutMs: options.loginTimeoutMs || 500,
    spawnProcess: (executable, args, opts) => {
      spawnCalls.push({ executable, args, opts });
      const child = new FakeChild((request, proc) => {
        if (options.handler && options.handler(request, proc, children.length, defaultHandler)) return;
        defaultHandler(request, proc);
      });
      children.push(child);
      return child;
    },
    openExternal: async (url) => {
      opened.push(url);
      if (options.browserError) throw new Error('secret browser details');
      if (options.completeLogin !== false) {
        signedIn = true;
        children.at(-1).notice('account/login/completed', {
          loginId: 'login-one', success: true, error: null, token: 'DO_NOT_FORWARD_NOTIFICATION_TOKEN' });
      }
    },
    onUpdate: (state) => updates.push(state),
  });
  t.after(async () => { await provider.stop(); await fs.rm(dataDir, { recursive: true, force: true }); });
  return { provider, dataDir, children, spawnCalls, opened, updates };
}

test('isolates child CODEX_HOME, file credential store and inherited auth environment', async (t) => {
  const env = createIsolatedEnvironment('C:\\tiga-data', {
    PATH: 'system-path', USERPROFILE: 'old-profile', CODEX_HOME: 'old-account',
    OPENAI_API_KEY: 'secret', OPENAI_BASE_URL: 'https://untrusted.invalid',
    CODEX_ACCESS_TOKEN: 'secret', ACCESS_TOKEN: 'secret', Anthropic_Api_Key: 'secret',
    CODEX_AUTH_JSON: 'old-auth', NODE_OPTIONS: '--require untrusted.cjs',
    HTTPS_PROXY: 'https://proxy.example',
  });
  assert.equal(env.CODEX_HOME, path.resolve('C:\\tiga-data', 'codex'));
  assert.equal(env.PATH, 'system-path');
  assert.equal(env.HTTPS_PROXY, 'https://proxy.example');
  for (const key of ['OPENAI_API_KEY', 'OPENAI_BASE_URL', 'CODEX_ACCESS_TOKEN', 'ACCESS_TOKEN',
    'Anthropic_Api_Key', 'CODEX_AUTH_JSON', 'NODE_OPTIONS']) assert.equal(env[key], undefined);
  const h = await harness(t);
  await h.provider.start();
  assert.equal(h.spawnCalls[0].opts.env.CODEX_HOME, path.join(h.dataDir, 'codex'));
  assert.equal(h.spawnCalls[0].opts.cwd, path.join(h.dataDir, 'codex'));
  assert.equal(h.spawnCalls[0].opts.windowsHide, true);
  assert.equal(h.spawnCalls[0].opts.shell, false);
  assert.deepEqual(h.spawnCalls[0].args, ['app-server', '-c', 'cli_auth_credentials_store="file"']);
});

test('signed out start never invents quotas or starts browser authorization', async (t) => {
  const h = await harness(t);
  const state = await h.provider.start();
  assert.equal(state.status, 'signed_out');
  assert.deepEqual(state.windows, []);
  assert.equal(state.updatedAt, null);
  assert.deepEqual(h.opened, []);
  assert.equal(h.children[0].requests.some((request) => request.method === 'account/rateLimits/read'), false);
  const methods = h.children[0].requests.map((request) => request.method);
  assert.deepEqual(methods, ['initialize', 'initialized', 'account/read']);
});

test('official browser flow waits for completion and publishes sanitized account and quota data', async (t) => {
  const h = await harness(t);
  const state = await h.provider.login();
  assert.equal(state.status, 'connected');
  assert.equal(state.account, 'test@example.com');
  assert.equal(state.plan, 'plus');
  assert.equal(state.windows[0].remainingPercent, 75);
  assert.equal(state.windows[1].remainingPercent, 38);
  assert.equal(state.windows[0].resetsAt, new Date(1800000000000).toISOString());
  assert.ok(state.updatedAt);
  assert.equal(h.opened.length, 1);
  assert.ok(h.updates.some((update) => update.status === 'awaiting_login'));
  assert.ok(!JSON.stringify(h.updates).includes('DO_NOT_FORWARD'));
  assert.ok(!JSON.stringify(state).includes('authUrl'));
  const request = h.children[0].requests.find((value) => value.method === 'account/login/start');
  assert.deepEqual(request.params, { type: 'chatgpt' });
  state.windows[0].remainingPercent = -1;
  assert.equal(h.provider.getState().windows[0].remainingPercent, 75);
});

test('login URL allowlist rejects non-HTTPS, lookalike domains, credentials and unusual ports', () => {
  for (const url of ['http://auth.openai.com/login', 'https://auth.openai.com.attacker.com/login',
    'https://evil.example/?next=https://auth.openai.com', 'https://evil@auth.openai.com/login',
    'file:///C:/Windows/system32/calc.exe', 'https://auth.openai.com:8080/login', 'invalid']) {
    assert.equal(isAllowedAuthUrl(url), false, url);
  }
  assert.equal(isAllowedAuthUrl('https://auth.openai.com/oauth/authorize?state=a'), true);
});

test('unsafe auth URL is canceled without opening browser or leaking raw server errors', async (t) => {
  const h = await harness(t, { authUrl: 'https://evil.example/?access_token=PRIVATE_TOKEN' });
  const state = await h.provider.login();
  assert.equal(state.status, 'error');
  assert.match(state.error, /官方域名/);
  assert.deepEqual(h.opened, []);
  assert.ok(!JSON.stringify(h.updates).includes('PRIVATE_TOKEN'));
});

test('missing and malformed quota fields stay unavailable, never a fabricated full/empty gauge', async (t) => {
  const h = await harness(t, { signedIn: true, limits: { rateLimits: {
    primary: { windowDurationMins: 300 }, secondary: { usedPercent: null } } } });
  const state = await h.provider.start();
  assert.equal(state.status, 'connected');
  assert.deepEqual(state.windows, []);
  assert.equal(state.updatedAt, null);
  assert.match(state.error, /未提供额度/);
  const core = new CoreBridge();
  t.after(() => core.stop());
  assert.deepEqual((await core.parseRateLimits({ rateLimits: { primary: { usedPercent: '' } } })).windows, []);
});

test('chooses Codex bucket and merges sparse notification without deleting weekly data', async (t) => {
  const h = await harness(t, { signedIn: true });
  await h.provider.start();
  h.children[0].notice('account/rateLimits/updated', { rateLimits: { limitId: 'codex',
    primary: { usedPercent: 35, windowDurationMins: 300 } } });
  await waitFor(() => h.provider.getState().windows[0].remainingPercent === 65);
  const state = h.provider.getState();
  assert.equal(state.windows.length, 2);
  assert.equal(state.windows[0].remainingPercent, 65);
  assert.equal(state.windows[1].remainingPercent, 38);
  const core = new CoreBridge();
  t.after(() => core.stop());
  const parsed = await core.parseRateLimits({ rateLimits: { limitId: 'review', primary: { usedPercent: 99 } },
    rateLimitsByLimitId: { codex: { primary: { usedPercent: 12, windowDurationMins: 300 } } } });
  assert.equal(parsed.windows[0].remainingPercent, 88);
  assert.deepEqual((await core.parseRateLimits({ rateLimits: { limitId: 'review', primary: { usedPercent: 1 } } })).windows, []);
});

test('native old core honors authoritative empty multi-bucket payload and explicit-null sparse removal', async (t) => {
  const core = new CoreBridge();
  t.after(() => core.stop());
  await core.parseRateLimits({ rateLimits: { primary: { usedPercent: 20, windowDurationMins: 300 },
    secondary: { usedPercent: 40, windowDurationMins: 10080 } } });
  const removed = await core.parseRateLimits({ rateLimits: { primary: null } }, true);
  assert.equal(removed.windows.length, 1);
  assert.equal(removed.windows[0].id, 'weekly');
  const empty = await core.parseRateLimits({ rateLimitsByLimitId: {},
    rateLimits: { primary: { usedPercent: 0, windowDurationMins: 300 } } });
  assert.deepEqual(empty.windows, []);
});

test('native old energy core drives all three scene states and unknown without fictional quota', async (t) => {
  const core = new CoreBridge();
  t.after(() => core.stop());
  assert.equal((await core.classifyEnergy(75, 50)).scene, 'full');
  const low = await core.classifyEnergy(10, 50);
  assert.equal(low.scene, 'low');
  assert.equal(low.fullCycleMs, 400);
  assert.equal((await core.classifyEnergy(0, 50)).scene, 'stone');
  assert.equal((await core.classifyEnergy(null, 50)).scene, 'unknown');
  assert.equal((await core.classifyEnergy(60, 70)).scene, 'low');
});

test('refresh reconnects once after timed out RPC and ignores stale process notifications', async (t) => {
  const h = await harness(t, { signedIn: true, requestTimeoutMs: 15,
    handler: (request, child, count) => count === 1 && request.method === 'account/read' });
  const state = await h.provider.start();
  assert.equal(state.status, 'connected');
  assert.equal(h.children.length, 2);
  assert.equal(h.children[0].killed, true);
  h.children[0].emit('data', 'stale');
  h.children[0].emit('exit', 1);
  assert.equal(h.provider.getState().status, 'connected');
});

test('manual refresh recovers after service exit without dropping accurate old windows before retry', async (t) => {
  const h = await harness(t, { signedIn: true });
  await h.provider.start();
  h.children[0].emit('exit', 1);
  assert.equal(h.provider.getState().status, 'error');
  assert.equal(h.provider.getState().windows[0].remainingPercent, 75);
  const state = await h.provider.refresh();
  assert.equal(state.status, 'connected');
  assert.equal(h.children.length, 2);
});

test('login timeout cancels official attempt and exposes only fixed safe error text', async (t) => {
  const h = await harness(t, { completeLogin: false, loginTimeoutMs: 20 });
  const state = await h.provider.login();
  assert.equal(state.status, 'error');
  assert.match(state.error, /登录已超时/);
  assert.ok(h.children[0].requests.some((request) => request.method === 'account/login/cancel'));
  assert.deepEqual(state.windows, []);
});

test('cancel and logout end pending login and clear account plus gauges', async (t) => {
  const h = await harness(t, { completeLogin: false });
  const pending = h.provider.login();
  while (!h.opened.length) await turn();
  h.children[0].notice('account/login/completed', { loginId: 'different-attempt', success: true });
  await turn();
  assert.equal(h.provider.getState().status, 'awaiting_login');
  await h.provider.cancelLogin();
  assert.equal((await pending).status, 'signed_out');
  assert.ok(h.children[0].requests.some((request) => request.method === 'account/login/cancel'));
  await h.provider.logout();
  assert.equal(h.provider.getState().status, 'signed_out');
  assert.deepEqual(h.provider.getState().windows, []);
  assert.ok(h.children[0].requests.some((request) => request.method === 'account/logout'));
});

test('logout removes exposed data while preserving token-free state contract', async (t) => {
  const h = await harness(t, { signedIn: true });
  await h.provider.start();
  const state = await h.provider.logout();
  assert.deepEqual(state, { id: 'codex', status: 'signed_out', account: null, plan: null,
    windows: [], updatedAt: null, error: null });
});

test('sign-out notification invalidates an older quota refresh still in flight', async (t) => {
  let requests = 0;
  let deferred;
  const h = await harness(t, { signedIn: true, handler: (request, child) => {
    if (request.method !== 'account/rateLimits/read' || ++requests === 1) return false;
    deferred = { request, child };
    return true;
  } });
  await h.provider.start();
  const pending = h.provider.refresh();
  await waitFor(() => deferred);
  deferred.child.notice('account/updated', { authMode: null, planType: null });
  deferred.child.result(deferred.request, { rateLimits: {
    primary: { usedPercent: 5, windowDurationMins: 300 } } });
  const state = await pending;
  assert.equal(state.status, 'signed_out');
  assert.deepEqual(state.windows, []);
});

test('reauthorization cannot merge a previous session weekly quota into a new account update', async (t) => {
  let accountReads = 0;
  let quotaReads = 0;
  let deferred;
  const h = await harness(t, { signedIn: true, handler: (request, child) => {
    if (request.method === 'account/read' && ++accountReads === 2) {
      child.send({ id: request.id, error: { code: 401, message: 'expired' } });
      return true;
    }
    if (request.method === 'account/rateLimits/read' && ++quotaReads > 1) {
      deferred = { request, child };
      return true;
    }
    return false;
  } });
  await h.provider.start();
  assert.equal((await h.provider.refresh()).status, 'signed_out');
  const pending = h.provider.login();
  await waitFor(() => deferred);
  deferred.child.notice('account/rateLimits/updated', { rateLimits: {
    primary: { usedPercent: 15, windowDurationMins: 300 } } });
  await waitFor(() => h.provider.getState().windows.length > 0);
  assert.equal(h.provider.getState().windows.length, 1);
  assert.equal(h.provider.getState().windows[0].id, 'five_hour');
  deferred.child.result(deferred.request, { rateLimits: {
    primary: { usedPercent: 15, windowDurationMins: 300 } } });
  assert.equal((await pending).windows.length, 1);
});

test('RPC error payload and stderr cannot leak credentials into application state', async (t) => {
  const h = await harness(t, { signedIn: true, handler: (request, child) => {
    if (request.method !== 'account/rateLimits/read') return false;
    child.stderr.write('access_token=PRIVATE_STDERR_TOKEN');
    child.send({ id: request.id, error: { code: 500, message: 'Bearer PRIVATE_SERVER_TOKEN',
      data: { refresh_token: 'PRIVATE_REFRESH_TOKEN' } } });
    return true;
  } });
  const state = await h.provider.start();
  assert.equal(state.status, 'error');
  assert.ok(!JSON.stringify(h.updates).includes('PRIVATE_'));
});

test('stop during login cancels callback server and ignores further completion', async (t) => {
  const h = await harness(t, { completeLogin: false });
  const pending = h.provider.login();
  while (!h.opened.length) await turn();
  const stopping = h.provider.stop();
  assert.equal(h.children[0].killed, true, 'process must be killed before the exit hook yields');
  await stopping;
  await pending;
  assert.equal(h.children[0].killed, true);
  assert.equal(h.provider.getState().status, 'signed_out');
});
