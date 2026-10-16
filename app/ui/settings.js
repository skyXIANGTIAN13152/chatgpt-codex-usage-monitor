import { $, $$, bridge, emptySnapshot, mergeSnapshot, action, notify, escapeHtml, providerMarkup, bindProviderActions } from './shared.js';
import { t, localizeDocument } from './i18n.js';
localizeDocument();
let snapshot=emptySnapshot();
const targetIds=['codex','claude'];
const targetNames={codex:'Codex',claude:'Claude Code',aggregate:t('聚合模式')};
let targetDraft=null, targetPending=0, targetRevision=0, targetSaveError=false;
let targetSaveQueue=Promise.resolve();

function targetConfig(settings) {
  const mode=['codex','claude','aggregate'].includes(settings.energyTarget)?settings.energyTarget:'codex';
  const members=targetIds.filter(id=>settings.aggregateMembers?.includes(id));
  return {mode,members:members.length?members:[...targetIds]};
}
function validPercent(value) { return typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=100; }
function percentText(value) { return validPercent(value)?`${Number(value.toFixed(1))}%`:t('待同步'); }
function renderTargetSettings() {
  const config=targetDraft||targetConfig(snapshot.settings);
  $('#energy-target').value=config.mode;
  $('#aggregate-members').hidden=config.mode!=='aggregate';
  for(const id of targetIds) {
    const input=$(`#aggregate-${id}`);
    input.checked=config.members.includes(id);
    input.disabled=config.members.length===1&&input.checked;
    input.title=input.disabled?t('至少保留一个聚合成员；请先选择另一个账号'):'';
  }
  const selected=config.mode==='aggregate'?config.members:[config.mode];
  const summary=snapshot.energyTarget;
  const matches=summary&&summary.mode===config.mode&&
    targetIds.filter(id=>summary.memberIds?.includes(id)).join(',')===selected.join(',');
  const ready=matches&&summary.status==='ready'&&validPercent(summary.remainingPercent);
  $('#target-result-label').textContent=t('{name} · 当前控制额度',{name:targetNames[config.mode]});
  $('#target-summary').textContent=ready?t('{value} 剩余',{value:percentText(summary.remainingPercent)}):t('待同步');
  $('#target-result').dataset.state=matches?summary.status:'pending';
  const aggregate=config.mode==='aggregate';
  const periods=matches&&Array.isArray(summary.windows)?summary.windows:[];
  const markup=periods.map(window=>{
    const label=t(window.label||(window.id==='weekly'?'每周':'短周期'));
    const counts=Number.isInteger(window.memberCount)&&Number.isInteger(window.totalCount)&&
      window.memberCount>=0&&window.totalCount>0&&window.memberCount<=window.totalCount
      ?t('{count} / {total} 个账号已同步',{count:window.memberCount,total:window.totalCount}):'';
    const value=validPercent(window.remainingPercent)?t('{value} 剩余',{value:percentText(window.remainingPercent)}):t('待同步');
    return `<li class="target-period"><span>${escapeHtml(label)}${aggregate?t(' · 平均'):''}</span><span>${value}${aggregate&&counts?`<small>${counts}</small>`:''}</span></li>`;
  }).join('');
  if($('#target-periods').innerHTML!==markup) $('#target-periods').innerHTML=markup;
  let explanation;
  if(!matches) explanation=t(targetPending?'正在更新所选目标的实际额度。':'等待所选目标的有效额度。');
  else if(ready) {
    const period=t(summary.controllingPeriod==='short'?'五小时':summary.controllingPeriod==='weekly'?'总周':'有效');
    explanation=aggregate
      ?t('各选中账号等权计算，当前由{period}额度决定迪迦状态；隐藏账号不会改变计算结果。',{period})
      :t('当前由此账号的{period}额度决定迪迦状态；隐藏账号不会改变此目标。',{period});
  }
  else if(aggregate) {
    const missing=targetIds.filter(id=>summary.missingIds?.includes(id)).map(id=>targetNames[id]);
    explanation=summary.reason==='unsupported_periods'
      ?t('部分额度周期暂不能合并。聚合结果尚不完整，暂不判断迪迦状态；已同步的可比周期仍可查看。')
      :`${missing.length?t('{names} 的优先周期额度待同步。',{names:missing.join(t('、'))}):t('优先周期尚未同步。')}${t('暂不判断迪迦状态；其他已同步周期仍可查看。')}`;
  } else {
    const status=snapshot.providers[config.mode]?.status;
    explanation=status==='signed_out'?t('{name} 尚未登录，请在下方账号区域登录。',{name:targetNames[config.mode]})
      :status==='awaiting_login'?t('请先在官方页面完成 {name} 登录。',{name:targetNames[config.mode]})
      :t('{name} 暂无有效额度，等待同步或更新已过期的数据。',{name:targetNames[config.mode]});
  }
  $('#target-explanation').textContent=explanation;
  $('#target-save-status').textContent=targetPending?t('正在保存选择…'):targetSaveError?t('未能保存，请检查当前选择后重试。'):'';
  $('#target-save-status').dataset.error=String(targetSaveError&&!targetPending);
}

function saveTargetSelection() {
  const revision=++targetRevision;
  const patch={energyTarget:targetDraft.mode,aggregateMembers:[...targetDraft.members]};
  targetPending++;
  targetSaveError=false;
  renderTargetSettings();
  // Capture each complete intent. Later member toggles never derive from an
  // older IPC response, and mode/member changes remain ordered on slow hosts.
  const operation=targetSaveQueue.then(async()=>{
    try {
      applySnapshot(await bridge.updateSettings(patch));
      if(revision===targetRevision) targetSaveError=false;
    } catch(error) {
      if(revision===targetRevision) targetSaveError=true;
      notify(error?.message||t('显示目标未能保存，请重试。'));
      try { applySnapshot(await bridge.getSnapshot()); } catch {}
    } finally {
      targetPending--;
      if(!targetPending) targetDraft=targetConfig(snapshot.settings);
      renderTargetSettings();
    }
  });
  targetSaveQueue=operation.catch(()=>{});
  return operation;
}

function applySnapshot(next) {
  snapshot=mergeSnapshot(snapshot,next);
  if(!targetPending) targetDraft=targetConfig(snapshot.settings);
  renderTargetSettings();
  for(const id of ['codex','claude']) {
    $(`#show-${id}`).checked=!!snapshot.settings.visible[id];
    $(`#launch-${id}`).checked=!!snapshot.settings[id==='codex'?'launchWithCodex':'launchWithClaude'];
  }
  $('#reduce-motion').checked=!!snapshot.settings.reducedMotion||(snapshot.settings.followSystemMotion&&window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  $('#always-on-top').checked=!!snapshot.settings.alwaysOnTop;
  $('#quota-display').value=snapshot.settings.quotaDisplay;
  $('#floating-scale').value=String(snapshot.settings.floatingScale);
  const threshold=String(snapshot.settings.warningThreshold);
  if(![...$('#threshold').options].some(o=>o.value===threshold)) $('#threshold').add(new Option(`${threshold}%`,threshold));
  $('#threshold').value=threshold;
  const accountMarkup=['codex','claude'].map(id=>providerMarkup(snapshot.providers[id],snapshot.settings)).join('');
  if($('#account-settings').innerHTML!==accountMarkup) $('#account-settings').innerHTML=accountMarkup;
  $('#follow-quota').hidden=!snapshot.previewScene;
  $('#preview-note').textContent=t(snapshot.previewScene?'浮窗正在预览动画，不代表实际额度。点击“跟随真实额度”退出预览。':'点击后在悬浮窗中预览，账号额度保持真实显示。');
  for(const button of $$('.scene-option')) { button.classList.toggle('active',button.dataset.scene===snapshot.previewScene); button.setAttribute('aria-pressed',String(button.dataset.scene===snapshot.previewScene)); }
}
async function update(patch) { applySnapshot(await bridge.updateSettings(patch)); }
$('#energy-target').addEventListener('change',event=>{
  targetDraft={...(targetDraft||targetConfig(snapshot.settings)),mode:event.target.value};
  void saveTargetSelection();
});
for(const id of targetIds) $(`#aggregate-${id}`).addEventListener('change',event=>{
  const current=targetDraft||targetConfig(snapshot.settings);
  const members=targetIds.filter(member=>member===id?event.target.checked:current.members.includes(member));
  if(!members.length) { event.target.checked=true; notify(t('至少保留一个聚合成员。')); return; }
  targetDraft={...current,members};
  void saveTargetSelection();
});
for(const id of ['codex','claude']) {
  $(`#show-${id}`).addEventListener('change',event=>void action(()=>update({visible:{[id]:event.target.checked}})));
  $(`#launch-${id}`).addEventListener('change',event=>void action(()=>update({[id==='codex'?'launchWithCodex':'launchWithClaude']:event.target.checked})));
}
$('#reduce-motion').addEventListener('change',event=>void action(()=>update({reducedMotion:event.target.checked,followSystemMotion:false})));
$('#quota-display').addEventListener('change',event=>void action(()=>update({quotaDisplay:event.target.value})));
for(const [selector,key,number] of [['#always-on-top','alwaysOnTop',false],['#floating-scale','floatingScale',true],['#threshold','warningThreshold',true]]) $(selector).addEventListener('change',event=>void action(()=>update({[key]:number?Number(event.target.value):event.target.checked})));
for(const button of $$('.scene-option')) button.addEventListener('click',()=>void action(()=>bridge.windowAction(`preview-${button.dataset.scene}`)));
$('#follow-quota').addEventListener('click',()=>void action(()=>bridge.windowAction('preview-auto')));
for(const id of ['settings-close','settings-done']) $(`#${id}`).addEventListener('click',()=>void action(()=>bridge.windowAction('close')));
bindProviderActions($('#account-settings'));
bridge.subscribe(applySnapshot);
void action(async()=>applySnapshot(await bridge.getSnapshot()));
setInterval(()=>applySnapshot(snapshot),30000);
