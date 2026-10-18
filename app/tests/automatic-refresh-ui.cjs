'use strict';
// Real Electron main/IPC/settings/native classification with fake account I/O.
// Only the scheduler clock/timers and provider boundary are replaced. Five
// simulated minutes advance explicitly; this is not an online five-minute test.
const { _electron: electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'automatic-refresh-ui-'));
const data = path.join(folder, 'profile');
fs.mkdirSync(data);
fs.writeFileSync(path.join(data, 'settings.json'), JSON.stringify({
  ...require('../backend/settings-store.cjs').DEFAULT_SETTINGS,
  refreshSeconds: 300, followSystemMotion: false, reducedMotion: false,
}));
const bootstrap = path.join(folder, 'bootstrap.cjs');
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;

fs.writeFileSync(bootstrap, `
'use strict';
const {BrowserWindow}=require('electron');
const test=global.__automaticRefresh={started:Date.now(),now:Date.now(),providers:{},
  tasks:new Map(),nextTimer:0,refreshCalls:[],publications:[],nativeInputs:[],nativePending:new Set()};
test.now=test.started;
Date.now=()=>test.now;
test.flush=async()=>{
  for(let i=0;i<4;i++){
    await new Promise(resolve=>setImmediate(resolve));
    if(test.nativePending.size)await Promise.allSettled([...test.nativePending]);
  }
};
test.syncRendererClocks=async()=>{
  for(const win of BrowserWindow.getAllWindows()){
    if(win.isDestroyed()||!win.webContents.getURL().startsWith('file:'))continue;
    await win.webContents.executeJavaScript('window.__automaticTestNow='+test.now+';Date.now=()=>window.__automaticTestNow;void 0;');
  }
};
test.inspect=()=>({now:test.now,started:test.started,
  scheduled:[...test.tasks.values()].map(({id,due})=>({id,due})),
  periodicAt:test.scheduler?.periodicAt,pending:!!test.scheduler?.pending,
  refreshCalls:structuredClone(test.refreshCalls),nativeInputs:structuredClone(test.nativeInputs)});
test.advance=async(target,settle=true)=>{
  if(target<test.now)throw new Error('The fixture clock must not move backwards');
  let turns=0;
  while(true){
    const next=[...test.tasks.values()].filter(item=>item.due<=target).sort((a,b)=>a.due-b.due||a.id-b.id)[0];
    if(!next)break;
    if(++turns>100)throw new Error('Scheduler unexpectedly spins at a deadline');
    test.now=next.due;await test.syncRendererClocks();
    test.tasks.delete(next.id);next.fn();
    if(settle&&test.scheduler.pending)await test.scheduler.pending;
    await test.flush();
  }
  test.now=target;await test.syncRendererClocks();await test.flush();
  return test.inspect();
};
const schedulerModule=require(${JSON.stringify(path.join(root, 'backend/refresh-scheduler.cjs'))});
const RealScheduler=schedulerModule.RefreshScheduler;
schedulerModule.RefreshScheduler=class extends RealScheduler{
  constructor(options){
    super({...options,now:()=>test.now,setTimeout:(fn,ms)=>{
      const token={id:++test.nextTimer,due:test.now+ms,fn,unref(){}};
      test.tasks.set(token.id,token);return token;
    },clearTimeout:token=>{if(token)test.tasks.delete(token.id);}});
    test.scheduler=this;
  }
};
const empty=id=>({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
function provider(id){return class FakeProvider{
  constructor({onUpdate}){this.onUpdate=onUpdate;this.state=empty(id);this.nextValue=75;
    this.holdNext=false;this.release=null;test.providers[id]=this;}
  getState(){return structuredClone(this.state);}
  publish(state=this.state){this.state=state;const copy=this.getState();
    test.publications.push({time:test.now,state:copy});this.onUpdate(copy);return this.getState();}
  quota(value){return {id,status:'connected',account:'fixture@example.invalid',plan:'fixture',
    windows:[{id:'five_hour',label:'5 小时',windowMinutes:300,remainingPercent:value,usedPercent:100-value,
      resetsAt:new Date(test.now+3600000).toISOString()},
      {id:'weekly',label:'总周',windowMinutes:10080,remainingPercent:84,usedPercent:16,
        resetsAt:new Date(test.now+7200000).toISOString()}],
    updatedAt:new Date(test.now).toISOString(),error:null};}
  async start(){return this.publish(id==='codex'?this.quota(75):empty(id));}
  async refresh(){
    test.refreshCalls.push({id,time:test.now});
    if(id!=='codex')return this.publish(empty(id));
    this.publish({...this.state,status:'connecting'});
    if(this.holdNext){this.holdNext=false;await new Promise(resolve=>{this.release=()=>{this.release=null;resolve();};});}
    return this.publish(this.quota(this.nextValue));
  }
  async login(){throw new Error('The regression must never start authentication');}
  async logout(){return this.publish(empty(id));}
  stop(){this.release?.();}
};}
require(${JSON.stringify(path.join(root, 'backend/codex-provider.cjs'))}).CodexProvider=provider('codex');
require(${JSON.stringify(path.join(root, 'backend/claude-client.cjs'))}).ClaudeClient=provider('claude');
const {CoreBridge}=require(${JSON.stringify(path.join(root, 'backend/core-bridge.cjs'))});
const classify=CoreBridge.prototype.classifyEnergy;
CoreBridge.prototype.classifyEnergy=function(...args){
  test.nativeInputs.push(args);const actual=classify.apply(this,args);test.nativePending.add(actual);
  actual.then(()=>test.nativePending.delete(actual),()=>test.nativePending.delete(actual));return actual;
};
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);

async function waitSnapshot(page, expected) {
  await page.waitForFunction(async expected => {
    const snapshot = await window.monitor.getSnapshot();
    return Object.entries(expected).every(([key, value]) => JSON.stringify(
      key.split('.').reduce((current, part) => current?.[part], snapshot)) === JSON.stringify(value));
  }, expected, { polling: 50, timeout: 10000 });
  return page.evaluate(() => window.monitor.getSnapshot());
}

async function expectMeters(page, mode, short, weekly = 84) {
  await page.waitForFunction(({ mode, short, weekly }) => {
    const provider = document.querySelector('[data-provider="codex"]');
    if (!provider) return false;
    const meter = period => provider.querySelector(mode === 'rings'
      ? '.quota-ring[data-period="' + period + '"]'
      : '.quota-window[data-period="' + period + '"] .quota-track');
    return [['short', short], ['weekly', weekly]].every(([period, value]) =>
      meter(period) && meter(period).getAttribute('aria-valuenow') === (value === null ? null : String(value)));
  }, { mode, short, weekly }, { timeout: 10000 });
  assert.equal(await page.locator(mode === 'rings' ? '.provider .quota-track' : '.provider .quota-ring').count(), 0);
}

(async () => {
  let app, page;
  const checks = [], errors = [];
  try {
    app = await electron.launch({ executablePath: require('electron'),
      args: [bootstrap, '--test-data-dir', data], cwd: root, env });
    page = await app.firstWindow();
    page.on('pageerror', error => errors.push(error.message));
    await page.waitForFunction(() => Boolean(window.monitor));
    await waitSnapshot(page, { 'settings.refreshSeconds': 300, 'energyTarget.remainingPercent': 75, 'energy.scene': 'full' });
    await app.evaluate(async () => {
      if (!global.__automaticRefresh.scheduler?.running) throw new Error('The real main process did not start RefreshScheduler');
      await global.__automaticRefresh.syncRendererClocks();
    });
    const initial = await page.evaluate(() => window.monitor.getSnapshot());
    const started = await app.evaluate(() => global.__automaticRefresh.started);
    const originalDue = started + 300000;
    const inspect = () => app.evaluate(() => global.__automaticRefresh.inspect());
    const assertDue = async due => {
      const details = await inspect();
      assert.equal(details.scheduled.length, 1, 'One scheduler timer remains armed');
      assert.equal(details.scheduled[0].due, due, 'The next poll keeps its original deadline');
      return details;
    };
    await assertDue(originalDue);
    await expectMeters(page, 'bars', 75);
    checks.push('real-main-starts-scheduler-with-300-second-setting');

    // Only scheduler time advances. Main's normal five-second snapshot tick and
    // the renderer's normal tick still run, so age rendering is not injected.
    await app.evaluate(async (_electron, time) => global.__automaticRefresh.advance(time), started + 240000);
    await page.waitForFunction(() => document.querySelector('[data-provider="codex"] .provider-status')?.textContent.includes('4分钟前刷新'), null, { timeout: 8000 });
    await expectMeters(page, 'bars', 75);
    let snapshot = await waitSnapshot(page, { 'energyTarget.remainingPercent': 75, 'energy.scene': 'full', 'providers.codex.status': 'connected' });
    assert.equal(snapshot.providers.codex.updatedAt, initial.providers.codex.updatedAt);
    assert.equal((await inspect()).refreshCalls.length, 0, 'Four simulated minutes did not force a premature polling request');
    checks.push('four-minute-cache-remains-valid-in-bars-with-connected-account');

    const opening = app.waitForEvent('window');
    await page.evaluate(() => window.monitor.windowAction('settings'));
    const settingsPage = await opening;
    settingsPage.on('pageerror', error => errors.push(error.message));
    await settingsPage.locator('#quota-display').waitFor();
    await app.evaluate(() => global.__automaticRefresh.syncRendererClocks());
    await settingsPage.locator('#quota-display').selectOption('rings');
    await waitSnapshot(page, { 'settings.quotaDisplay': 'rings' });
    await expectMeters(page, 'rings', 75);
    await assertDue(originalDue);
    checks.push('four-minute-cache-remains-valid-in-concentric-rings');

    await page.locator('#pause').click();
    await waitSnapshot(page, { 'settings.paused': true });
    await assertDue(originalDue);
    await page.locator('#pin').click();
    await waitSnapshot(page, { 'settings.alwaysOnTop': !initial.settings.alwaysOnTop });
    await assertDue(originalDue);
    await settingsPage.locator('#show-codex').uncheck();
    await waitSnapshot(page, { 'settings.visible.codex': false });
    await assertDue(originalDue);
    await settingsPage.locator('#show-codex').check();
    await waitSnapshot(page, { 'settings.visible.codex': true });
    await settingsPage.locator('#quota-display').selectOption('bars');
    await waitSnapshot(page, { 'settings.quotaDisplay': 'bars' });
    await expectMeters(page, 'bars', 75);
    assert.equal((await assertDue(originalDue)).refreshCalls.length, 0);
    checks.push('pause-pin-visibility-and-display-mode-do-not-postpone-polling');

    await app.evaluate(async (_electron, time) => {
      const test = global.__automaticRefresh;
      test.providers.codex.nextValue = 15;
      return test.advance(time);
    }, originalDue - 1);
    assert.equal((await inspect()).refreshCalls.filter(call => call.id === 'codex').length, 0);
    await app.evaluate(async (_electron, time) => global.__automaticRefresh.advance(time), originalDue);
    snapshot = await waitSnapshot(page, { 'providers.codex.status': 'connected', 'energyTarget.remainingPercent': 15, 'energy.scene': 'low' });
    await expectMeters(page, 'bars', 15);
    assert.equal(Date.parse(snapshot.providers.codex.updatedAt), originalDue);
    let details = await inspect();
    assert.deepEqual(details.refreshCalls.filter(call => call.id === 'codex').map(call => call.time), [originalDue]);
    assert(details.nativeInputs.some(args => args[0] === 15), 'The actual native classifier receives the refreshed allowance');
    await assertDue(originalDue + 300000);
    checks.push('timer-deadline-refreshes-real-main-provider-path-and-native-energy');

    // A fixture provider now declares a reset earlier than the polling interval.
    // Hold that next response to prove reset never fabricates a fresh 100% value.
    const resetAt = originalDue + 20000;
    await app.evaluate((_electron, reset) => {
      const provider = global.__automaticRefresh.providers.codex;
      provider.nextValue = 0; provider.holdNext = true;
      provider.publish({ ...provider.state, windows: provider.state.windows.map(window =>
        window.windowMinutes === 300 ? { ...window, resetsAt: new Date(reset).toISOString() } : window) });
    }, resetAt);
    await assertDue(resetAt);
    await app.evaluate(async (_electron, time) => global.__automaticRefresh.advance(time), resetAt - 1);
    assert.equal((await inspect()).refreshCalls.filter(call => call.id === 'codex').length, 1);
    await app.evaluate(async (_electron, time) => global.__automaticRefresh.advance(time, false), resetAt);
    snapshot = await waitSnapshot(page, { 'providers.codex.status': 'connecting', 'energyTarget.remainingPercent': null, 'energy.scene': 'unknown' });
    await expectMeters(page, 'bars', null);
    assert.equal(snapshot.providers.codex.windows[0].remainingPercent, 15, 'Expiry does not invent a new quota reading');
    assert.equal(Date.parse(snapshot.providers.codex.updatedAt), originalDue, 'A request in flight does not refresh the data timestamp');
    await app.evaluate(async () => {
      const test = global.__automaticRefresh;
      test.providers.codex.release();
      await test.scheduler.pending; await test.flush();
    });
    snapshot = await waitSnapshot(page, { 'energyTarget.remainingPercent': 0, 'energy.scene': 'stone', 'providers.codex.status': 'connected' });
    await expectMeters(page, 'bars', 0);
    assert.equal(Date.parse(snapshot.providers.codex.updatedAt), resetAt);
    checks.push('reset-deadline-refreshes-automatically-and-waits-for-real-zero-response');

    const resumedAt = resetAt + 30000;
    await app.evaluate(async (_electron, time) => global.__automaticRefresh.advance(time), resumedAt);
    const callsBeforeResume = (await inspect()).refreshCalls.filter(call => call.id === 'codex').length;
    await app.evaluate(async ({ powerMonitor }) => {
      const test = global.__automaticRefresh;
      test.providers.codex.nextValue = 85;
      powerMonitor.emit('resume');
      if (!test.scheduler.pending) throw new Error('The real main resume listener did not start a provider refresh');
      await test.scheduler.pending; await test.flush();
    });
    snapshot = await waitSnapshot(page, { 'energyTarget.remainingPercent': 85, 'energy.scene': 'full' });
    await expectMeters(page, 'bars', 85);
    details = await inspect();
    assert.equal(details.refreshCalls.filter(call => call.id === 'codex').length, callsBeforeResume + 1);
    assert.equal(Date.parse(snapshot.providers.codex.updatedAt), resumedAt);
    assert.equal(snapshot.settings.paused, true, 'Pausing artwork never pauses allowance refresh');
    await assertDue(resumedAt + 300000);
    checks.push('actual-electron-resume-event-refreshes-while-artwork-is-paused');
    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify({ checks, details, snapshot }, null, 2));
    console.log(JSON.stringify({ ok: true, checks, simulatedSeconds: (resumedAt - started) / 1000,
      clock: 'Manually advanced scheduler/host/renderer Date.now; no five-minute online wait',
      provider: 'Fake account boundary, isolated profile; no real account or credential access',
      host: 'Actual main IPC, SettingsStore, RefreshScheduler, Electron resume event and native monitor-core.exe',
      evidence: folder }));
  } catch (error) {
    if (app) {
      try {
        const host = await app.evaluate(() => ({ ...global.__automaticRefresh.inspect(), publications: global.__automaticRefresh.publications }));
        const snapshot = page ? await page.evaluate(() => window.monitor.getSnapshot()) : null;
        fs.writeFileSync(path.join(folder, 'failure.json'), JSON.stringify({ error: error.stack, errors, host, snapshot }, null, 2));
      } catch {}
      console.error('Automatic-refresh diagnostic directory:', folder);
    }
    throw error;
  } finally { if (app) await app.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
