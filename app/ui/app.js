import { $, $$, bridge, emptySnapshot, mergeSnapshot, action, notify, providerMarkup, bindProviderActions, timestamp, refreshAge } from './shared.js';
import { installCharacterShape, measureWidgetInfoRects } from './shape.js';
import { t, localizeDocument } from './i18n.js';
localizeDocument();
let snapshot=emptySnapshot();
let activeVideo=$('#video-a'), currentScene='full', transitionRunning=false, queuedScene=null;
const scenes={full:t('能量充足'),low:t('红灯状态'),stone:t('等待恢复')};
const mediaPreference=window.matchMedia('(prefers-reduced-motion: reduce)');
const reduced=()=>snapshot.settings.reducedMotion||(snapshot.settings.followSystemMotion&&mediaPreference.matches);
const shapeReady=installCharacterShape(bridge).catch(()=>null);
let shapeScenes=['full'];
async function syncShape() { const apply=await shapeReady; if(apply) await apply(shapeScenes,{infoRects:measureWidgetInfoRects()}); }
async function shapeFor(...scenes) { shapeScenes=scenes;await syncShape(); }
let shapeFrame=null;
function scheduleShape() {
  if(shapeFrame!==null)return;
  shapeFrame=requestAnimationFrame(()=>{shapeFrame=null;void syncShape().catch(()=>{});});
}
function setMotion() {
  document.body.classList.toggle('reduced-motion',reduced());
  const stopped=snapshot.settings.paused||reduced()||document.hidden;
  document.body.classList.toggle('motion-paused',stopped);
  document.body.dataset.energy=currentScene;
  for(const video of $$('.scene-video')) {
    if(stopped||(video!==activeVideo&&!transitionRunning)) video.pause();
    else video.play().catch(()=>{});
  }
  const label=t(reduced()?'播放动画（当前为静态画面）':snapshot.settings.paused?'播放动画':'暂停动画');
  $('#pause').title=label;
  $('#pause').setAttribute('aria-label',label);
  $('#pause').setAttribute('aria-pressed',String(!!snapshot.settings.paused||reduced()));
  $('#pause').disabled=false;
  $('#pause').querySelector('svg').innerHTML=reduced()||snapshot.settings.paused?'<path d="m8 5 11 7-11 7z"/>':'<path d="M8 5v14M16 5v14"/>';
}
async function changeScene(scene) {
  if (!scenes[scene]) scene = 'full';
  if (transitionRunning) { queuedScene = scene; return; }
  if (scene === currentScene) { setMotion(); return; }
  transitionRunning = true;
  const previous = activeVideo;
  const next = previous.id === 'video-a' ? $('#video-b') : $('#video-a');
  next.classList.remove('is-visible');
  next.poster = `assets/characters/${scene}.png`;
  next.src = `assets/characters/${scene}.webm`;
  next.load();
  let mediaFailed = false;
  await new Promise((resolve) => {
    const timer = setTimeout(done, 6000);
    function done() { clearTimeout(timer); next.removeEventListener('loadeddata', done); next.removeEventListener('error', failed); resolve(); }
    function failed() { mediaFailed = true; done(); }
    next.addEventListener('loadeddata', done, { once: true });
    next.addEventListener('error', failed, { once: true });
    if (next.readyState >= 2) done();
  });
  if (queuedScene && queuedScene !== scene) {
    const latest = queuedScene;
    queuedScene = null;
    transitionRunning = false;
    void changeScene(latest);
    return;
  }
  if (mediaFailed) notify(t('这段动画暂时无法播放，已保留对应静态画面。'));
  await shapeFor(currentScene,scene).catch(()=>{});
  activeVideo = next;
  currentScene = scene;
  $('#scene-stage').dataset.state = scene;
  document.body.dataset.energy = scene;
  if (!snapshot.settings.paused && !reduced()) next.play().catch(() => {});
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  next.classList.add('is-visible');
  previous.classList.remove('is-visible');
  await new Promise((resolve) => setTimeout(resolve, reduced() ? 0 : 1850));
  previous.pause();
  await shapeFor(currentScene).catch(()=>{});
  transitionRunning = false;
  setMotion();
  if (queuedScene) { const latest = queuedScene; queuedScene = null; void changeScene(latest); }
}


let lastMarkup='';
function render() {
  document.body.dataset.quotaDisplay=snapshot.settings.quotaDisplay;
  const ids=['codex','claude'].filter(id=>snapshot.settings.visible[id]);
  const markup=ids.map(id=>providerMarkup(snapshot.providers[id],snapshot.settings,true)).join('');
  if(markup!==lastMarkup) { $('#provider-list').innerHTML=markup; lastMarkup=markup; }
  $('#no-providers').hidden=ids.length!==0;
  $('#provider-list').hidden=ids.length===0;
  const actual=snapshot.energy?.scene||'unknown', preview=snapshot.previewScene;
  const scene=preview||(actual==='unknown'?(snapshot.energyTarget?.status==='ready'?currentScene:'full'):actual);
  $('#preview-badge').hidden=!preview;
  $('#preview-badge').textContent=preview?t('预览 · {scene}',{scene:scenes[preview]}):'';
  const active=(snapshot.energyTarget?.memberIds||[]).map(id=>snapshot.providers[id]).filter(p=>p&&['connected','connecting','error'].includes(p.status));
  const updates=active.map(p=>timestamp(p.updatedAt)).filter(value=>value>0);
  const updated=updates.length?Math.min(...updates):null;
  const syncing=active.some(p=>p.status==='connecting')||$('#refresh').disabled;
  $('#scene-mode').textContent=preview?t('动画预览 · {scene}',{scene:scenes[preview]}):syncing?t('刷新中 · {age}',{age:refreshAge(updated)}):actual==='unknown'?t('目标额度待同步'):`${scenes[scene]} · ${refreshAge(updated)}`;
  $('#sync-status').dataset.active=String(actual!=='unknown');
  $('#sync-status').dataset.preview=String(!!preview);
  $('#sync-status').title=preview?t('动画预览，不代表真实额度'):t('每 {seconds} 秒刷新；{age}{oldest}；悬停额度查看重置倒计时',{seconds:snapshot.settings.refreshSeconds,age:refreshAge(updated),oldest:updates.length>1?t('（按所选应用中较早的一次刷新）'):''});
  $('#pin').setAttribute('aria-pressed',String(!!snapshot.settings.alwaysOnTop));
  void changeScene(scene); setMotion();
  scheduleShape();
}
function applySnapshot(next) {
  snapshot=mergeSnapshot(snapshot,next);
  render();
}
bindProviderActions($('#provider-list'));
for(const button of $$('[data-window]')) button.addEventListener('click',()=>void action(()=>bridge.windowAction(button.dataset.window)));
$('#pause').addEventListener('click',()=>void action(async()=>applySnapshot(await bridge.updateSettings(reduced()?{paused:false,reducedMotion:false,followSystemMotion:false}:{paused:!snapshot.settings.paused}))));
$('#pin').addEventListener('click',()=>void action(()=>bridge.windowAction('pin')));
$('#refresh').addEventListener('click',()=>void action(async()=>{ $('#refresh').disabled=true;render();try { await bridge.refresh(); } finally { $('#refresh').disabled=false;render(); } }));
mediaPreference.addEventListener('change',setMotion);
document.addEventListener('visibilitychange',setMotion);
bridge.subscribe(applySnapshot);
void action(async()=>applySnapshot(await bridge.getSnapshot()));
setInterval(render,5000);
activeVideo.addEventListener('loadeddata',setMotion,{once:true});
// If a media asset is unavailable, preserve the usable window instead of hiding controls.
shapeFor(currentScene).catch(()=>{});
const informationResize=new ResizeObserver(scheduleShape);
for(const element of $$('.widget-info,.widget-accounts,.widget-actions,.widget-status')) informationResize.observe(element);
const toastChanges=new MutationObserver(scheduleShape);
toastChanges.observe($('#toast'),{attributes:true,attributeFilter:['hidden'],childList:true,subtree:true,characterData:true});
setMotion();
