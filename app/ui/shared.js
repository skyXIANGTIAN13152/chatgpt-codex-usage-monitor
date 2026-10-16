import { quotaRingsSvg } from './energy-gauge.js';
import { t, formatDate } from './i18n.js';
export const $ = (selector) => document.querySelector(selector);
export const $$ = (selector) => [...document.querySelectorAll(selector)];
export const defaults = { visible:{codex:true,claude:true}, energyTarget:'codex', aggregateMembers:['codex','claude'], quotaDisplay:'bars', launchWithCodex:false, launchWithClaude:false, alwaysOnTop:true, floatingScale:100, reducedMotion:false, followSystemMotion:true, paused:false, warningThreshold:20, refreshSeconds:60 };
export const signedOut = (id) => ({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
export const emptySnapshot = () => ({providers:{codex:signedOut('codex'),claude:signedOut('claude')},settings:structuredClone(defaults),energy:{scene:'unknown'},previewScene:null});
export const bridge = window.monitor ?? { getSnapshot:async()=>emptySnapshot(), subscribe:()=>()=>{}, windowAction:async()=>{throw new Error(t('请在桌面程序中使用。'));} };
export const escapeHtml = (value) => String(value ?? '').replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export const statusLabel = (provider) => t(({signed_out:'未登录',connecting:'连接中',awaiting_login:'等待授权',connected:'已连接',quota_pending:'等待同步',error:'连接异常'})[provider.status] || '等待同步');
let toastTimer;
export function notify(message) { $('#toast').textContent=t(message); $('#toast').hidden=false; clearTimeout(toastTimer); toastTimer=setTimeout(()=>$('#toast').hidden=true,5200); }
export async function action(operation) { try { return await operation(); } catch(error) { notify(error?.message || t('暂时无法完成，请稍后重试。')); } }
export function mergeSnapshot(current,next) { return {...current,...next,providers:{...current.providers,...next.providers},settings:{...defaults,...current.settings,...next.settings,visible:{...current.settings.visible,...next.settings?.visible}}}; }
export function timestamp(value) { if(typeof value==='number') return value<1e12?value*1000:value; const parsed=Date.parse(value); return Number.isFinite(parsed)?parsed:null; }
export function refreshAge(value,now=Date.now()) {
  const updated=timestamp(value);
  if(!updated) return t('尚未刷新');
  const seconds=Math.floor((now-updated)/1000);
  if(seconds<0) return t('更新时间异常');
  if(seconds<10) return t('刚刚刷新');
  if(seconds<60) return t('{count}秒前刷新',{count:seconds});
  if(seconds<3600) return t('{count}分钟前刷新',{count:Math.floor(seconds/60)});
  if(seconds<86400) return t('{count}小时前刷新',{count:Math.floor(seconds/3600)});
  return t('{count}天前刷新',{count:Math.floor(seconds/86400)});
}
export function resetDescription(item) {
  const reset=timestamp(item.resetsAt);
  if(!reset) return t(item.resetLabel || '重置时间暂未提供');
  const diff=reset-Date.now();
  if(diff<=0) return t('重置时间已到，等待重新同步');
  const minutes=Math.ceil(diff/60000);
  const duration=minutes>=1440?t('{days} 天 {hours} 小时',{days:Math.floor(minutes/1440),hours:Math.floor(minutes%1440/60)}):minutes>=60?t('{hours} 小时 {minutes} 分钟',{hours:Math.floor(minutes/60),minutes:minutes%60}):t('{minutes} 分钟',{minutes});
  return t('{duration}后重置',{duration});
}
// Meter colors follow their own quota; the configurable threshold only controls Tiga.
export function quotaLevel(value) {
  return !Number.isFinite(value)||value<0||value>100?'unknown':value===0?'stone':value<50?'low':'full';
}
function shortLabel(item) {
  const model=/(?:^|[^a-z0-9])(sonnet|opus|haiku|spark|bengalfox)(?=$|[^a-z])|(?:^|[^a-z0-9])((?:gpt|claude)[-\d][\w.-]*)/i.exec(item.id+' '+item.label);
  if(model) return (model[1]||model[2]).replace(/^./,s=>s.toUpperCase());
  if(item.windowMinutes===300 || /5.?小时|five.hour|session/i.test(item.label+' '+item.id)) return '5小时';
  if(item.windowMinutes===10080 || /周|week/i.test(item.label+' '+item.id)) return '总周';
  return item.label || '额度';
}
export function providerMarkup(provider,settings,compact=false) {
  const id=provider.id, name=id==='codex'?'Codex':'Claude Code';
  const loginLabel=t(id==='codex'?'使用 ChatGPT 账号登录':'使用 Claude Code 账号登录');
  const busy=['connecting','awaiting_login'].includes(provider.status);
  const raw=(provider.windows||[]).filter(item=>item&&typeof item==='object');
  const valid=raw.filter(item=>quotaLevel(item.remainingPercent)!=='unknown');
  const updated=timestamp(provider.updatedAt), stale=!updated || updated>Date.now() || Date.now()-updated>180000;
  const hasQuota=['connected','connecting','error'].includes(provider.status)&&valid.length>0;
  const waiting=provider.status==='quota_pending'||(provider.status==='connected'&&!hasQuota);
  const weekly=raw.find(item=>shortLabel(item)==='总周');
  const short=raw.find(item=>shortLabel(item)==='5小时');
  const principal=[weekly,short].filter(Boolean);
  const headerWindows=principal.length?principal:valid;
  const headerReady=hasQuota&&!stale&&headerWindows.every(item=>{
    const reset=timestamp(item.resetsAt);return quotaLevel(item.remainingPercent)!=='unknown'&&(item.resetsAt==null||(Number.isFinite(reset)&&reset>Date.now()));
  });
  const headerLevel=headerReady?quotaLevel(Math.min(...headerWindows.map(item=>item.remainingPercent))):'unknown';
  const button=(method,label)=>`<button class="${compact?'text-button login-link':'primary-button'}" data-action="${method}" data-id="${id}">${label}</button>`;
  let content='';
  if(hasQuota) {
    if(!compact&&provider.account) content+=`<p class="provider-account">${escapeHtml(provider.account)}${provider.plan?' · '+escapeHtml(provider.plan):''}</p>`;
    // Always retain account-wide weekly and five-hour quota in the small
    // widget. Model-specific limits remain in the complete Settings list.
    const shown=compact?(principal.length?principal:[...valid].sort((a,b)=>a.remainingPercent-b.remainingPercent).slice(0,2)):valid;
    const rings=compact&&settings.quotaDisplay==='rings';
    const entries=shown.map(item=>{
      const remaining=quotaLevel(item.remainingPercent)==='unknown'?null:item.remainingPercent;
      const resetAt=timestamp(item.resetsAt), uncertain=remaining===null||stale||(item.resetsAt!=null&&(!Number.isFinite(resetAt)||resetAt<=Date.now()));
      const level=uncertain?'unknown':quotaLevel(remaining);
      const label=t(compact?shortLabel(item):(item.label||'可用额度'));
      const reset=stale?t('数据已过期，等待最新额度'):resetDescription(item);
      const display=remaining>0&&remaining<1?'&lt;1':Math.round(remaining);
      const hint=`${t('{label}：{value}',{label:t(item.label||'可用额度'),value:uncertain?t('待同步'):t('{value}% 剩余',{value:remaining})})} · ${reset}${resetAt?' · '+formatDate(resetAt):''} · ${refreshAge(provider.updatedAt)}${compact&&valid.length>shown.length?t(' · 更多模型额度见设置'):''}`;
      const period=shortLabel(item)==='总周'?'weekly':shortLabel(item)==='5小时'?'short':'other';
      return {remaining,uncertain,level,label,reset,display,hint,period,item};
    });
    const readings=`<div class="quota-windows${rings?' quota-ring-legend':''}">`+entries.map(entry=>{
      const {remaining,uncertain,level,label,reset,display,hint,period,item}=entry;
      const meter=`<div class="quota-track" role="meter" aria-label="${escapeHtml(t('{label}剩余',{label:t(item.label||'额度')}))}" aria-valuemin="0" aria-valuemax="100"${uncertain?'':` aria-valuenow="${remaining}"`}><div class="quota-fill" style="width:${uncertain?0:remaining}%"></div></div>`;
      const track=rings?'':compact?`<div class="quota-bar">${meter}</div>`:meter;
      return `<div class="quota-window ${level}" data-quota-level="${level}" data-period="${period}" title="${escapeHtml(hint)}"><div class="quota-line"><span class="quota-label">${escapeHtml(label)}</span><span class="quota-number">${uncertain?'—':display}<small>${uncertain?t('待同步'):'%'}</small></span></div>${track}${compact?'':`<p class="quota-reset">${escapeHtml(reset)}</p>`}</div>`;
    }).join('')+'</div>';
    if(rings) {
      const control=entries.find(entry=>entry.period==='short')||entries.find(entry=>entry.period==='weekly')||entries[0];
      const hint=`${t('{name} · 外环：{outer}{inner} · 中央：{center}',{name,outer:entries[0].label,inner:entries[1]?t(' · 内环：{label}',{label:entries[1].label}):'',center:control.label})}\n${entries.map(entry=>entry.hint).join('\n')}`;
      content+=`<div class="quota-ring-layout"><div class="quota-ring-group" role="group" aria-label="${t('{name}额度',{name})}" title="${escapeHtml(hint)}">${quotaRingsSvg(entries,id)}<div class="quota-ring-value quota-number" data-period="${control.period}" data-quota-level="${control.level}" aria-hidden="true">${control.uncertain?'—':control.display}${control.uncertain?'':'<small>%</small>'}</div></div>${readings}</div>`;
    } else content+=readings;
  } else if(busy) {
    content+=compact?(provider.status==='awaiting_login'?button('cancel',t('等待官方登录 · 取消')):`<p class="provider-copy">${t('正在连接账号…')}</p>`):`<p class="provider-copy">${t(provider.status==='awaiting_login'?'请在打开的官方页面完成登录，完成后自动同步。':'正在连接账号，请稍候…')}</p>${provider.status==='awaiting_login'?button('cancel',t('取消登录')):''}`;
  } else if(provider.status==='signed_out') content+=button('login',loginLabel);
  else if(id==='claude'&&provider.needsSetup) content+=button('setup',t('完成官方首次设置'));
  else if(waiting) content+=button('refresh',t('等待额度 · 点击重新同步'));
  else content+=button('login',loginLabel);
  if(!compact) {
    if(provider.error) content+=`<p class="provider-error">${escapeHtml(t(provider.error))}</p>`;
    if(provider.notice) content+=`<p class="provider-copy">${escapeHtml(t(provider.notice))}</p>`;
    if(provider.status!=='signed_out'&&!busy) content+=`<div class="provider-actions"><button class="text-button" data-action="usage" data-id="${id}">${t('官方用量页')}</button><button class="text-button" data-action="logout" data-id="${id}">${t('退出登录')}</button></div>`;
  }
  const age=refreshAge(provider.updatedAt);
  const title=escapeHtml([provider.account,statusLabel(provider),age,t(provider.error),t(provider.notice)].filter(Boolean).join(' · '));
  const status=compact&&provider.status==='connected'&&updated?age:statusLabel(provider);
  if(!compact&&updated) content+=`<p class="provider-copy">${escapeHtml(age)} · ${escapeHtml(formatDate(updated))}</p>`;
  return `<article class="provider ${compact?'':'settings-provider'}" data-provider="${id}" data-quota-level="${headerLevel}"><div class="provider-title" title="${title}"><h3>${name}</h3><span class="provider-status ${provider.status}">${status}</span></div>${content}</article>`;
}
export function bindProviderActions(element) {
  element.addEventListener('click',event=>{
    const button=event.target.closest('button[data-action]'); if(!button)return;
    const id=button.dataset.id,method=button.dataset.action;
    void action(()=>method==='refresh'?bridge.refresh():method==='setup'?bridge.completeClaudeSetup():method==='usage'?bridge.openUsage(id):method==='logout'||method==='cancel'?bridge.logout(id):bridge.login(id));
  });
}
