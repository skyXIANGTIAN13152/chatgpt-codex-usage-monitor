'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const NOW = Date.parse('2026-10-18T02:30:00Z');
const flush = async()=>{ for(let index=0; index<12; index++) await Promise.resolve(); };

function clock() {
  let now = NOW, nextId = 0;
  const timers = new Map();
  return { now:()=>now,
    setTimeout(callback, delay) { const token={id:++nextId,unref(){}};timers.set(token,{callback,due:now+delay});return token; },
    clearTimeout(token) { timers.delete(token); },
    jump(milliseconds) { now+=milliseconds; },
    nextDue() { return Math.min(...[...timers.values()].map(timer=>timer.due)); },
    async advance(milliseconds) {
      const end=now+milliseconds;
      for(let guard=0; guard<1000; guard++) {
        const next=[...timers.entries()].filter(([,timer])=>timer.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];
        if(!next) {now=end;await flush();return;}
        now=next[1].due;timers.delete(next[0]);next[1].callback();await flush();
      }
      throw new Error('Automatic refresh entered a timer loop');
    },
  };
}

function state(time, overrides={}) {
  return { id:'codex',status:'connected',updatedAt:new Date(time).toISOString(),error:null,
    windows:[{remainingPercent:75,resetsAt:new Date(time+3_600_000).toISOString()}],...overrides };
}

function harness(overrides={}) {
  const {RefreshScheduler}=require('../backend/refresh-scheduler.cjs');
  const fake=clock();
  const data={settings:{refreshSeconds:300},states:{codex:state(NOW)},calls:[],...overrides};
  const refresh=overrides.refresh||(async()=>{
    data.calls.push(fake.now());data.states.codex=state(fake.now());return 'refreshed';
  });
  const scheduler=new RefreshScheduler({refresh,getSettings:()=>data.settings,getStates:()=>data.states,
    now:fake.now,setTimeout:fake.setTimeout,clearTimeout:fake.clearTimeout});
  return {scheduler,fake,data};
}

test('ordinary host settings changes do not postpone the pending automatic refresh', async()=>{
  // Exercise the real host write boundary without launching Electron or providers.
  const source=fs.readFileSync(path.resolve(__dirname,'../main.cjs'),'utf8');
  const persist=source.slice(source.indexOf('async function persistSettingsPatch('),source.indexOf('function updateSettings('));
  let clock=0, nextDue=300_000;
  const context={settings:{refreshSeconds:300,paused:false},
    settingsStore:{update(patch){return {...context.settings,...patch};}},
    configureWatcher:async()=>{},applyWindowSettings:()=>{},
    scheduleRefresh:()=>{nextDue=clock+context.settings.refreshSeconds*1000;},
    refreshScheduler:{update(){}},updateEnergy:async()=>{},getSnapshot:()=>({}),localizedError:error=>error.message};
  vm.createContext(context);vm.runInContext(persist,context);
  clock=240_000;await context.persistSettingsPatch({paused:true});
  assert.equal(nextDue,300_000,'Pausing animation must not move the five-minute refresh to nine minutes');
});

test('host refresh coalesces calls and reports unexpected rejection without leaking diagnostics', async()=>{
  const source=fs.readFileSync(path.resolve(__dirname,'../main.cjs'),'utf8');
  const perform=source.slice(source.indexOf('function performRefreshAll('),source.indexOf('function scheduleRefresh('));
  let calls=0,publications=0;
  const context={refreshOperation:null,refreshScheduler:null,noProviders:false,quitting:false,
    providers:{codex:{async refresh(){calls++;throw new Error('private fixture diagnostic');}}},
    updateEnergy:async()=>{publications++;},getSnapshot:()=>({}),t:message=>message};
  vm.createContext(context);vm.runInContext(perform,context);
  const first=context.refreshAll(),second=context.refreshAll();assert.equal(first,second);
  await assert.rejects(first,error=>error.message==='操作未能完成，请重试。');
  assert.equal(calls,1);assert.equal(publications,1);
});

test('provider startup settling after quit cannot create a refresh timer', async()=>{
  const source=fs.readFileSync(path.resolve(__dirname,'../main.cjs'),'utf8');
  const {RefreshScheduler}=require('../backend/refresh-scheduler.cjs');
  const fake=clock();
  class ClockedScheduler extends RefreshScheduler {
    constructor(options) {super({...options,now:fake.now,setTimeout:fake.setTimeout,clearTimeout:fake.clearTimeout});}
  }
  class DeferredProvider {
    start() {return new Promise(resolve=>{this.finishStart=resolve;});}
    stop() {this.finishStart();}
  }
  let refreshCalls=0;
  const context={quitting:false,noProviders:false,providers:{},CodexProvider:DeferredProvider,ClaudeClient:DeferredProvider,
    path,RUNTIME_DIRECTORY:'fixture-runtime',APP_DIRECTORY:'fixture-app',dataDirectory:'fixture-profile',
    openExternal(){},providerUpdate(){},refreshScheduler:null,RefreshScheduler:ClockedScheduler,
    performRefreshAll:async()=>{refreshCalls++;},settings:{refreshSeconds:300},states:{},updateEnergy:async()=>{}};
  vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function scheduleRefresh('),source.indexOf('function constrainBounds(')),context);
  vm.runInContext(source.slice(source.indexOf('async function initializeProviders('),source.indexOf('if (!app.requestSingleInstanceLock())')),context);
  const starting=context.initializeProviders();
  context.quitting=true;
  context.refreshScheduler?.stop();
  for(const provider of Object.values(context.providers)) provider.stop();
  await starting;
  context.scheduleRefresh(); // A late settings callback must also remain harmless.
  try {
    assert.equal(context.refreshScheduler,null,'Shutdown must prevent late startup from creating the scheduler');
    assert.equal(fake.nextDue(),Infinity);
    await fake.advance(600_000);assert.equal(refreshCalls,0);
  } finally {context.refreshScheduler?.stop();}
});

test('late energy updates after quit cannot restart native classification or publish', async()=>{
  const source=fs.readFileSync(path.resolve(__dirname,'../main.cjs'),'utf8');
  let classifications=0,publications=0;
  const context={quitting:true,sendSnapshot(){publications++;},energySignature:'late-update',
    classifiedEnergySignature:null,pendingEnergyClassification:null,snapshotRevision:1,
    energyTarget:{remainingPercent:50},settings:{warningThreshold:20},
    core:{async classifyEnergy(){classifications++;return {}; }},unknownEnergy:()=>({}),synchronizeEnergyTarget(){}};
  vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function updateEnergy('),source.indexOf('function providerUpdate(')),context);
  await context.updateEnergy();
  assert.equal(classifications,0,'The stopped native helper must not be reconnected');
  assert.equal(publications,0);
});

test('a refresh request arriving during quit cannot restart providers', async()=>{
  const source=fs.readFileSync(path.resolve(__dirname,'../main.cjs'),'utf8');
  let refreshes=0,updates=0;
  const context={quitting:true,refreshOperation:null,refreshScheduler:null,noProviders:false,
    providers:{codex:{async refresh(){refreshes++;}}},updateEnergy:async()=>{updates++;},
    getSnapshot:()=>({}),t:message=>message};
  vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function performRefreshAll('),source.indexOf('function scheduleRefresh(')),context);
  await context.refreshAll();
  assert.equal(refreshes,0);assert.equal(updates,0);assert.equal(context.refreshOperation,null);
});

test('provider and display updates preserve periodic cadence; changing the interval updates it', async()=>{
  const {scheduler,fake,data}=harness();scheduler.start();
  await fake.advance(200_000);
  data.settings={...data.settings,paused:true,quotaDisplay:'rings'};scheduler.update();
  assert.equal(fake.nextDue(),NOW+300_000);
  data.states.codex=state(fake.now());scheduler.update();
  assert.equal(fake.nextDue(),NOW+300_000,'A provider push must not delay regular polling');
  data.settings.refreshSeconds=60;scheduler.update();
  assert.equal(fake.nextDue(),NOW+260_000);
  await fake.advance(60_000);assert.equal(data.calls.length,1);
  assert.equal(fake.nextDue(),NOW+320_000);scheduler.stop();
});

test('a quota reset triggers a real refresh before the regular interval', async()=>{
  const {scheduler,fake,data}=harness();
  data.states.codex.windows[0].resetsAt=new Date(NOW+10_000).toISOString();scheduler.start();
  await fake.advance(9_999);assert.equal(data.calls.length,0);
  await fake.advance(1);assert.deepEqual(data.calls,[NOW+10_000]);
  assert.equal(fake.nextDue(),NOW+310_000);scheduler.stop();
});

test('freshness expires strictly after the configured allowance and triggers a real refresh', async()=>{
  const {scheduler,fake,data}=harness();data.states.codex=state(NOW-240_000);scheduler.start();
  // A five-minute polling interval permits 420 seconds; this cache has 180 left.
  await fake.advance(180_000);assert.equal(data.calls.length,0);
  await fake.advance(1);assert.deepEqual(data.calls,[NOW+180_001]);scheduler.stop();
});

test('failed refreshes retry after at least 30 seconds despite frequent state updates', async()=>{
  const calls=[];
  const {scheduler,fake,data}=harness({refresh:async()=>{calls.push(fake.now());throw new Error('fixture offline');}});
  data.states.codex.windows[0].resetsAt=new Date(NOW+1_000).toISOString();scheduler.start();
  await fake.advance(1_000);assert.equal(calls.length,1);
  for(let index=0;index<5;index++){await fake.advance(5_000);scheduler.update();}
  assert.equal(calls.length,1);
  await fake.advance(4_999);assert.equal(calls.length,1);
  await fake.advance(1);assert.deepEqual(calls,[NOW+1_000,NOW+31_000]);scheduler.stop();
});

test('a rejected refresh retries even while its cached data is still fresh', async()=>{
  let calls=0;
  const {scheduler,fake,data}=harness({refresh:async()=>{
    calls++;
    if(calls===1) throw new Error('fixture request failed');
    data.states.codex=state(fake.now());
  }});
  scheduler.start();await assert.rejects(scheduler.refreshNow(),/fixture request failed/);
  await fake.advance(5_000);data.settings.paused=true;scheduler.update();
  assert.equal(fake.nextDue(),NOW+30_000,'A display update must not cancel or delay a failed request retry');
  await fake.advance(24_999);assert.equal(calls,1);
  await fake.advance(1);assert.equal(calls,2);
  assert.equal(fake.nextDue(),NOW+330_000);scheduler.stop();
});

test('provider errors returned as state still retry; sign-in and first setup wait for the user', async()=>{
  const {scheduler,fake,data}=harness();
  data.states.codex=state(NOW,{status:'error',error:'fixture offline'});scheduler.start();
  await fake.advance(29_999);assert.equal(data.calls.length,0);
  await fake.advance(1);assert.equal(data.calls.length,1);
  data.states.codex=state(fake.now(),{status:'awaiting_login',windows:[],updatedAt:null});scheduler.update();
  await fake.advance(30_000);assert.equal(data.calls.length,1);
  data.states.codex=state(fake.now(),{status:'quota_pending',needsSetup:true,windows:[]});scheduler.update();
  await fake.advance(30_000);assert.equal(data.calls.length,1);scheduler.stop();
});

test('manual, deadline and resume requests coalesce while refresh is running', async()=>{
  let resolve, calls=0;
  const pending=new Promise(done=>{resolve=done;});
  const {scheduler,fake,data}=harness({refresh:()=>{calls++;return pending;}});scheduler.start();
  const first=scheduler.refreshNow();const second=scheduler.refreshNow();
  assert.equal(first,second);await flush();assert.equal(calls,1);
  await fake.advance(600_000);assert.equal(scheduler.resume(),first);
  assert.equal(calls,1);
  data.states.codex=state(fake.now());resolve('finished');assert.equal(await first,'finished');await flush();
  assert.equal(fake.nextDue(),fake.now()+300_000);scheduler.stop();
});

test('resume refreshes immediately after sleep and stop prevents later automatic work', async()=>{
  const {scheduler,fake,data}=harness();scheduler.start();fake.jump(900_000);
  await scheduler.resume();assert.deepEqual(data.calls,[NOW+900_000]);
  scheduler.stop();await fake.advance(3_600_000);assert.equal(data.calls.length,1);
});

test('a pending quota response keeps retrying without fabricating freshness or resetting its value', async()=>{
  let calls=0;
  const {scheduler,fake,data}=harness({refresh:async()=>{calls++;return data.states.codex;}});
  const original=state(NOW,{status:'quota_pending',windows:[]});data.states.codex=original;scheduler.start();
  await fake.advance(90_000);assert.equal(calls,3);
  assert.deepEqual(data.states.codex,original);scheduler.stop();
});
