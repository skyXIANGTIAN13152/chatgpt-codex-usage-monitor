import { ringLowMaterial } from './quota-materials.js';
import { t } from './i18n.js';
// One account owns one ring group; each period keeps its own value and palette.
export function quotaRingsSvg(entries, providerId) {
  const prefix=String(providerId).replace(/[^a-z0-9_-]/gi,'');
  return `<svg class="quota-rings-svg" viewBox="0 0 40 40">${entries.map((entry,index)=>{
    const radius=entries.length===1||index===0?16.8:11.8;
    const width=radius===16.8?2.8:2.4;
    const value=entry.level==='unknown'?null:entry.remaining;
    const arc=Number.isFinite(value)&&value>0?value:0;
    const gradient=`quota-${prefix}-${index}`;
    const material=entry.level==='low'?ringLowMaterial(radius,width,arc,gradient):null;
    const label=t(entry.period==='weekly'?'总周额度':entry.period==='short'?'5 小时额度':'额度');
    return `<g class="quota-ring" data-period="${entry.period}" data-quota-level="${entry.level}" role="meter" aria-label="${t('{label}剩余',{label})}" aria-valuemin="0" aria-valuemax="100"${value===null?` aria-valuetext="${t('待同步')}"`:` aria-valuenow="${value}"`}>
      <defs><linearGradient id="${gradient}" x1="0%" y1="0%" x2="100%" y2="100%"><stop class="gauge-start" offset="0"/><stop class="gauge-end" offset="1"/></linearGradient></defs>
      <circle class="gauge-track" cx="20" cy="20" r="${radius}" stroke-width="${width}"/>
      <circle class="gauge-fill" cx="20" cy="20" r="${radius}" stroke-width="${width}" pathLength="100" transform="rotate(-90 20 20)" stroke="url(#${gradient})" stroke-dasharray="${arc} 100" opacity="${arc>0?1:0}"/>
      ${material?.surface||''}
    </g>`;
  }).join('')}</svg>`;
}
