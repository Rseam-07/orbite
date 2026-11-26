'use strict';
window.OrbitMotion=(()=>{
 const reduce=matchMedia('(prefers-reduced-motion: reduce)');
 const ease='cubic-bezier(.18,.69,.27,1)';
 const navigation=new Set(),micro=new Set();let generation=0;
 function stop(clearView=true){
  for(const a of navigation)a.cancel();
  navigation.clear();
  if(clearView)document.getElementById('scene')?.removeAttribute('data-turn');
 }
 function animate(node,frames,opts,group=micro){
  const a=node.animate(frames,opts);group.add(a);
  a.finished.catch(()=>{}).finally(()=>{if(opts.fill!=='both')group.delete(a)});return a;
 }
 function geometry(rect,tile){
  if(!rect||rect.width<40||rect.height<40)return null;
  return tile&&tile.width>8&&tile.height>8?{
   x:tile.left+tile.width/2-rect.left-rect.width/2,
   y:tile.top+tile.height/2-rect.top-rect.height/2,
   sx:Math.min(.65,Math.max(.31,tile.width/rect.width*1.7)),
   sy:Math.min(.52,Math.max(.28,tile.height/rect.height*1.8))
  }:{x:Math.round(-rect.width*.16),y:-Math.round(rect.height*.07),sx:.46,sy:.43};
 }
 function transform(p,x,s,rotate){
  const sy=1-(1-p.sy)*s,sx=1-(1-p.sx)*s;
  return 'translate3d('+Math.round(p.x*x)+'px,'+Math.round(p.y*x)+'px,0) perspective(1250px) rotateY('+rotate+'deg) scale('+sx+','+sy+')';
 }
 function scene(change,{direction='forward',source=null,target=null,content=null}={}){
  const id=++generation;
  stop();
  const stage=document.getElementById('scene'),before=stage?.dataset.view;
  const can=!!stage&&!reduce.matches&&document.visibilityState!=='hidden'&&typeof Element.prototype.animate==='function';
  if(!can){change();return Promise.resolve()}
  const back=direction==='back',surface=document.getElementById('body'),hub=document.getElementById('hub');
  const full=back?before==='thread':before==='home';
  if(!full){change();const n=content?.();if(n)animate(n,[{opacity:.74,transform:'translate3d(7px,0,0)'},{opacity:1,transform:'translate3d(0,0,0)'}],{duration:130,easing:ease},navigation);return Promise.resolve()}
  const bounds=stage.getBoundingClientRect();
  const beforeTile=source?.querySelector('.tile-signal')??source;
  const origin=beforeTile?.getBoundingClientRect();
  stage.dataset.turn=back?'back':'forward';
  // Both routes stay live behind the flip. No whole-window screenshots, overlays or bitmap stretching.
  change();
  const backTarget=back?target?.():null;
  const p=geometry(bounds,back?backTarget?.getBoundingClientRect():origin);
  if(!p){stage.removeAttribute('data-turn');return Promise.resolve()}
  const opening=[
   {transform:transform(p,1,1,-87),offset:0},
   {transform:transform(p,.78,.68,-65),offset:.23},
   {transform:transform(p,.34,.18,-27),offset:.53},
   {transform:'translate3d(0,0,0) perspective(1250px) rotateY(0deg) scale(1)',offset:1}
  ];
  const page=animate(surface,opening,{duration:back?245:285,direction:back?'reverse':'normal',easing:ease,fill:'both'},navigation);
  animate(hub,[
   {opacity:1,transform:'translate3d(0,0,0)',offset:0},
   {opacity:.90,transform:'translate3d(-8px,0,0)',offset:.31},
   {opacity:0,transform:'translate3d(-52px,0,0)',offset:1}
  ],{duration:back?230:255,direction:back?'reverse':'normal',easing:ease,fill:'both'},navigation);
  return page.finished.catch(()=>{}).finally(()=>{
   if(id!==generation)return;
   stage.removeAttribute('data-turn');stop(false);
  });
 }
 function enter(node,key='small'){
  if(!node||reduce.matches||typeof node.animate!=='function')return;
  const distance=key==='approval'?7:4;
  animate(node,[{opacity:0,transform:'translate3d(0,'+distance+'px,0)'},{opacity:1,transform:'translate3d(0,0,0)'}],{duration:key==='approval'?200:145,easing:ease});
 }
 reduce.addEventListener('change',()=>{if(reduce.matches){generation++;stop();for(const a of micro)a.cancel();micro.clear()}});
 return Object.freeze({scene,enter,get reduced(){return reduce.matches},strategy:'live-turnstile'});
})();
