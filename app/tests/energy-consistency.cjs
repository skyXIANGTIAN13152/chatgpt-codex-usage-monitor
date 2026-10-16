'use strict';
// Isolated process regression: fake account boundaries and controllable delays
// around the real native classifier/watcher boundary. No account login or GUI.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { selectEnergyTarget } = require('../backend/energy-selection.cjs');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, {recursive:true});
const folder = fs.mkdtempSync(path.join(qa, 'energy-consistency-'));
const data = path.join(folder, 'profile');
const bootstrap = path.join(folder, 'bootstrap.cjs');
const env = {...process.env}; delete env.ELECTRON_RUN_AS_NODE;

fs.writeFileSync(bootstrap, `
'use strict';
const {app,BrowserWindow}=require('electron');
BrowserWindow.prototype.showInactive=function(){};
global.__consistency={now:Date.now(),providers:{},publications:[],broadcasts:[],jobs:[],watcherJobs:[],delayCore:false,delayWatcher:false};
const test=global.__consistency;
// Source timestamps and freshness checks share the provider host's clock. Node
// and Chromium clocks on Windows can differ by a few milliseconds; generating
// fixtures in the runner could otherwise create accidental future timestamps.
Date.now=()=>test.now;
app.on('web-contents-created',(_event,contents)=>{
 const send=contents.send;
 contents.send=function(channel,...args){
  if(channel==='monitor:snapshot')test.broadcasts.push({time:Date.now(),snapshot:structuredClone(args[0])});
  return send.call(this,channel,...args);
 };
});
const empty=id=>({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
function provider(id){return class {
 constructor({onUpdate}){this.onUpdate=onUpdate;this.state=empty(id);test.providers[id]=this;}
 publish(value=this.state){this.state={...value,id};test.publications.push({time:Date.now(),state:structuredClone(this.state)});this.onUpdate(structuredClone(this.state));return this.state;}
 async start(){return this.publish();} async refresh(){return this.publish();}
 async login(){return this.publish();} async logout(){return this.publish(empty(id));} stop(){}
};}
require(${JSON.stringify(path.join(root,'backend/codex-provider.cjs'))}).CodexProvider=provider('codex');
require(${JSON.stringify(path.join(root,'backend/claude-client.cjs'))}).ClaudeClient=provider('claude');
const {CoreBridge}=require(${JSON.stringify(path.join(root,'backend/core-bridge.cjs'))});
const classify=CoreBridge.prototype.classifyEnergy;
CoreBridge.prototype.classifyEnergy=function(...args){
 const actual=classify.apply(this,args);
 if(!test.delayCore)return actual;
 const settled=actual.then(value=>({value}),error=>({error}));
 return new Promise((resolve,reject)=>{
  const job={id:test.jobs.length,args,released:false,release:async()=>{
   if(job.released)return;job.released=true;
   const result=await settled;if(result.error)reject(result.error);else resolve(result.value);
  }};test.jobs.push(job);
 });
};
require(${JSON.stringify(path.join(root,'backend/launch-watcher.cjs'))}).configureLaunchWatcher=async ({settings})=>{
 if(!test.delayWatcher)return;
 return new Promise((resolve,reject)=>{const job={settings:structuredClone(settings),released:false,
  release:fail=>{job.released=true;fail?reject(new Error('fixture watcher failure')):resolve();}};
  test.watcherJobs.push(job);});
};
require(${JSON.stringify(path.join(root,'main.cjs'))});
`);

function fixture(id, value, resetOffset=3600000) {
  return {id,value,resetOffset};
}

(async()=>{
 let app;
 try {
  app=await _electron.launch({executablePath:require('electron'),args:[bootstrap,'--test-data-dir',data],cwd:root,env});
  const page=await app.firstWindow();
  await page.waitForFunction(()=>Boolean(window.monitor),null,{polling:50});
  await page.evaluate(()=>window.monitor.getSnapshot());
  await page.evaluate(()=>{window.__seen=[];window.monitor.subscribe(snapshot=>window.__seen.push({time:Date.now(),snapshot}));});
  const publish=async fixtures=>app.evaluate((_electron,fixtures)=>{
    for(const [id,{value,resetOffset}] of Object.entries(fixtures)){
      const now=Date.now();
      global.__consistency.providers[id].publish({id,status:'connected',
        account:`fixture-${id}@example.invalid`,plan:'fixture',
        windows:[{id:'five_hour',label:'5 小时',remainingPercent:value,usedPercent:100-value,windowMinutes:300,
          resetsAt:new Date(now+resetOffset).toISOString()}],updatedAt:new Date(now).toISOString(),error:null});
    }
  },fixtures);
  const wait=async expected=>{
    try {
      await page.waitForFunction(expected=>{
        const snapshot=window.__seen.at(-1)?.snapshot;
        return snapshot&&Object.entries(expected).every(([key,value])=>JSON.stringify(
          key.split('.').reduce((result,part)=>result?.[part],snapshot))===JSON.stringify(value));
      },expected,{polling:50,timeout:10000});
    } catch (error) {
      const diagnostic={expected,
        broadcasts:await page.evaluate(()=>window.__seen.slice(-10)),
        host:await app.evaluate(()=>({time:Date.now(),publications:global.__consistency.publications,
          broadcasts:global.__consistency.broadcasts,
          jobs:global.__consistency.jobs.map(({id,args,released})=>({id,args,released}))}))};
      const file=path.join(folder,'failure.json');
      fs.writeFileSync(file,JSON.stringify(diagnostic,null,2));
      console.error(`Consistency failure trace: ${file}`);
      throw error;
    }
    return page.evaluate(()=>window.__seen.at(-1).snapshot);
  };
  const delayCore=flag=>app.evaluate((_electron,flag)=>{global.__consistency.delayCore=flag;},flag);
  const release=async (value,threshold=20)=>{
    const found=await app.evaluate(async (_electron,{value,threshold})=>{
      const job=global.__consistency.jobs.find(job=>!job.released&&job.args[0]===value&&job.args[1]===threshold);
      if(!job)return false;await job.release();return true;
    },{value,threshold});
    assert.equal(found,true,`pending native classification ${value}/${threshold}`);
  };
  const requestSettings=patch=>page.evaluate(patch=>{
    window.__settingsResult=null;
    window.monitor.updateSettings(patch).then(value=>{window.__settingsResult={ok:true,value};},
      error=>{window.__settingsResult={ok:false,error:error.message};});
  },patch);
  const waitWatcher=()=>app.evaluate(async()=>{
    for(let attempt=0;attempt<100;attempt++){
      if(global.__consistency.watcherJobs.some(job=>!job.released))return;
      await new Promise(resolve=>setTimeout(resolve,20));
    }
    throw new Error('watcher request did not arrive');
  });
  const releaseWatcher=fail=>app.evaluate((_electron,fail)=>{
    const job=global.__consistency.watcherJobs.find(job=>!job.released);
    if(!job)throw new Error('No pending watcher request');job.release(fail);
  },fail);

  await publish({codex:fixture('codex',0),claude:fixture('claude',90)});
  await wait({'energyTarget.remainingPercent':0,'energy.scene':'stone'});
  await delayCore(true);
  await publish({codex:fixture('codex',100)});
  await wait({'energyTarget.remainingPercent':100,'energy.scene':'unknown'});
  await publish({codex:fixture('codex',0)});
  await wait({'energyTarget.remainingPercent':0,'energy.scene':'unknown'});
  await release(0);
  await wait({'energyTarget.remainingPercent':0,'energy.scene':'stone'});
  await release(100);
  await wait({'energyTarget.remainingPercent':0,'energy.scene':'stone'});

  const beforeStable=await page.evaluate(()=>window.__seen.length);
  const jobsBefore=await app.evaluate(()=>global.__consistency.jobs.length);
  await publish({codex:fixture('codex',0)});
  await wait({'energyTarget.remainingPercent':0,'energy.scene':'stone'});
  const stable=await page.evaluate(index=>window.__seen.slice(index),beforeStable);
  assert(stable.length>0&&stable.every(entry=>entry.snapshot.energy.scene==='stone'));
  assert.equal(await app.evaluate(()=>global.__consistency.jobs.length),jobsBefore);

  await publish({codex:fixture('codex',50,500)});
  await wait({'energyTarget.remainingPercent':50,'energy.scene':'unknown'});
  await app.evaluate(()=>{global.__consistency.now+=700;});
  await release(50);
  await wait({'energyTarget.remainingPercent':null,'energy.scene':'unknown'});
  await release(null);
  await delayCore(false);
  await publish({codex:fixture('codex',10)});
  await wait({'energyTarget.remainingPercent':10,'energy.scene':'low'});

  await app.evaluate(()=>{global.__consistency.delayWatcher=true;});
  await requestSettings({energyTarget:'claude'});
  await waitWatcher();
  await publish({codex:fixture('codex',12)});
  await wait({'settings.energyTarget':'codex','energyTarget.mode':'codex','energyTarget.remainingPercent':12,'energy.scene':'low'});
  await releaseWatcher(true);
  await page.waitForFunction(()=>window.__settingsResult?.ok===false,null,{polling:50});
  assert.equal(JSON.parse(fs.readFileSync(path.join(data,'settings.json'),'utf8')).energyTarget,'codex');

  await delayCore(true);
  await requestSettings({energyTarget:'claude'});
  await waitWatcher();
  await releaseWatcher(false);
  await wait({'settings.energyTarget':'claude','energyTarget.mode':'claude','energyTarget.remainingPercent':90,'energy.scene':'unknown'});
  await release(90);
  await wait({'energyTarget.remainingPercent':90,'energy.scene':'full'});
  await page.waitForFunction(()=>window.__settingsResult?.ok===true,null,{polling:50});
  await delayCore(false);
  await app.evaluate(()=>{global.__consistency.delayWatcher=false;});
  await publish({claude:fixture('claude',30)});
  await wait({'energyTarget.remainingPercent':30,'energy.scene':'full'});
  await delayCore(true);
  await requestSettings({warningThreshold:40});
  await wait({'settings.warningThreshold':40,'energyTarget.remainingPercent':30,'energy.scene':'unknown'});
  await release(30,40);
  await wait({'energy.scene':'low'});
  const preview=await page.evaluate(()=>window.monitor.windowAction('preview-full'));
  assert.equal(preview.energy.scene,'low');assert.equal(preview.previewScene,'full');

  // Audit at the actual send boundary, not at a renderer arrival timestamp
  // from a separate clock or after an arbitrary IPC delivery delay.
  const seen=await app.evaluate(()=>global.__consistency.broadcasts);
  for(const {time,snapshot} of seen){
    assert.deepEqual(snapshot.energyTarget,selectEnergyTarget(snapshot.providers,snapshot.settings,time));
    if(snapshot.energy.scene!=='unknown'){
      const value=snapshot.energyTarget.remainingPercent;
      assert.notEqual(value,null);
      const expected=value<=0?'stone':value<=snapshot.settings.warningThreshold?'low':'full';
      assert.equal(snapshot.energy.scene,expected);
    }
  }
  console.log(JSON.stringify({ok:true,broadcasts:seen.length,checks:[
    'every-broadcast-matches-providers-and-settings','delayed-old-result-cannot-overwrite-new-target',
    'identical-refresh-keeps-known-energy','reset-during-classification-becomes-unknown',
    'watcher-candidates-not-published-before-success','watcher-failure-preserves-active-settings',
    'changed-target-and-threshold-wait-with-unknown','preview-stays-separate',
    'provider-fixtures-and-send-audits-use-host-clock','reset-time-advanced-without-wall-clock-race'],nativeCore:'actual monitor-core.exe',visibleWindows:0}));
 }finally{if(app)await app.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
