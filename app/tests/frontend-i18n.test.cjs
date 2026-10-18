'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');

const uiDirectory = path.resolve(__dirname, '../ui');
const temporaryDirectories = [];
const NOW = Date.parse('2026-10-17T02:30:00.000Z');
let frontendPromise;

// Load the actual browser modules in independent ESM scopes. The only stub is
// the synchronous preload locale; account rendering and formatters stay real.
async function frontends() {
  if (!frontendPromise) frontendPromise = (async()=>{
    const result = {};
    const originalWindow = globalThis.window;
    try {
      for (const locale of ['zh-CN','en']) {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'tiga-frontend-i18n-'));
        temporaryDirectories.push(directory);
        fs.writeFileSync(path.join(directory, 'package.json'), '{"type":"module"}');
        for (const filename of ['i18n.js','shared.js','energy-gauge.js','quota-materials.js']) {
          fs.copyFileSync(path.join(uiDirectory, filename), path.join(directory, filename));
        }
        globalThis.window = {monitor:{locale}};
        const i18n = await import(pathToFileURL(path.join(directory, 'i18n.js')));
        const shared = await import(pathToFileURL(path.join(directory, 'shared.js')));
        result[locale] = {...i18n, ...shared};
      }
    } finally {
      if (originalWindow === undefined) delete globalThis.window;
      else globalThis.window = originalWindow;
    }
    return result;
  })();
  return frontendPromise;
}

test.after(()=>{
  for (const directory of temporaryDirectories) {
    const resolved = path.resolve(directory);
    assert.equal(path.dirname(resolved), path.resolve(os.tmpdir()));
    assert(path.basename(resolved).startsWith('tiga-frontend-i18n-'));
    fs.rmSync(resolved, {recursive:true, force:true});
  }
});

function freezeNow(context) {
  const previous = Date.now;
  Date.now = ()=>NOW;
  context.after(()=>{ Date.now = previous; });
}

function provider(frontend, overrides={}) {
  return {...frontend.signedOut('codex'), status:'connected',
    updatedAt:new Date(NOW - 60_000).toISOString(),
    windows:[{id:'primary', label:'5 小时', windowMinutes:300,
      remainingPercent:25, resetsAt:new Date(NOW + 3_600_000).toISOString()},
    {id:'secondary', label:'每周', windowMinutes:10080,
      remainingPercent:70, resetsAt:new Date(NOW + 86_400_000).toISOString()}],
    ...overrides};
}

test('each release uses its explicit language and matching local date format', async()=>{
  const modules = await frontends();
  for (const [locale, dateLocale] of [['zh-CN','zh-CN'],['en','en-US']]) {
    const frontend = modules[locale];
    assert.equal(frontend.locale, locale);
    assert.equal(frontend.dateLocale, dateLocale);
    assert.equal(frontend.formatDate(NOW), new Date(NOW).toLocaleString(dateLocale));
    assert.equal(frontend.t('External service message'), 'External service message');
    assert.equal(frontend.t('constructor'), 'constructor');
  }
  assert.equal(modules['zh-CN'].t('使用 ChatGPT 账号登录'), '使用 ChatGPT 账号登录');
  assert.equal(modules.en.t('使用 ChatGPT 账号登录'), 'Sign in with ChatGPT');
});

test('refresh age formats seconds, minutes, hours and days without accepting future times', async()=>{
  const modules = await frontends();
  const cases = [
    [1_000,'刚刚刷新','Just refreshed'],
    [30_000,'30秒前刷新','Updated 30s ago'],
    [120_000,'2分钟前刷新','Updated 2m ago'],
    [7_200_000,'2小时前刷新','Updated 2h ago'],
    [172_800_000,'2天前刷新','Updated 2d ago'],
  ];
  for (const [elapsed, chinese, english] of cases) {
    assert.equal(modules['zh-CN'].refreshAge(NOW - elapsed, NOW), chinese);
    assert.equal(modules.en.refreshAge(new Date(NOW - elapsed).toISOString(), NOW), english);
  }
  assert.equal(modules.en.refreshAge(NOW + 1_000, NOW), 'Invalid refresh time');
  assert.equal(modules.en.refreshAge('invalid', NOW), 'Not refreshed yet');
  assert.equal(modules['zh-CN'].refreshAge(null, NOW), '尚未刷新');
});

test('reset countdowns keep their real deadline, expired state and official fallback text', async context=>{
  freezeNow(context);
  const modules = await frontends();
  const cases = [
    [60_000,'1 分钟后重置','Resets in 1m'],
    [3_660_000,'1 小时 1 分钟后重置','Resets in 1h 1m'],
    [90_000_000,'1 天 1 小时后重置','Resets in 1d 1h'],
    [-1,'重置时间已到，等待重新同步','Reset time reached; waiting for a refresh'],
  ];
  for (const [offset, chinese, english] of cases) {
    const window = {resetsAt:new Date(NOW + offset).toISOString()};
    assert.equal(modules['zh-CN'].resetDescription(window), chinese);
    assert.equal(modules.en.resetDescription(window), english);
  }
  assert.equal(modules.en.resetDescription({resetsAt:'invalid'}), 'Reset time unavailable');
  assert.equal(modules.en.resetDescription({resetLabel:'Resets at 4pm (UTC)'}), 'Resets at 4pm (UTC)');
});

test('the floating widget shows separate reset countdowns without needing hover', async context=>{
  freezeNow(context);
  const modules=await frontends();
  for(const locale of ['zh-CN','en']) for(const mode of ['bars','rings']) {
    const frontend=modules[locale], value=provider(frontend);
    value.windows[0].resetsAt=new Date(NOW+9_000_000).toISOString();
    value.windows[1].resetsAt=new Date(NOW+525_600_000).toISOString();
    const settings={...frontend.defaults,quotaDisplay:mode};
    const rendered=frontend.providerMarkup(value,settings,true);
    const visible=[...rendered.matchAll(/<p class="quota-reset"[^>]*>(.*?)<\/p>/g)].map(match=>match[1]);
    assert.deepEqual(visible,locale==='en'?['Reset 6d 2h','Reset 2h 30m']:['重置 6天2小时','重置 2小时30分']);
    assert(rendered.includes(locale==='en'?'Just refreshed':'刚刚刷新') || rendered.includes(locale==='en'?'Updated 1m ago':'1分钟前刷新'));
    const before=JSON.stringify(value);
    Date.now=()=>NOW+60_000;
    const elapsed=frontend.providerMarkup(value,settings,true);
    assert(elapsed.includes(locale==='en'?'>Reset 2h 29m</p>':'>重置 2小时29分</p>'));
    assert.equal(JSON.stringify(value),before,'A local countdown must not modify quota or the last successful refresh');
    Date.now=()=>NOW;
  }
});

test('compact reset countdowns distinguish elapsed and unknown deadlines', async context=>{
  freezeNow(context);
  const modules=await frontends();
  for(const locale of ['zh-CN','en']) {
    const frontend=modules[locale];
    for(const resetsAt of [null,'invalid']) assert.equal(frontend.resetDescription({resetsAt},true),locale==='en'?'Reset unknown':'重置时间未知');
    assert.equal(frontend.resetDescription({resetsAt:new Date(NOW).toISOString()},true),locale==='en'?'Reset pending':'重置待同步');
    assert.equal(frontend.resetDescription({resetsAt:new Date(NOW+1000).toISOString()},true),locale==='en'?'Reset 1m':'重置 1分');
  }
});

test('English aggregate counts use singular and plural while Chinese templates remain intact', async()=>{
  const modules = await frontends();
  const template = '{count} / {total} 个账号已同步';
  assert.equal(modules.en.t(template,{count:1,total:1}), '1 / 1 account synced');
  assert.equal(modules.en.t(template,{count:1,total:2}), '1 / 2 accounts synced');
  assert.equal(modules['zh-CN'].t(template,{count:1,total:2}), '1 / 2 个账号已同步');
});

test('bars and rings localize period labels, reset tooltips and accessible meters', async context=>{
  freezeNow(context);
  const modules = await frontends();
  for (const locale of ['zh-CN','en']) for (const mode of ['bars','rings']) {
    const frontend = modules[locale];
    const value = provider(frontend);
    value.windows.push({id:'seven_day_sonnet', label:'每周 · Sonnet', windowMinutes:10080,
      remainingPercent:42, resetsAt:new Date(NOW + 86_400_000).toISOString()});
    const compact = frontend.providerMarkup(value,{...frontend.defaults,quotaDisplay:mode},true);
    assert.match(compact, /aria-valuenow="25"/);
    assert.match(compact, /aria-valuenow="70"/);
    assert(!compact.includes('Sonnet'), 'Model-specific quota must not become a total period');
    if(locale==='en') {
      assert(!/\p{Script=Han}/u.test(compact));
      assert(compact.includes('Weekly'));
      assert(compact.includes('5h'));
      assert(compact.includes('Resets in'));
      assert(compact.includes('remaining'));
    } else {
      assert(compact.includes('总周'));
      assert(compact.includes('5小时'));
      assert(compact.includes('后重置'));
    }
    const full = frontend.providerMarkup(value,frontend.defaults,false);
    assert(full.includes('Sonnet'), 'Settings retain the complete model quota');
  }
});

test('unknown, expired and signed-out quota never becomes a numeric English meter', async context=>{
  freezeNow(context);
  const {en} = await frontends();
  const expired = provider(en,{windows:[{id:'primary',label:'5 小时',windowMinutes:300,
    remainingPercent:25,resetsAt:new Date(NOW - 1).toISOString()}]});
  for(const mode of ['bars','rings']) {
    const markup = en.providerMarkup(expired,{...en.defaults,quotaDisplay:mode},true);
    assert(!markup.includes('aria-valuenow'));
    assert(markup.includes('Pending'));
    assert(!markup.includes('>25<'));
    const stale = en.providerMarkup(provider(en,{updatedAt:new Date(NOW - 181_000).toISOString()}),{...en.defaults,quotaDisplay:mode},true);
    assert(!stale.includes('aria-valuenow'));
    assert(stale.includes('Data expired'));
  }
  const signedOut = en.providerMarkup(provider(en,{status:'signed_out'}),en.defaults,true);
  assert(signedOut.includes('Sign in with ChatGPT'));
  assert(!signedOut.includes('role="meter"'));
  const missing = en.providerMarkup(provider(en,{windows:[]}),en.defaults,true);
  assert(missing.includes('Quota pending'));
  assert(!missing.includes('role="meter"'));
});

for(const locale of ['zh-CN','en']) for(const mode of ['bars','rings']) {
  for(const [age,ready] of [[240000,true],[300000,true],[420000,true],[420001,false]]) {
    test(`${locale} ${mode}: 300-second polling is ${ready?'fresh':'expired'} at ${age}ms`, async context=>{
      freezeNow(context);
      const frontend=(await frontends())[locale];
      const settings={...frontend.defaults,refreshSeconds:300,quotaDisplay:mode};
      for(const id of ['codex','claude']) {
        const value=provider(frontend,{id,updatedAt:new Date(NOW-age).toISOString()});
        const markup=frontend.providerMarkup(value,settings,true);
        const readings=[...markup.matchAll(/aria-valuenow="([^"]+)"/g)].map(match=>Number(match[1])).sort((a,b)=>a-b);
        assert.deepEqual(readings,ready?[25,70]:[],`${id} must retain or clear both real period meters`);
        assert.equal(markup.includes(frontend.t('数据已过期，等待最新额度')),!ready);
        assert.equal(markup.includes(frontend.t('待同步')),!ready);
        if(ready) {
          assert.match(markup,/data-quota-level="low"/);
          assert.match(markup,/>25<small>%<\/small>/);
          assert.match(markup,/>70<small>%<\/small>/);
        } else {
          assert(!markup.includes('>25<small>'));
          assert(!markup.includes('>70<small>'));
        }
        if(locale==='en') assert(!/\p{Script=Han}/u.test(markup));
      }
    });
  }
}

test('both languages and meter forms apply the legal polling interval bounds and default freshness minimum', async context=>{
  freezeNow(context);
  const modules=await frontends();
  for(const locale of ['zh-CN','en']) for(const mode of ['bars','rings']) {
    const frontend=modules[locale];
    for(const [refreshSeconds,maximumAge] of [[30,180000],[60,180000],[61,181000],[120,240000],[3600,3720000]]) {
      for(const [age,ready] of [[maximumAge,true],[maximumAge+1,false]]) {
        const markup=frontend.providerMarkup(provider(frontend,{updatedAt:new Date(NOW-age).toISOString()}),
          {...frontend.defaults,refreshSeconds,quotaDisplay:mode},true);
        assert.equal(markup.includes('aria-valuenow="25"'),ready,`${locale}/${mode} refresh=${refreshSeconds} age=${age}`);
      }
    }
    const {refreshSeconds:_omitted,...settings}=frontend.defaults;
    for(const [age,ready] of [[180000,true],[180001,false]]) {
      const markup=frontend.providerMarkup(provider(frontend,{updatedAt:new Date(NOW-age).toISOString()}),
        {...settings,quotaDisplay:mode},true);
      assert.equal(markup.includes('aria-valuenow="25"'),ready,`${locale}/${mode} missing setting at ${age}ms`);
    }
  }
});

test('invalid polling values retain the sixty-second default in both localized bars and rings', async context=>{
  freezeNow(context);
  const modules=await frontends();
  for(const locale of ['zh-CN','en']) for(const mode of ['bars','rings']) {
    const frontend=modules[locale];
    for(const refreshSeconds of [undefined,null,false,'300',NaN,Infinity,-Infinity,0,29,3601,300.5,{},[]]) {
      for(const [age,ready] of [[180000,true],[180001,false],[240000,false]]) {
        const markup=frontend.providerMarkup(provider(frontend,{updatedAt:new Date(NOW-age).toISOString()}),
          {...frontend.defaults,refreshSeconds,quotaDisplay:mode},true);
        assert.equal(markup.includes('aria-valuenow="25"'),ready,
          `${locale}/${mode} invalid refresh=${String(refreshSeconds)} age=${age}`);
      }
    }
  }
});

test('long polling never hides real zero early, extends an elapsed reset, or reveals signed-out cached meters', async context=>{
  freezeNow(context);
  const modules=await frontends();
  for(const locale of ['zh-CN','en']) for(const mode of ['bars','rings']) {
    const frontend=modules[locale];
    const settings={...frontend.defaults,refreshSeconds:300,quotaDisplay:mode};
    const value=provider(frontend,{updatedAt:new Date(NOW-420000).toISOString()});
    value.windows[0].remainingPercent=0;
    for(const status of ['connected','connecting','error']) {
      const markup=frontend.providerMarkup({...value,status},settings,true);
      assert(markup.includes('aria-valuenow="0"'),`${locale}/${mode}/${status}: real zero remains known`);
      assert(markup.includes('data-quota-level="stone"'));
    }
    for(const offset of [0,-1]) {
      const expired=provider(frontend,{updatedAt:new Date(NOW-240000).toISOString()});
      expired.windows[0].resetsAt=new Date(NOW+offset).toISOString();
      const markup=frontend.providerMarkup(expired,settings,true);
      assert(!markup.includes('aria-valuenow="25"'),`${locale}/${mode}: elapsed short reset is immediate`);
      assert(markup.includes('aria-valuenow="70"'),`${locale}/${mode}: unrelated weekly remains known`);
      assert(markup.includes(frontend.t('重置时间已到，等待重新同步')));
      assert(markup.includes(frontend.t('待同步')));
    }
    for(const id of ['codex','claude']) {
      const markup=frontend.providerMarkup({...value,id,status:'signed_out'},settings,true);
      assert(!markup.includes('role="meter"'));
      assert(!markup.includes('aria-valuenow'));
      assert(markup.includes(frontend.t(id==='codex'?'使用 ChatGPT 账号登录':'使用 Claude Code 账号登录')));
    }
  }
});

test('external account and plan text remains verbatim and HTML is escaped in both languages', async context=>{
  freezeNow(context);
  const modules = await frontends();
  for(const locale of ['zh-CN','en']) {
    const frontend = modules[locale];
    const account = '中文账号 <Admin> & "Team"';
    const plan = '套餐 <Pro> & Partners';
    const value = provider(frontend,{account,plan});
    value.windows[0].label = '5-hour <quota> "quoted"';
    const markup = frontend.providerMarkup(value,frontend.defaults,false);
    assert(markup.includes(frontend.escapeHtml(account)));
    assert(markup.includes(frontend.escapeHtml(plan)));
    assert(markup.includes(frontend.escapeHtml(value.windows[0].label)));
    assert(!markup.includes('<Admin>'));
    assert(!markup.includes('<Pro>'));
    assert(!markup.includes('<quota>'));
    assert(markup.includes('中文账号'), 'External account names must not be translated');
    assert(markup.includes('套餐'), 'External plan names must not be translated');
  }
});
