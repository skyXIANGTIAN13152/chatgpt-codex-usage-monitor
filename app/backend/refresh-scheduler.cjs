'use strict';
const {quotaMaxAgeMs} = require('./energy-selection.cjs');

const ACTIVE_STATUSES = new Set(['connected','connecting','error','quota_pending']);

function intervalMs(settings) {
  const seconds=settings?.refreshSeconds;
  return (Number.isInteger(seconds)&&seconds>=30&&seconds<=3600?seconds:60)*1000;
}

function activeStates(states) {
  return Object.values(states||{}).filter(state=>state&&ACTIVE_STATUSES.has(state.status)&&!state.needsSetup);
}

function invalidWindow(window, now) {
  if(!window||!Number.isFinite(window.remainingPercent)||window.remainingPercent<0||window.remainingPercent>100) return true;
  if(window.resetsAt==null) return false;
  const reset=Date.parse(window.resetsAt);
  return !Number.isFinite(reset)||reset<=now;
}

// One timer owns periodic polling, data expiry and bounded retries. Provider
// snapshots remain read-only; only a real provider refresh can advance their age.
class RefreshScheduler {
  constructor({refresh,getSettings,getStates,now=Date.now,
    setTimeout:setTimer=setTimeout,clearTimeout:clearTimer=clearTimeout,retryMs=30000}={}) {
    if(typeof refresh!=='function'||typeof getSettings!=='function'||typeof getStates!=='function') {
      throw new TypeError('RefreshScheduler requires refresh, settings and state callbacks.');
    }
    this.refresh=refresh;this.getSettings=getSettings;this.getStates=getStates;
    this.now=now;this.setTimer=setTimer;this.clearTimer=clearTimer;
    this.retryMs=Math.max(30000,Number.isFinite(retryMs)?retryMs:30000);
    this.running=false;this.timer=null;this.pending=null;this.retryAt=null;this.failedAttempt=false;
    this.interval=intervalMs(getSettings());this.periodicAt=null;
  }

  start() {
    if(this.running) return this.update();
    this.running=true;this.periodicAt=this.now()+this.interval;
    this.update();
  }

  stop() { this.running=false;this.clearTimer(this.timer);this.timer=null; }

  needsRetry(now=this.now()) {
    const maxAge=quotaMaxAgeMs(this.getSettings());
    return activeStates(this.getStates()).some(state=>{
      if(state.error||state.status==='error'||state.status==='quota_pending') return true;
      const age=now-Date.parse(state.updatedAt);
      return !Number.isFinite(age)||age<0||age>maxAge||!Array.isArray(state.windows)||!state.windows.length||
        state.windows.some(window=>invalidWindow(window,now));
    });
  }

  deadline() {
    const maxAge=quotaMaxAgeMs(this.getSettings());
    let deadline=Infinity;
    for(const state of activeStates(this.getStates())) {
      const updated=Date.parse(state.updatedAt);
      if(Number.isFinite(updated)) deadline=Math.min(deadline,updated+maxAge+1);
      for(const window of Array.isArray(state.windows)?state.windows:[]) {
        if(window?.resetsAt==null) continue;
        const reset=Date.parse(window.resetsAt);
        if(Number.isFinite(reset)) deadline=Math.min(deadline,reset);
      }
    }
    return deadline;
  }

  update() {
    if(!this.running) return;
    const now=this.now(), interval=intervalMs(this.getSettings());
    // Animation, visibility and target changes retain the existing polling time.
    if(interval!==this.interval) {this.interval=interval;this.periodicAt=now+interval;}
    if(!this.pending) {
      if(this.failedAttempt||this.needsRetry(now)) this.retryAt??=now+this.retryMs;
      else this.retryAt=null;
    }
    this.schedule();
  }

  schedule() {
    this.clearTimer(this.timer);this.timer=null;
    if(!this.running||this.pending) return;
    const now=this.now();
    const due=this.retryAt??Math.min(this.periodicAt,this.deadline());
    this.timer=this.setTimer(()=>{this.timer=null;this.refreshNow().catch(()=>{});},Math.max(0,due-now));
    this.timer?.unref?.();
  }

  refreshNow() {
    if(this.pending) return this.pending;
    this.clearTimer(this.timer);this.timer=null;
    let failed=false;
    const operation=Promise.resolve().then(()=>this.refresh()).catch(error=>{failed=true;throw error;});
    this.pending=operation.finally(()=>{
      this.pending=null;
      const now=this.now();
      this.failedAttempt=failed;
      this.interval=intervalMs(this.getSettings());this.periodicAt=now+this.interval;
      this.retryAt=failed||this.needsRetry(now)?now+this.retryMs:null;
      this.schedule();
    });
    return this.pending;
  }

  resume() {
    if(this.pending) return this.pending;
    if(!this.running) return Promise.resolve();
    if(this.retryAt!==null&&this.now()<this.retryAt) {this.schedule();return Promise.resolve();}
    return this.refreshNow();
  }
}

module.exports={RefreshScheduler};
