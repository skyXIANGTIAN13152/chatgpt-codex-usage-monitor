'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { createEnvironment, parseUsageScreen, statusLineWindows, ClaudeProvider } = require('../backend/claude-provider.cjs');

test('Claude children receive only the isolated account environment', () => {
  const env = createEnvironment('D:/isolated/claude', {
    PATH: 'native-tools', USERPROFILE: 'C:/Users/example',
    ANTHROPIC_API_KEY: 'do-not-forward', CLAUDE_CODE_OAUTH_TOKEN: 'do-not-forward',
    CLAUDE_CONFIG_DIR: 'C:/other-account', CLAUDE_CODE_USE_BEDROCK: '1', CLAUDECODE: '1',
  });
  assert.equal(env.CLAUDE_CONFIG_DIR, 'D:/isolated/claude');
  assert.equal(env.ANTHROPIC_API_KEY, undefined);
  assert.equal(env.CLAUDE_CODE_OAUTH_TOKEN, undefined);
  assert.equal(env.CLAUDE_CODE_USE_BEDROCK, undefined);
  assert.equal(env.CLAUDECODE, undefined);
  assert.equal(env.PATH, 'native-tools');
});

test('TUI parsing distinguishes real quota sections from context and spend', () => {
  const windows = parseUsageScreen(`
Context window 99% used
Current session
████░░ 12% used
Resets in 2 hr 30 min
Current week (all models)
51.5% used
Resets Oct 20 at 9am (Australia/Sydney)
Current week (Sonnet only)
90% remaining
Extra usage
100% used
`, Date.UTC(2026, 9, 16, 0));
  assert.equal(windows.length, 3);
  assert.equal(windows[0].remainingPercent, 88);
  assert.equal(windows[0].resetsAt, '2026-10-16T02:30:00.000Z');
  assert.equal(windows[1].remainingPercent, 48.5);
  assert.equal(windows[1].resetsAt, null);
  assert.equal(windows[2].usedPercent, 10);
});

test('unknown or out-of-range TUI data never invents quota', () => {
  assert.deepEqual(parseUsageScreen('Context usage\n25% used\nCost\n20% remaining'), []);
  assert.deepEqual(parseUsageScreen('Current session\n105% used'), []);
  assert.deepEqual(parseUsageScreen('Current session\n--\nCurrent week (all models)\nLoading...'), []);
});

test('statusLine imports documented windows and discards past or missing resets', () => {
  const now = Date.UTC(2026, 9, 16, 0);
  const windows = statusLineWindows({ rate_limits: {
    five_hour: { used_percentage: 22.5, resets_at: now / 1000 + 600 },
    seven_day: { used_percentage: 0, resets_at: now / 1000 - 60 },
    spend_limit: { used_percentage: 80, resets_at: now / 1000 + 600 },
  } }, now);
  assert.equal(windows.length, 1);
  assert.equal(windows[0].remainingPercent, 77.5);
  assert.deepEqual(statusLineWindows({ rate_limits: {five_hour: {used_percentage: 1}} }, now), []);
});

test('a missing binary stays signed out and a website visit does not connect', async () => {
  const path = require('node:path');
  const fs = require('node:fs');
  const directory = path.join(__dirname, '.claude-fixture');
  let opened;
  const provider = new ClaudeProvider({ dataDir: directory, openExternal: url => { opened = url; } });
  try {
    assert.equal((await provider.start()).status, 'signed_out');
    await provider.openUsagePage();
    assert.equal(opened, 'https://claude.ai/settings/usage');
    assert.equal(provider.getState().status, 'signed_out');
  } finally {
    provider.stop();
    fs.rmSync(directory, {recursive:true,force:true});
  }
});

test('official setup opens only on an explicit action, passes paths as data, and refreshes on exit', async () => {
  const path = require('node:path');
  const fs = require('node:fs');
  const { EventEmitter } = require('node:events');
  const directory = path.resolve(__dirname, '.claude setup & fixture');
  const calls = [];
  const child = new EventEmitter();
  child.kill = () => {};
  const provider = new ClaudeProvider({ executable: process.execPath, dataDir: directory,
    spawnProcess: (...args) => { calls.push(args); return child; } });
  let refreshed = 0;
  provider.refresh = async () => { refreshed += 1; return provider.getState(); };
  try {
    await provider.openSetup();
    assert.equal(calls.length, 0);
    provider.state = {...provider.state,status:'quota_pending',needsSetup:true};
    assert.equal((await provider.openSetup()).needsSetup,true);
    assert.equal(calls.length,1);
    const [command,args,options] = calls[0];
    assert.equal(path.basename(command),'powershell.exe');
    assert.equal(args[args.indexOf('-Executable')+1],process.execPath);
    assert.equal(args[args.indexOf('-ProfileDirectory')+1],path.join(directory,'claude-profile'));
    assert.equal(options.env.CLAUDE_CONFIG_DIR,path.join(directory,'claude-profile'));
    assert.equal(options.shell,false);
    assert.equal(options.windowsHide,true);
    assert.equal(args.includes('-Command'),false);
    await provider.openSetup();
    assert.equal(calls.length,1);
    child.emit('close',1);
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(refreshed,1);
  } finally {
    provider.stop();
    assert.equal(path.dirname(directory),path.resolve(__dirname));
    fs.rmSync(directory,{recursive:true,force:true});
  }
});
