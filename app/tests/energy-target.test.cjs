'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { selectEnergyTarget, selectFreshRemaining } = require('../backend/energy-selection.cjs');
const { SettingsStore, DEFAULT_SETTINGS, validatePatch, readSafeSettings } = require('../backend/settings-store.cjs');

const NOW = Date.UTC(2026, 9, 17, 0);
const future = new Date(NOW + 3600000).toISOString();
const windowQuota = (remainingPercent, windowMinutes = 300, extras = {}) => ({
  id: windowMinutes === 300 ? 'primary' : 'secondary',
  label: windowMinutes === 300 ? '5 小时' : '每周',
  remainingPercent, windowMinutes, resetsAt: future, ...extras,
});
const account = (windows, extras = {}) => ({
  status: 'connected', updatedAt: new Date(NOW - 1000).toISOString(), windows, ...extras,
});
const aggregate = { ...DEFAULT_SETTINGS, energyTarget: 'aggregate' };
const selected = (providers, settings = aggregate) => selectEnergyTarget(providers, settings, NOW);

test('single target prioritizes five-hour quota while preserving independent custom readings', () => {
  const providers = { codex: account([windowQuota(0)]), claude: account([
    windowQuota(95), windowQuota(8, null, {id:'custom',label:'未知周期'}),
  ]) };
  const codex = selected(providers, {...DEFAULT_SETTINGS,visible:{codex:false,claude:true}});
  assert.equal(codex.remainingPercent,0);
  assert.equal(codex.status,'ready');
  assert.equal(codex.controllingPeriod,'short');
  assert.deepEqual(codex.memberIds,['codex']);
  const claude = selected(providers,{...DEFAULT_SETTINGS,energyTarget:'claude',visible:{codex:true,claude:false}});
  assert.equal(claude.remainingPercent,95);
  assert.equal(claude.controllingPeriod,'short');
  assert.equal(claude.windows[1].id,'custom');
  assert.equal(claude.windows[1].windowMinutes,null);
  assert.equal(selectFreshRemaining(providers,{},NOW),0);
});

test('aggregate averages each period equally and five-hour average controls even when weekly is lower', () => {
  const result = selected({
    codex:account([windowQuota(10),windowQuota(20,10080)]),
    claude:account([windowQuota(90),windowQuota(10,10080)]),
  });
  assert.equal(result.remainingPercent,50);
  assert.equal(result.status,'ready');
  assert.equal(result.controllingPeriod,'short');
  assert.deepEqual(result.windows.map(window=>[window.id,window.remainingPercent,window.memberCount,window.totalCount]),
    [['short',50,2,2],['weekly',15,2,2]]);
  assert.deepEqual(result.missingIds,[]);
});

test('each provider has equal weight and model-specific quotas never replace total weekly quota', () => {
  const result = selected({
    codex:account([windowQuota(20),windowQuota(60,300,{id:'model-a'}),windowQuota(90,10080)]),
    claude:account([windowQuota(80),windowQuota(70,10080),windowQuota(10,10080,{id:'seven_day_sonnet'})]),
  });
  assert.equal(result.remainingPercent,50);
  assert.deepEqual(result.windows.map(window=>window.remainingPercent),[50,80]);
  assert.equal(result.windows[1].label,'总周');
  assert.ok(result.windows.every(window=>window.memberCount===2));
});

test('single-account energy follows account-wide quotas while models remain untouched in provider data', () => {
  const provider=account([windowQuota(90),windowQuota(80,10080),
    windowQuota(0,10080,{id:'seven_day_sonnet',label:'每周 · Sonnet'}),
    windowQuota(0,300,{id:'gpt-5.4',label:'GPT-5.4'}),
    windowQuota(0,60,{id:'opus',label:'Opus 专属'}),
  ]);
  const before=structuredClone(provider);
  const result=selected({codex:provider},DEFAULT_SETTINGS);
  assert.equal(result.remainingPercent,90);
  assert.equal(result.controllingPeriod,'short');
  assert.deepEqual(result.windows.map(window=>window.label),['5 小时','总周']);
  assert.deepEqual(provider,before);
  assert.equal(selected({codex:account([windowQuota(0,10080,{id:'seven_day_sonnet'})])},DEFAULT_SETTINGS).remainingPercent,null);
});

test('model-only weekly data does not fill total weekly quota or block complete five-hour data', () => {
  const result=selected({codex:account([windowQuota(100),windowQuota(80,10080)]),
    claude:account([windowQuota(100),windowQuota(60,10080,{id:'seven_day_sonnet'})])});
  assert.equal(result.status,'ready');
  assert.equal(result.remainingPercent,100);
  assert.equal(result.controllingPeriod,'short');
  assert.deepEqual(result.missingIds,[]);
  assert.deepEqual(result.windows.map(window=>[window.id,window.remainingPercent,window.memberCount]),
    [['short',100,2],['weekly',null,1]]);
});

test('invalid or unsupported model-specific windows do not invalidate valid account totals', () => {
  const result=selected({codex:account([windowQuota(50),windowQuota(60,10080)]),
    claude:account([windowQuota(50),windowQuota(60,10080),
      windowQuota(null,10080,{id:'seven_day_sonnet'}),windowQuota(0,60,{id:'haiku'})])});
  assert.equal(result.status,'ready');
  assert.equal(result.remainingPercent,50);
  assert.deepEqual(result.windows.map(window=>window.remainingPercent),[50,60]);
});

test('real zero participates in an aggregate and can make it exhausted', () => {
  assert.equal(selected({codex:account([windowQuota(0)]),claude:account([windowQuota(100)])}).remainingPercent,50);
  assert.equal(selected({codex:account([windowQuota(0)]),claude:account([windowQuota(0)])}).remainingPercent,0);
});

test('a single shared period is sufficient and visibility never selects aggregate members', () => {
  const providers={codex:account([windowQuota(20,10080)]),claude:account([windowQuota(80,10080)])};
  const result=selected(providers,{...aggregate,visible:{codex:false,claude:false}});
  assert.equal(result.remainingPercent,50);
  assert.equal(result.controllingPeriod,'weekly');
  assert.deepEqual(result.windows.map(window=>window.id),['weekly']);
  assert.deepEqual(result.memberIds,['codex','claude']);
});

test('missing, signed-out, stale and future-dated members make the whole aggregate incomplete', () => {
  for(const claude of [undefined,account([]),account([windowQuota(100)],{status:'signed_out'}),
    account([windowQuota(100)],{status:'quota_pending'}),
    account([windowQuota(100)],{updatedAt:new Date(NOW-180001).toISOString()}),
    account([windowQuota(100)],{updatedAt:new Date(NOW+1).toISOString()}),
  ]) {
    const result=selected({codex:account([windowQuota(100)]),claude});
    assert.equal(result.status,'incomplete');
    assert.equal(result.remainingPercent,null);
    assert.deepEqual(result.missingIds,['claude']);
    assert.equal(result.windows[0].remainingPercent,null);
    assert.equal(result.windows[0].memberCount,1);
    assert.equal(result.windows[0].totalCount,2);
  }
});

test('connecting and error caches remain valid up to exactly 180 seconds', () => {
  const result=selected({
    codex:account([windowQuota(40)],{status:'connecting',updatedAt:new Date(NOW-180000).toISOString()}),
    claude:account([windowQuota(80,300,{resetsAt:null})],{status:'error'}),
  });
  assert.equal(result.status,'ready');
  assert.equal(result.remainingPercent,60);
});

test('invalid or elapsed weekly readings stay unknown without blocking complete five-hour data', () => {
  for(const invalid of [windowQuota(0,10080,{resetsAt:new Date(NOW).toISOString()}),
    windowQuota(100,10080,{resetsAt:'invalid'}),windowQuota(101,10080),
    windowQuota(NaN,10080),windowQuota(null,10080),windowQuota('100',10080),
  ]) {
    const result=selected({codex:account([windowQuota(100),invalid]),
      claude:account([windowQuota(100),windowQuota(100,10080)])});
    assert.equal(result.status,'ready');
    assert.equal(result.remainingPercent,100);
    assert.equal(result.controllingPeriod,'short');
    assert.deepEqual(result.missingIds,[]);
    assert.deepEqual(result.windows.map(window=>window.remainingPercent),[100,null]);
    assert.equal(result.windows[1].memberCount,1);
  }
});

test('a valid model-specific window cannot replace an invalid general window in the same period', () => {
  for(const invalid of [windowQuota(0,10080,{resetsAt:new Date(NOW).toISOString()}),
    windowQuota(null,10080),windowQuota(101,10080)]) {
    const result=selected({codex:account([windowQuota(100,10080)]),
      claude:account([invalid,windowQuota(100,10080,{id:'seven_day_sonnet'})])});
    assert.equal(result.remainingPercent,null);
    assert.equal(result.status,'incomplete');
    assert.equal(result.controllingPeriod,'weekly');
    assert.deepEqual(result.missingIds,['claude']);
    assert.deepEqual(result.windows.map(window=>[window.remainingPercent,window.memberCount]),[[null,1]]);
  }
});

test('a declared five-hour period requires every aggregate member and never falls back to weekly', () => {
  const result=selected({codex:account([windowQuota(100)]),claude:account([windowQuota(100,10080)])});
  assert.equal(result.remainingPercent,null);
  assert.equal(result.controllingPeriod,'short');
  assert.deepEqual(result.missingIds,['claude']);
  assert.deepEqual(result.windows.map(window=>[window.id,window.remainingPercent,window.memberCount]),
    [['short',null,1],['weekly',null,1]]);
});

test('unknown-only multi-account periods cannot be combined without a matching duration', () => {
  const result=selected({codex:account([windowQuota(100,null)]),claude:account([windowQuota(100,60)])});
  assert.equal(result.status,'incomplete');
  assert.equal(result.remainingPercent,null);
  assert.deepEqual(result.windows,[]);
  assert.deepEqual(result.missingIds,['codex','claude']);
  const mixed=selected({codex:account([windowQuota(100)]),claude:account([windowQuota(100,null)])});
  assert.deepEqual(mixed.missingIds,['claude']);
  assert.equal(mixed.remainingPercent,null);
});

test('unsupported non-controlling periods do not block complete five-hour aggregate data', () => {
  for(const duration of [null,60,1440,'300']) {
    const result=selected({codex:account([windowQuota(100),windowQuota(100,10080)]),
      claude:account([windowQuota(100),windowQuota(100,10080),windowQuota(0,duration,{id:'custom'})])});
    assert.equal(result.status,'ready');
    assert.equal(result.remainingPercent,100);
    assert.equal(result.controllingPeriod,'short');
    assert.equal(result.reason,undefined);
    assert.deepEqual(result.missingIds,[]);
    assert.deepEqual(result.windows.map(window=>window.remainingPercent),[100,100]);
  }
});

test('a one-member aggregate exactly matches that account including custom periods', () => {
  const providers={claude:account([windowQuota(90),windowQuota(0,60,{id:'custom',label:'自定义'})])};
  const single=selected(providers,{...DEFAULT_SETTINGS,energyTarget:'claude'});
  const combined=selected(providers,{...aggregate,aggregateMembers:['claude']});
  assert.deepEqual(combined,{...single,mode:'aggregate'});
  assert.equal(combined.remainingPercent,90);
  assert.equal(combined.controllingPeriod,'short');
});

test('declared expired or malformed five-hour data never falls back to a full weekly reading', () => {
  for(const invalid of [windowQuota(0,300,{resetsAt:new Date(NOW).toISOString()}),
    windowQuota(100,300,{resetsAt:'invalid'}),windowQuota(101),windowQuota(NaN),
    windowQuota(null),windowQuota('100'),windowQuota(undefined)]) {
    const provider=account([invalid,windowQuota(100,10080)]);
    const single=selected({codex:provider},DEFAULT_SETTINGS);
    assert.equal(single.status,'incomplete');
    assert.equal(single.remainingPercent,null);
    assert.equal(single.controllingPeriod,'short');
    assert.deepEqual(single.missingIds,['codex']);
    assert.deepEqual(single.windows.map(window=>[window.remainingPercent,window.memberCount]),[[null,0],[100,1]]);
    const combined=selected({codex:provider,claude:account([windowQuota(100),windowQuota(100,10080)])});
    assert.equal(combined.status,'incomplete');
    assert.equal(combined.remainingPercent,null);
    assert.equal(combined.controllingPeriod,'short');
    assert.deepEqual(combined.missingIds,['codex']);
    assert.deepEqual(combined.windows.map(window=>[window.remainingPercent,window.memberCount]),[[null,1],[100,2]]);
  }
});

test('single-account weekly zero never overrides valid five-hour data and real five-hour zero still controls', () => {
  const normal=selected({codex:account([windowQuota(85),windowQuota(0,10080)])},DEFAULT_SETTINGS);
  assert.equal(normal.remainingPercent,85);
  assert.equal(normal.controllingPeriod,'short');
  assert.deepEqual(normal.windows.map(window=>window.remainingPercent),[85,0]);
  const exhausted=selected({codex:account([windowQuota(0),windowQuota(100,10080)])},DEFAULT_SETTINGS);
  assert.equal(exhausted.remainingPercent,0);
  assert.equal(exhausted.status,'ready');
});

test('weekly controls only when all account-wide five-hour declarations are absent', () => {
  const providers={codex:account([windowQuota(80,10080),windowQuota(0,300,{id:'spark'})]),
    claude:account([windowQuota(40,10080),windowQuota(0,60,{id:'custom'})])};
  const combined=selected(providers);
  assert.equal(combined.remainingPercent,60);
  assert.equal(combined.controllingPeriod,'weekly');
  assert.deepEqual(combined.windows.map(window=>[window.id,window.remainingPercent]),[['weekly',60]]);
  const single=selected(providers,DEFAULT_SETTINGS);
  assert.equal(single.remainingPercent,80);
  assert.equal(single.controllingPeriod,'weekly');
});

test('single-account custom-only data keeps its minimum while unmatched multi-account custom data stays unknown', () => {
  const providers={codex:account([windowQuota(75,60,{id:'custom-1'}),windowQuota(7,null,{id:'custom-2'})])};
  const single=selected(providers,DEFAULT_SETTINGS);
  assert.equal(single.remainingPercent,7);
  assert.equal(single.controllingPeriod,null);
  assert.equal(single.status,'ready');
  const combined=selected({...providers,claude:account([windowQuota(60,60)])});
  assert.equal(combined.remainingPercent,null);
  assert.equal(combined.controllingPeriod,null);
  assert.equal(combined.status,'incomplete');
});

test('empty or malformed quota data never creates an available target', () => {
  for(const providers of [undefined,{}, {codex:account('not-windows'),claude:account([])},
    {codex:account([null,{},windowQuota(Infinity)]),claude:account([])}]) {
    assert.equal(selected(providers).remainingPercent,null);
    assert.equal(selected(providers).status,'incomplete');
  }
  assert.equal(selected({}, {...aggregate,aggregateMembers:['codex','invalid']}).status,'incomplete');
});

test('energy target settings validate nonempty distinct account IDs and canonicalize order', () => {
  for(const energyTarget of ['all','Codex','',null,1]) assert.throws(()=>validatePatch({energyTarget}));
  for(const aggregateMembers of [[],null,'codex',['codex','codex'],['other'],['codex',null],Array(1)]) {
    assert.throws(()=>validatePatch({aggregateMembers}));
  }
  assert.deepEqual(validatePatch({energyTarget:'aggregate',aggregateMembers:['claude','codex']}),
    {energyTarget:'aggregate',aggregateMembers:['codex','claude']});
  assert.deepEqual(validatePatch({aggregateMembers:['claude']}),{aggregateMembers:['claude']});
});

test('pre-target preferences select the sole visible account once, otherwise Codex', () => {
  for(const [visible,expected] of [[{codex:false,claude:true},'claude'],
    [{codex:true,claude:false},'codex'],[{codex:true,claude:true},'codex'],[{codex:false,claude:false},'codex']]) {
    const settings=readSafeSettings({visible});
    assert.equal(settings.energyTarget,expected);
    assert.deepEqual(settings.aggregateMembers,['codex','claude']);
  }
  assert.equal(readSafeSettings({energyTarget:'codex',visible:{codex:false,claude:true}}).energyTarget,'codex');
});

test('target migration is persisted and subsequent visibility changes never retarget it', () => {
  const fixture=fs.mkdtempSync(path.join(__dirname,'.energy-target-'));
  try {
    fs.writeFileSync(path.join(fixture,'settings.json'),JSON.stringify({visible:{codex:false,claude:true},refreshSeconds:90}));
    const store=new SettingsStore({dataDir:fixture});
    assert.equal(store.load().energyTarget,'claude');
    assert.equal(JSON.parse(fs.readFileSync(store.settingsPath,'utf8')).energyTarget,'claude');
    store.update({visible:{codex:true,claude:false}});
    assert.equal(store.get().energyTarget,'claude');
    store.update({energyTarget:'aggregate',aggregateMembers:['claude']});
    const reloaded=new SettingsStore({dataDir:fixture}).load();
    assert.equal(reloaded.energyTarget,'aggregate');
    assert.deepEqual(reloaded.aggregateMembers,['claude']);
    assert.deepEqual(reloaded.visible,{codex:true,claude:false});
    assert.equal(reloaded.refreshSeconds,90);
  } finally {
    assert.equal(path.dirname(path.resolve(fixture)),path.resolve(__dirname));
    assert.ok(path.basename(fixture).startsWith('.energy-target-'));
    fs.rmSync(fixture,{recursive:true,force:true});
  }
});

test('a migration write failure preserves valid loaded preferences in memory', () => {
  const fixture=fs.mkdtempSync(path.join(__dirname,'.energy-target-'));
  try {
    const saved={visible:{codex:false,claude:true},refreshSeconds:120,warningThreshold:35,alwaysOnTop:false};
    const settingsPath=path.join(fixture,'settings.json');
    fs.writeFileSync(settingsPath,JSON.stringify(saved));
    const store=new SettingsStore({dataDir:fixture});
    store.write=()=>{throw Object.assign(new Error('File is temporarily locked'),{code:'EACCES'});};
    const loaded=store.load();
    assert.equal(loaded.energyTarget,'claude');
    assert.equal(loaded.refreshSeconds,120);
    assert.equal(loaded.warningThreshold,35);
    assert.equal(loaded.alwaysOnTop,false);
    assert.deepEqual(loaded.visible,saved.visible);
    assert.deepEqual(JSON.parse(fs.readFileSync(settingsPath,'utf8')),saved);
  } finally {
    assert.equal(path.dirname(path.resolve(fixture)),path.resolve(__dirname));
    assert.ok(path.basename(fixture).startsWith('.energy-target-'));
    fs.rmSync(fixture,{recursive:true,force:true});
  }
});
