'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { SettingsStore, DEFAULT_SETTINGS, validatePatch, readSafeSettings } = require('../backend/settings-store.cjs');
const { selectFreshRemaining } = require('../backend/energy-selection.cjs');
const { WidgetPosition, validPosition } = require('../backend/widget-position.cjs');
const { widgetDimensions, validateShapeRects, scaleShapeRects } = require('../backend/window-shape.cjs');

const now = Date.UTC(2026, 9, 16, 0);
function account(remainingPercent, extras = {}) {
  return { status: 'connected', updatedAt: new Date(now - 1000).toISOString(),
    windows: [{ remainingPercent, resetsAt: new Date(now + 100000).toISOString() }], ...extras };
}

test('energy follows the chosen account independently of visible providers', () => {
  const providers = {codex: account(85), claude: account(9)};
  assert.equal(selectFreshRemaining(providers, DEFAULT_SETTINGS, now), 85);
  assert.equal(selectFreshRemaining(providers, {...DEFAULT_SETTINGS,visible:{codex:true,claude:false}}, now), 85);
  assert.equal(selectFreshRemaining(providers, {...DEFAULT_SETTINGS,visible:{codex:false,claude:false}}, now), 85);
  assert.equal(selectFreshRemaining(providers, {...DEFAULT_SETTINGS,energyTarget:'claude'}, now), 9);
});

test('signed out, pending, stale, invalid, and elapsed quotas stay unknown', () => {
  for (const entry of [
    account(100, {status:'quota_pending'}), account(100, {status:'signed_out'}),
    account(100, {updatedAt:new Date(now - 180001).toISOString()}),
    account(100, {updatedAt:null}), account(101), account(NaN),
    account(100, {windows:[{remainingPercent:100,resetsAt:new Date(now).toISOString()}]}),
    account(100, {windows:[{remainingPercent:100,resetsAt:'not-a-time'}]}),
  ]) assert.equal(selectFreshRemaining({codex:entry}, DEFAULT_SETTINGS, now), null);
  assert.equal(selectFreshRemaining({codex:account(0)}, DEFAULT_SETTINGS, now), 0);
});

test('settings reject injected fields and malformed values', () => {
  for (const patch of [{executable:'bad'}, {visible:{other:true}}, {paused:'false'},
    {refreshSeconds:1}, {warningThreshold:Infinity}, {floatingScale:101},
    {floatingScale:'125'}, {followSystemMotion:'false'}, {startupAnimation:false}, {visible:null}, []]) {
    assert.throws(()=>validatePatch(patch));
  }
  assert.deepEqual(validatePatch({visible:{codex:false}}), {visible:{codex:false}});
  assert.deepEqual(validatePatch({followSystemMotion:false}),{followSystemMotion:false});
  for (const scale of [80,100,125,150]) assert.deepEqual(validatePatch({floatingScale:scale}),{floatingScale:scale});
});

test('quota display accepts exactly one of bars and rings', () => {
  for (const quotaDisplay of ['bars','rings']) {
    assert.deepEqual(validatePatch({quotaDisplay}),{quotaDisplay});
  }
  for (const quotaDisplay of [null,undefined,true,0,'bar','ring','BARS','both','',['bars'],{mode:'rings'}]) {
    assert.throws(()=>validatePatch({quotaDisplay}));
  }
  assert.equal(readSafeSettings({quotaDisplay:'both'}).quotaDisplay,'bars');
});

test('a new profile defaults to a pinned compact widget while saved preferences remain authoritative', () => {
  const fresh = readSafeSettings({});
  assert.equal(fresh.compact,true);
  assert.equal(fresh.alwaysOnTop,true);
  assert.equal(fresh.floatingScale,100);
  assert.equal(fresh.followSystemMotion,true);
  assert.equal(fresh.quotaDisplay,'bars');
  assert.equal(Object.hasOwn(fresh,'startupAnimation'),false);
  const saved = readSafeSettings({compact:false,alwaysOnTop:false});
  assert.equal(saved.compact,false);
  assert.equal(saved.alwaysOnTop,false);
});

test('floating dimensions preserve the full 420 by 246 artwork at all four scales', () => {
  assert.deepEqual(widgetDimensions(80), {width:336,height:197});
  assert.deepEqual(widgetDimensions(100), {width:420,height:246});
  assert.deepEqual(widgetDimensions(125), {width:525,height:308});
  assert.deepEqual(widgetDimensions(150), {width:630,height:369});
  assert.throws(()=>widgetDimensions(101));
});

test('native shape accepts bounded fractional rectangles and rejects malformed or excessive input', () => {
  const rect={x:1.25,y:2.5,width:100.5,height:200.25};
  const valid=validateShapeRects([rect]);
  assert.deepEqual(valid,[rect]);
  assert.notEqual(valid[0],rect);
  assert.deepEqual(validateShapeRects([]),[]);
  assert.equal(validateShapeRects(Array(1000).fill(rect)).length,1000);
  for(const value of [null,{},Array(1001).fill(rect),[null],[[0,0,1,1]],
    [{...rect,x:-1}],[{...rect,width:0}],[{...rect,height:-1}],
    [{...rect,x:NaN}],[{...rect,y:Infinity}],[{...rect,width:'10'}],
    [{x:419,y:0,width:2,height:1}],[{x:0,y:245,width:1,height:2}],
    [{...rect,extra:true}],[{x:0,y:0,width:1}],
  ]) assert.throws(()=>validateShapeRects(value));
});

test('native shape rounding includes every original edge after zoom changes', () => {
  const rects=validateShapeRects([{x:1.25,y:2.5,width:100.5,height:200.25},
    {x:243,y:34,width:174,height:205},{x:190,y:81,width:72,height:48}]);
  for(const scale of [80,100,125,150]) {
    const dimensions=widgetDimensions(scale);
    const scaled=scaleShapeRects(rects,scale);
    for(let i=0;i<rects.length;i++) {
      const source=rects[i],result=scaled[i],factor=scale/100;
      assert.ok(Object.values(result).every(Number.isInteger));
      assert.ok(result.x<=source.x*factor && result.y<=source.y*factor);
      assert.ok(result.x+result.width>=(source.x+source.width)*factor);
      assert.ok(result.y+result.height>=(source.y+source.height)*factor);
      assert.ok(result.x+result.width<=dimensions.width);
      assert.ok(result.y+result.height<=dimensions.height);
    }
  }
});

test('widget position persists only valid coordinates without changing settings', () => {
  const fixtureRoot=path.resolve(__dirname,'.position-fixture');
  const position=new WidgetPosition(fixtureRoot);
  try {
    assert.equal(position.read(),null);
    position.save({x:-500,y:120,token:'ignored'});
    assert.deepEqual(position.read(),{x:-500,y:120});
    assert.equal(fs.readFileSync(position.file,'utf8').includes('token'),false);
    position.save({x:Infinity,y:50});
    assert.deepEqual(position.read(),{x:-500,y:120});
    assert.equal(validPosition({x:'4',y:5}),false);
    assert.equal(fs.existsSync(path.join(fixtureRoot,'settings.json')),false);
  } finally {
    assert.equal(path.dirname(fixtureRoot),path.resolve(__dirname));
    fs.rmSync(fixtureRoot,{recursive:true,force:true});
  }
});

test('settings migrate only three safe fields and persist partial visibility', () => {
  const fixtureRoot = path.resolve(__dirname, '.host-fixture');
  fs.mkdirSync(fixtureRoot,{recursive:true});
  const legacy = path.join(fixtureRoot,'legacy.json');
  fs.writeFileSync(legacy,JSON.stringify({refreshSeconds:120,warningThreshold:35,alwaysOnTop:true,
    launchWithCodex:true,quotaDisplay:'rings',authToken:'must-never-migrate'}));
  const store = new SettingsStore({dataDir:path.join(fixtureRoot,'v2'),legacyPath:legacy});
  try {
    const initial = store.load();
    assert.equal(initial.refreshSeconds,120);
    assert.equal(initial.warningThreshold,35);
    assert.equal(initial.alwaysOnTop,true);
    assert.equal(initial.launchWithCodex,false);
    assert.equal(initial.quotaDisplay,'bars');
    assert.equal(initial.authToken,undefined);
    store.update({visible:{codex:false},compact:true,followSystemMotion:false,quotaDisplay:'rings'});
    const stored = new SettingsStore({dataDir:path.join(fixtureRoot,'v2')}).load();
    assert.deepEqual(stored.visible,{codex:false,claude:true});
    assert.equal(stored.compact,true);
    assert.equal(stored.followSystemMotion,false);
    assert.equal(stored.quotaDisplay,'rings');
    assert.equal(fs.readFileSync(store.settingsPath,'utf8').includes('must-never-migrate'),false);
  } finally {
    assert.equal(path.dirname(fixtureRoot),path.resolve(__dirname));
    fs.rmSync(fixtureRoot,{recursive:true,force:true});
  }
});

test('pre-display profiles persist bars and discard the removed introduction preference', () => {
  const fixtureRoot=fs.mkdtempSync(path.resolve(__dirname,'.display-fixture-'));
  const settingsPath=path.join(fixtureRoot,'settings.json');
  try {
    fs.writeFileSync(settingsPath,JSON.stringify({energyTarget:'claude',aggregateMembers:['claude'],
      visible:{codex:false,claude:true},startupAnimation:true,warningThreshold:35}));
    const store=new SettingsStore({dataDir:fixtureRoot});
    const loaded=store.load();
    assert.equal(loaded.quotaDisplay,'bars');
    assert.equal(loaded.energyTarget,'claude');
    assert.equal(loaded.warningThreshold,35);
    assert.equal(Object.hasOwn(loaded,'startupAnimation'),false);
    const saved=JSON.parse(fs.readFileSync(settingsPath,'utf8'));
    assert.equal(saved.quotaDisplay,'bars');
    assert.equal(Object.hasOwn(saved,'startupAnimation'),false);
    store.update({quotaDisplay:'rings'});
    const reopened=new SettingsStore({dataDir:fixtureRoot});
    assert.equal(reopened.load().quotaDisplay,'rings');
    assert.throws(()=>reopened.update({quotaDisplay:'both'}));
    assert.equal(new SettingsStore({dataDir:fixtureRoot}).load().quotaDisplay,'rings');
    reopened.update({quotaDisplay:'bars'});
    assert.equal(new SettingsStore({dataDir:fixtureRoot}).load().quotaDisplay,'bars');
  } finally {
    assert.equal(path.dirname(fixtureRoot),path.resolve(__dirname));
    fs.rmSync(fixtureRoot,{recursive:true,force:true});
  }
});
