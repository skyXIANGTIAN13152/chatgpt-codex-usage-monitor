// Native hit region follows the character silhouettes, so empty desktop remains clickable.
// During transitions, temporarily keep both poses so no light or limb is clipped.
export function measureWidgetInfoRects() {
  const widget=document.querySelector('.floating-widget');
  if(!widget)return [];
  const origin=widget.getBoundingClientRect(),rects=[];
  const scaleX=420/origin.width,scaleY=246/origin.height;
  function add(element,padding={},minimumX=236) {
    if(!element||element.hidden||!element.getClientRects().length)return;
    const bounds=element.getBoundingClientRect();
    if(bounds.width<=0||bounds.height<=0)return;
    const left=Math.max(minimumX,(bounds.left-origin.left)*scaleX-(padding.left??2));
    const top=Math.max(0,(bounds.top-origin.top)*scaleY-(padding.top??2));
    const right=Math.min(420,(bounds.right-origin.left)*scaleX+(padding.right??2));
    const bottom=Math.min(246,(bounds.bottom-origin.top)*scaleY+(padding.bottom??2));
    if(right>left&&bottom>top)rects.push({x:left,y:top,width:right-left,height:bottom-top});
  }
  add(document.querySelector('.widget-accounts'));
  for(const element of document.querySelectorAll('.quota-ring-group,.widget-accounts .quota-track')) add(element);
  for(const selector of ['.widget-actions','.widget-status','#preview-badge','#toast']) add(document.querySelector(selector));

  // Preserve the thin energy arc itself, without its former broad empty box.
  const thread=document.querySelector('.energy-thread'),path=thread?.querySelector('path');
  if(path&&thread.getClientRects().length) {
    const bounds=thread.getBoundingClientRect(),view=thread.viewBox.baseVal,length=path.getTotalLength();
    for(let index=0;index<=24;index++) {
      const point=path.getPointAtLength(length*index/24);
      const x=(bounds.left-origin.left+(point.x-view.x)*bounds.width/view.width)*scaleX;
      const y=(bounds.top-origin.top+(point.y-view.y)*bounds.height/view.height)*scaleY;
      rects.push({x:Math.max(0,x-1.5),y:Math.max(0,y-1.5),width:3,height:3});
    }
  }
  return rects;
}

export async function installCharacterShape(bridge) {
  if (!bridge.updateShape) return null;
  const canvas=document.createElement('canvas');
  canvas.width=420; canvas.height=246;
  const context=canvas.getContext('2d',{willReadFrequently:true});
  const masks={};
  for(const scene of ['full','low','stone']) {
    const img=new Image(); img.src=`assets/characters/${scene}.png`;
    await img.decode();
    context.clearRect(0,0,420,246);
    context.drawImage(img,0,8,260,195);
    masks[scene]=context.getImageData(0,0,420,246).data;
  }
  const cache=new Map();
  let appliedSignature=null;
  return async function setScenes(scenes,{infoRects=measureWidgetInfoRects()}={}) {
    const key=[...new Set(scenes)].sort().join(',');
    if(!cache.has(key)) {
      const layers=scenes.map(scene=>masks[scene]).filter(Boolean);
      const rects=[],step=2;
      for(let y=0;y<246;y+=step) {
        let start=-1;
        for(let x=0;x<=420;x+=step) {
          let opaque=false;
          // Reserve the existing edge allowance for resampling and the soft
          // original glow. All three video silhouettes now remain fixed.
          if(x<420) for(let sy=Math.max(0,y-5);sy<Math.min(246,y+step+5)&&!opaque;sy++) {
            for(let sx=Math.max(0,x-5);sx<Math.min(420,x+step+5);sx++) if(layers.some(pixels=>pixels[(sy*420+sx)*4+3]>0)) {opaque=true;break;}
          }
          if(opaque&&start<0) start=x;
          if(!opaque&&start>=0) {rects.push({x:start,y,width:x-start,height:Math.min(step,246-y)});start=-1;}
        }
      }
      cache.set(key,rects);
    }
    const rects=[...cache.get(key),...infoRects];
    const signature=JSON.stringify(rects);
    if(signature===appliedSignature)return;
    appliedSignature=signature;
    try { await bridge.updateShape(rects); }
    catch(error) { if(appliedSignature===signature)appliedSignature=null;throw error; }
  };
}
