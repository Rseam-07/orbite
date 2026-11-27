'use strict';
const motion=window.OrbitMotion;
const $=id=>document.getElementById(id),api=window.orbit,desktop=new URLSearchParams(location.search).has('desktop'),demo=!desktop&&!api,app=$('app');
if(desktop)document.body.classList.add('is-desktop');
const svgNS='http://www.w3.org/2000/svg';
function icon(n){const e=document.createElementNS(svgNS,'svg'),u=document.createElementNS(svgNS,'use');u.setAttribute('href','#i-'+n);e.append(u);return e}
function el(tag,text,cls){const n=document.createElement(tag);if(text!=null)n.textContent=text;if(cls)n.className=cls;return n}
const basename=p=>p?.split(/[\\/]/).filter(Boolean).at(-1)||'';
const storage=demo?'orbit07-preview':'orbit02-local';
let data={projects:[],threads:[]};try{const d=JSON.parse(localStorage.getItem(storage)||'null');if(Array.isArray(d?.projects)&&Array.isArray(d?.threads))data=d}catch{}
const sample='/Demo/Atelier';
const sampleDiff={branch:'main',status:' M Sources/ImportList.swift\n M Tests/ImportListTests.swift',patch:`diff --git a/Sources/ImportList.swift b/Sources/ImportList.swift
--- a/Sources/ImportList.swift
+++ b/Sources/ImportList.swift
@@ -34,4 +34,8 @@ func append(_ incoming: [URL]) {
-    items.append(contentsOf: incoming)
+    for url in incoming {
+        let key = url.standardizedFileURL.path
+        guard seen.insert(key).inserted else { continue }
+        items.append(url)
+    }
 }
diff --git a/Tests/ImportListTests.swift b/Tests/ImportListTests.swift
--- a/Tests/ImportListTests.swift
+++ b/Tests/ImportListTests.swift
@@ -14,3 +14,6 @@ struct ImportListTests {
+    @Test func duplicateURLsAreIgnored() {
+        #expect(importedURLs.count == 1)
+    }
 }`};
if(demo&&!data.projects.length){
 data={projects:[{path:sample},{path:'/Demo/NotesKit'}],threads:[
  {id:'sample-a',folder:sample,title:'修复导入列表的重复项',stamp:'刚刚',messages:[{role:'user',text:'导入同一个文件夹两次，列表里会出现重复项。按文件路径去重，保留原来的排序。',files:[{name:'ImportList.swift',kind:'file',size:4800},{name:'导入样例',kind:'folder'}]},{role:'assistant',text:'问题在导入合并时：第二批文件被直接追加。现在会用规范化路径去重，保留每个文件第一次出现的位置；不同目录下的同名文件仍会分别保留。\n\n改动在 `ImportList.swift`，并加了一个重复导入的回归测试。',status:'2 个文件有改动',tools:[{title:'读取 Sources/ImportList.swift',status:'completed'},{title:'修改 Sources/ImportList.swift',status:'completed'},{title:'补充 Tests/ImportListTests.swift',status:'completed'}]}]},
  {id:'sample-b',folder:sample,title:'核对翻译缓存',stamp:'昨天',messages:[{role:'user',text:'先看翻译缓存怎么落盘。'},{role:'assistant',text:'缓存通过项目里的索引文件记录版本，演示会话未修改实际文件。'}]},
  {id:'sample-c',folder:'/Demo/NotesKit',title:'整理导出测试',stamp:'周一',messages:[{role:'user',text:'梳理导出测试。'},{role:'assistant',text:'这是一条示例历史。可以新建会话，试试添加文件或打开审阅。'}]}
 ],selected:'sample-a'};
}
let view='home',saveTimer=0,scrollTicket=0,reviewMotionTimer=0;
let current=data.threads.find(x=>x.id===data.selected)||null,folder=current?.folder??null,tab='diff',driverSelected=false,cuaStatus=null,cuaTestResult=null,cuaWorking=false;
const authorized=new Set(demo?data.projects.map(x=>x.path):[]),drafts=new Map(),states=new Map(),incomingDrops=new Map();
const demoOptions=[{id:'model',name:'模型（离线预览）',currentValue:'demo-model',options:[{value:'demo-model',name:'Gemini'}]},{id:'mode',name:'模式（离线预览）',currentValue:'default',options:[{value:'default',name:'默认'},{value:'plan',name:'计划'}]}];
const recentToolLimit=100,turnToolLimit=20,activityNoteLimit=60;
function runtime(id=key()){
 if(!states.has(id))states.set(id,{running:false,connecting:false,session:null,authState:null,options:demo?demoOptions:[],reviewSelection:new Map(),review:null,extensionEnabled:false,turns:new Map(),approvals:new Map(),plan:[],activity:[],turnTools:new Map(),activityFrame:0,activityClipped:false,demoTimer:0,error:'',status:'',isolatedReady:false,taskDiff:new Map(),diffMode:'workspace',steering:false,refreshSeq:0,dirtyText:false,flushFrame:0,offlineWhileRunning:false,commands:demo?[{name:'plan',description:'先制定计划'},{name:'compact',description:'压缩会话上下文'}]:[]});
 return states.get(id);
}
function key(){return current?.id??'new:'+String(folder)}
function draft(){if(!drafts.has(key()))drafts.set(key(),{text:'',files:[],isolated:false});return drafts.get(key())}
function persist(){clearTimeout(saveTimer);saveTimer=0;try{localStorage.setItem(storage,JSON.stringify({...data,threads:data.threads.slice(0,80),selected:current?.id??null}))}catch{}}
function scheduleSave(){if(!saveTimer)saveTimer=setTimeout(persist,350)}
window.addEventListener('pagehide',persist);
function plain(t){return String(t||'').replace(/^Error invoking remote method '[^']+': Error: /,'').trim()}
function setStatus(t,id=key()){
 const s=runtime(id);if(t!==undefined)s.status=t;
 if(id===key()){$('runStatus').textContent=s.status;$('sideStatus').textContent=demo?'离线预览':s.session?'已连接':''}
}
function notice(e){runtime().error=plain(e?.message||e);if($('settings').open)$('dialogStatus').textContent=runtime().error;else if($('extensionDialog').open)$('extNote').textContent=runtime().error;else renderLive()}
const closeDlg=id=>{if($(id).open)$(id).close()};
function scanProject(){if(demo){
 const s=runtime();s.review=folder===sample&&current?.id==='sample-a'?sampleDiff:null;
 if(current?.id==='sample-a'&&!s.taskDiff.size)s.taskDiff.set('Sources/ImportList.swift',{before:'func append(_ incoming: [URL]) {\n    items.append(contentsOf: incoming)\n}',after:'func append(_ incoming: [URL]) {\n    for url in incoming {\n        let key = url.standardizedFileURL.path\n        guard seen.insert(key).inserted else { continue }\n        items.append(url)\n    }\n}'});
 renderReview();return
 }refresh()}
function viewLayout(next){
 view=next;$('scene').dataset.view=next;
 $('showHub').classList.toggle('active',next==='home');$('showHub').setAttribute('aria-pressed',String(next==='home'));
 $('openDiff').hidden=next==='home';
}
function go(t,f,source=null,focus=false){
 if(runtime().connecting)return Promise.resolve();
 const anchor=source??(view==='home'?[...$('hubTiles').querySelectorAll('.metro-tile')].find(n=>n.dataset.project===(t?.folder??f??folder)):null);
 const action=()=>{
  current=t;folder=t?.folder??f??folder;viewLayout('thread');persist();render();scanProject();renderActivity();
  if(!t)reviewPanel(false);
  if(innerWidth<=640)sidebar(false);
  if(focus)$('prompt').focus({preventScroll:true});
  else $('routeTitle').focus({preventScroll:true});
 };
 return motion.scene(action,{source:anchor,target:()=>$('routeMarker'),content:()=>document.querySelector('.route-head')});
}
function showHub(){
 if(runtime().connecting||view==='home')return;
 const path=folder;
 motion.scene(()=>{viewLayout('home');renderSidebar();$('hubTitle').focus({preventScroll:true})},{
  direction:'back',source:$('routeMarker'),
  target:()=>[...$('hubTiles').querySelectorAll('.metro-tile')].find(n=>n.dataset.project===path)?.querySelector('.tile-signal'),
  content:()=>$('hubTiles')
 });
 if(innerWidth<=640)sidebar(false);
}
function renderHub(){
 if(view!=='home')return;
 const grid=$('hubTiles'),recent=$('hubRecents');grid.replaceChildren();recent.replaceChildren();
 const projects=data.projects.slice(0,30);
 $('hubProjectCount').textContent=projects.length?String(projects.length):'';
 for(const p of projects){
  const threads=data.threads.filter(t=>t.folder===p.path),pending=threads.filter(t=>runtime(t.id).approvals.size).length,active=threads.filter(t=>runtime(t.id).running).length;
  const row=el('button',null,'metro-tile'),head=el('span',null,'workspace-identity'),marker=el('span',null,'tile-signal'),name=el('span',null,'workspace-label');
  marker.append(icon('folder'));name.append(el('strong',basename(p.path)),el('small',p.path));head.append(marker,name);
  const state=el('span',pending?pending+' 待审批':active?active+' 运行中':threads.length?threads.length+' 个会话':'无会话','workspace-state'+(pending||active?' live-state':''));
  row.append(head,el('span',threads[0]?.title??'未开始','workspace-last'),state,icon('chevron'));
  row.dataset.project=p.path;row.title=p.path;
  row.onclick=()=>go(threads[0]??null,p.path,marker);
  grid.append(row)
 }
 if(!projects.length)grid.append(el('p','还没有项目。','empty-list'));
 const running=data.threads.filter(t=>runtime(t.id).running).length;
 $('hubQueue').textContent=running?running+' 个运行中':'';
 for(const t of data.threads.slice(0,7)){
  const r=el('button',null,'rec-row'),s=runtime(t.id),status=s.approvals.size?'待审批':s.running?'运行中':t.stamp;
  r.append(icon('chat'),el('strong',t.title),el('span',basename(t.folder),'rec-project'),el('time',status??'',s.running||s.approvals.size?'live-state':''));
  r.onclick=()=>go(t);recent.append(r)
 }
 if(!recent.childElementCount)recent.append(el('p','还没有会话。','empty-list'));
}
function addProject(p){if(!data.projects.some(x=>x.path===p))data.projects.unshift({path:p});authorized.add(p)}
async function pickProject(s){
 try{
 const p=demo?(s||'/Demo/示例项目'):await api.pick(typeof s==='string'?s:null);
 if(!p)return null;addProject(p);return p;
 }catch(e){notice(e);return null}
}
function sidebar(show){app.classList.toggle('no-side',!show);$('expand').hidden=show}
function opensSidebar(){sidebar(true);$('searchWrap').hidden=false;$('search').focus()}
function renderSidebar(){
 const root=$('projects'),q=$('search').value.trim().toLowerCase();root.replaceChildren();
 const groups=data.projects.filter(p=>!q||basename(p.path).toLowerCase().includes(q)||data.threads.some(t=>t.folder===p.path&&t.title.toLowerCase().includes(q)));
 for(const p of groups){
  const group=el('section',null,'project-group'),row=el('div',null,'project-row');
  if(p.path===folder){row.classList.add('project-active');if(current)row.classList.add('has-thread')}
  const main=el('button',null,'project-title');main.title=p.path;main.textContent=basename(p.path);
  main.onclick=()=>go(data.threads.find(t=>t.folder===p.path)??null,p.path);
  const plus=el('button',null,'new-small');plus.title='在 '+basename(p.path)+' 新建会话';plus.setAttribute('aria-label',plus.title);plus.append(icon('plus'));plus.onclick=()=>go(null,p.path,null,true);
  row.append(icon('folder'),main,plus);group.append(row);
  for(const t of data.threads.filter(t=>t.folder===p.path&&(!q||basename(p.path).toLowerCase().includes(q)||t.title.toLowerCase().includes(q)))){
   const b=el('button',null,'thread'+(current===t?' selected':''));b.title=t.title;
   const st=runtime(t.id);const live=st.approvals.size?'待审批':st.running?'运行中':t.isolated?'工作树':'';
   b.append(el('span',t.title),el('time',live||t.stamp||'',live?'thread-status':''));b.onclick=()=>go(t);group.append(b);
  }
  root.append(group);
 }
 if(!groups.length)root.append(el('div',q?'没有匹配的会话':'尚未打开项目','project-empty'));
 $('projectName').textContent=view==='home'?'Orbite':basename(folder)||'Orbite';
 $('taskTitle').textContent=view==='home'?'工作台':current?.title||'新建会话';
 $('routeProject').textContent=basename(folder)||'会话';
 $('routeTitle').textContent=current?.title||'新建会话';
 $('routeMarker').replaceChildren(icon(folder?'folder':'chat'));
 renderHub();
 $('scopeName').textContent=basename(folder)||'未选择项目';
 $('emptyText').textContent=folder?'':'选择项目后开始。';
 $('emptyPick').hidden=!!folder;
}
function size(n){return n===0?'0 B':n>=1048576?(n/1048576).toFixed(1)+' MB':n>=1024?Math.round(n/1024)+' KB':n+' B'}
function chip(file,removable){
 const d=el('span',null,'chip');d.title=file.name;d.append(icon(file.clipboard?'image':file.kind==='folder'?'folder':'file'),el('span',file.name,'file-name'));
 if(Number.isFinite(file.size)&&file.kind==='file')d.append(el('span',size(file.size),'size'));
 if(removable){const x=el('button');x.setAttribute('aria-label','移除 '+file.name);x.title='移除';x.append(icon('close'));x.onclick=()=>{draft().files=draft().files.filter(f=>f.id!==file.id);api?.forget(file.id).catch(()=>{});renderAttachments()};d.append(x)}
 return d;
}
function renderAttachments(){
 const f=draft().files,tray=$('attachmentTray'),seen=new Set([...tray.children].map(n=>n.dataset.attachment));tray.hidden=!f.length;
 tray.replaceChildren(...f.map(a=>{const n=chip(a,true);n.dataset.attachment=a.id;return n}));
 [...tray.children].filter(n=>!seen.has(n.dataset.attachment)).forEach(n=>motion.enter(n));
 $('footerHint').textContent=f.length&&api?'附件发送时复制到项目 · ⌘ ↵':'⌘ ↵ 发送';
}
function addAttachments(files,d=draft()){
 const n=Math.max(0,16-d.files.length);d.files.push(...files.slice(0,n));
 for(const a of files.slice(n))api?.forget(a.id).catch(()=>{});
 if(d===draft()){
  if(n<files.length)notice('每条消息最多添加 16 个附件。');
  renderAttachments();
 }
}
async function pasteImage(e){
 // Desktop accepts actual paste events only. The offline preview does not read or store image bytes.
 if((!e.isTrusted&&!demo)||e.defaultPrevented||!e.clipboardData)return;
 const clipboard=e.clipboardData;
 if(clipboard.getData('text/plain'))return;
 const images=[...clipboard.files].filter(f=>f.type.startsWith('image/'));
 if(!images.length)return;
 const types={'image/png':'png','image/jpeg':'jpg','image/webp':'webp'};
 const image=images.find(f=>Object.hasOwn(types,f.type));
 if(!image){notice('无法粘贴这种图片。请使用 PNG、JPEG 或 WebP。');return;}
 e.preventDefault();
 const d=draft(),id=key();
 if(d.files.length>=16){notice('每条消息最多添加 16 个附件。');return}
 if(!image.size||image.size>16*1024*1024){notice('请选择不超过 16 MiB 的剪贴板图片。');return}
 if(d.pasting){notice('正在添加上一张图片，请稍候。');return}
 if(!demo&&!api?.pasteImage){notice('桌面桥接未启动，请重启 Orbite。');return}
 d.pasting=true;buttons();
 try{
  const file=demo?{id:crypto.randomUUID(),kind:'file',name:'剪贴板图片.'+types[image.type],size:image.size,clipboard:true}
   :await api.pasteImage(image.type,new Uint8Array(await image.arrayBuffer()));
  addAttachments([file],d);
 }catch(err){if(d===draft())notice(err);else runtime(id).error=plain(err?.message||err)}
 finally{d.pasting=false;if(d===draft())buttons()}
}
function parseDemo(files,dir){
 const names=new Set(),out=[];
 for(const f of files){const relative=f.webkitRelativePath?.split('/')[0],name=relative||f.name;if(names.has(name))continue;names.add(name);out.push({name,kind:dir||relative||f.type===''&&f.size===0?'folder':'file',size:relative?null:f.size,id:crypto.randomUUID()})}
 addAttachments(out)
}
function textParts(into,text){
 // Only inline code. Model output is never inserted as HTML.
 for(const s of String(text).split(/(`[^`\n]+`)/g))into.append(s.startsWith('`')&&s.endsWith('`')&&s.length>2?el('code',s.slice(1,-1)):document.createTextNode(s))
}
function toolDetails(tools,open=false){
 const panel=el('details',null,'tool-details');panel.open=open;
 const summary=el('summary');summary.append(icon('chevron'),el('span'));panel.append(summary,el('ol',null,'tool-details-list'));
 updateToolDetails(panel,tools);return panel
}
function updateToolDetails(panel,tools){
 panel.querySelector('summary span').textContent=tools.length+' 项操作';
 const rows=tools.map(t=>{
  const row=el('li',null,'tool-detail');
  const label=t.status==='completed'?'完成':t.status==='failed'?'失败':t.status==='in_progress'?'运行中':'等待';
  row.append(el('span',t.title||'工具调用','tool-name'),el('span',label,'tool-state '+(t.status||'')));
  return row
 });
 panel.querySelector('ol').replaceChildren(...rows);
}
function renderTurnTools(){
 const root=$('toolSummary'),s=runtime(),items=[...s.turnTools.values()],id=key(),panel=root.firstElementChild;
 if(!s.running||!items.length){root.replaceChildren();return}
 if(panel?.dataset.thread===id){updateToolDetails(panel,items);return}
 const next=toolDetails(items);next.dataset.thread=id;root.replaceChildren(next);
}
function appendRecent(map,id,value,limit){
 map.set(id,value);
 if(map.size<=limit)return false;
 map.delete(map.keys().next().value);return true
}
function queueActivity(s,id){
 if(id!==key()||s.activityFrame)return;
 s.activityFrame=requestAnimationFrame(()=>{
  s.activityFrame=0;if(id!==key()||view!=='thread')return;
  renderTurnTools();
  if(tab==='activity'&&$('body').dataset.review==='open')renderActivity();
 });
}
function message(m){
 const user=m.role==='user',root=el('article',null,'turn '+(user?'user-turn':'assistant-turn'));
 if(!user){const role=el('span',null,'role');role.append(el('i'),el('span','Antigravity'));root.append(role)}
 const body=el('div',null,user?'bubble':'');
 if(m.files?.length){const files=el('div',null,'chips');files.append(...m.files.map(f=>chip(f,false)));body.append(files)}
 const t=el('div',null,'text');textParts(t,m.text);body.append(t);root.append(body);
 if(m.tools?.length)root.append(toolDetails(m.tools));
 if(m.status){const strip=el('div',null,'action-strip');strip.append(icon('check'),el('span',m.status));root.append(strip)}
 return root;
}
function renderChat(){
 const ms=current?.messages??[];$('messages').replaceChildren(...ms.map(message));$('welcome').hidden=!!ms.length;
 renderTurnTools();renderLive();
}
function renderLive(){
 const live=$('live'),seen=new Set([...live.querySelectorAll('.approval-card')].map(n=>n.dataset.approval));live.replaceChildren();
 if(runtime().error)live.append(el('div',runtime().error,'error'));
 for(const p of runtime().approvals.values()){
  const card=el('div',null,'approval-card'),actions=el('div',null,'approval-actions');card.append(el('strong',p.title||'需要你的许可'),el('pre',p.text));
  for(const a of p.actions){
   const b=el('button',a.label,'secondary');const id=key();b.onclick=async()=>{
    try{
     if(api)await api.decide(id,p.key,a.id);
     runtime(id).approvals.delete(p.key);if(id===key())renderLive();
     if(demo){handle({task:id,type:'delta',text:a.kind==='allow_once'?'已完成示例检查。没有执行实际命令。':'已跳过示例操作。'});finish(id,'end_turn')}
    }catch(e){notice(e)}
   };actions.append(b)
  }card.dataset.approval=p.key;card.append(actions);live.append(card);if(!seen.has(p.key))motion.enter(card,'approval')
 }
}
function bottom(force=true){
 const chat=$('chat'),id=key(),ticket=++scrollTicket;
 if(!force&&chat.scrollHeight-chat.scrollTop-chat.clientHeight>80)return;
 requestAnimationFrame(()=>{if(ticket===scrollTicket&&id===key()&&view==='thread')chat.scrollTop=chat.scrollHeight});
}
function queueStream(s,id){
 if(s.flushFrame)return;
 s.flushFrame=requestAnimationFrame(()=>{
  s.flushFrame=0;if(id!==key()||view!=='thread'||!s.assistant)return;
  const c=$('chat'),follow=c.scrollHeight-c.scrollTop-c.clientHeight<90;
  const root=$('messages'),last=root.lastElementChild;
  if(last?.classList.contains('assistant-turn')){
   const text=last.querySelector('.text');text.replaceChildren();textParts(text,s.assistant.text);
  }else{
   const n=message(s.assistant);root.append(n);motion.enter(n);
  }
  if(follow)bottom(true)
 });
}

function render(){renderSidebar();renderChat();$('prompt').value=draft().text;renderAttachments();renderOptions();renderCommands();setStatus();buttons();bottom()}
function buttons(){
 const s=runtime(),locked=s.running||s.connecting||s.submitting;
 $('auth').disabled=locked;$('selectExe').disabled=locked;$('folder').disabled=s.connecting;$('new').disabled=s.connecting;$('send').disabled=runtime().connecting||(!demo&&!api);$('sendIcon').setAttribute('href',runtime().running?'#i-stop':'#i-up');
 $('send').setAttribute('aria-label',s.running?'追加指令':'发送');$('send').title=s.running?'追加指令（⌘↵）':'发送（⌘↵）';
 $('sendIcon').setAttribute('href','#i-up');
 $('send').disabled=s.connecting||s.steering||(!s.running&&s.submitting)||draft().pasting||(!demo&&!api);
 $('stop').hidden=!s.running;$('composer').classList.toggle('is-running',s.running);
 $('scopeMode').hidden=!folder;
 $('scopeMode').value=(current?.isolated??draft().isolated)?'worktree':'local';
 $('scopeMode').disabled=!!current||s.connecting;
 $('reveal').hidden=demo||!current?.isolated||!s.isolatedReady;
 $('exportPatch').hidden=demo||!current?.isolated||!s.isolatedReady;
 $('exportPatch').disabled=s.running||s.connecting;
 renderCua();
 document.querySelectorAll('#options select').forEach(n=>n.disabled=locked);
}
function clearEngineControls(id,s=runtime(id)){
 s.controlRevision=(s.controlRevision??0)+1;s.options=[];s.commands=[];
 if(id===key()){renderOptions();renderCommands()}
}
function renderOptions(){
 const root=$('options');root.replaceChildren();
 if(!runtime().options.length){root.append(el('span','Antigravity'));return}
 for(const a of runtime().options.slice(0,3)){
  const s=el('select');s.title=a.name;s.ariaLabel=a.name;
  if(!a.options.some(o=>o.value===a.currentValue)){const unknown=el('option','当前设置');unknown.value='';unknown.disabled=true;s.append(unknown)}
  for(const o of a.options){const n=el('option',o.name||o.value);n.value=o.value;s.append(n)}
  s.value=a.options.some(o=>o.value===a.currentValue)?a.currentValue:'';
  s.onchange=async()=>{
   const id=key(),st=runtime(id),revision=st.controlRevision;
   try{
    const options=api?await api.configure(id,a.id,s.value):st.options.map(x=>x.id===a.id?{...x,currentValue:s.value}:x);
    if(st.controlRevision!==revision)return;
    st.options=options;if(id===key())renderOptions();
   }catch(e){if(st.controlRevision===revision&&id===key()){notice(e);renderOptions()}}
  };
  root.append(s);
 }buttons()
}
function reviewPanel(show){
 const shell=$('reviewShell'),stage=$('body');if(!show&&shell.contains(document.activeElement))$('openDiff').focus();
 if(!motion.reduced&&stage.dataset.review!==(show?'open':'closed')){
  stage.dataset.reviewMotion='1';clearTimeout(reviewMotionTimer);
  reviewMotionTimer=setTimeout(()=>stage.removeAttribute('data-review-motion'),270);
 }
 stage.dataset.review=show?'open':'closed';shell.inert=!show;shell.setAttribute('aria-hidden',String(!show));
 $('openDiff').setAttribute('aria-expanded',String(show));if(show){if(tab==='diff')scanProject();else renderActivity()}
}
function renderActivity(){
 const r=$('activity'),s=runtime();r.replaceChildren();
 if(s.activityClipped)r.append(el('div','仅显示近期活动','ins-empty'));
 for(const e of s.turns.values()){const row=el('div',e.title||'工具调用','activity-row');if(e.status)row.append(el('small',({pending:'等待中',in_progress:'执行中',completed:'完成',failed:'失败'}[e.status]||e.status)));r.append(row)}
 for(const x of s.activity)r.append(el('div',x,'activity-row'));
 if(!r.childElementCount)r.append(el('div','暂无活动','ins-empty'));
 $('plan').replaceChildren(...s.plan.map(p=>el('div',(p.status==='completed'?'✓ ':p.status==='in_progress'?'◌ ':'○ ')+p.text,'activity-row')))
}
function agentPatch(name,c){
 const a=c.before.split('\n'),b=c.after.split('\n');
 let pre=0,post=0;
 while(pre<a.length&&pre<b.length&&a[pre]===b[pre])pre++;
 while(post<a.length-pre&&post<b.length-pre&&a[a.length-1-post]===b[b.length-1-post])post++;
 const begin=Math.max(0,pre-3),endA=a.length-post,endB=b.length-post;
 return ['diff --git a/'+name+' b/'+name,'--- a/'+name,'+++ b/'+name,
  '@@ 从第 '+(begin+1)+' 行开始',
  ...a.slice(begin,pre).map(t=>' '+t),
  ...a.slice(pre,endA).map(t=>'-'+t),
  ...b.slice(pre,endB).map(t=>'+'+t),
  ...a.slice(endA,endA+Math.min(post,3)).map(t=>' '+t)
 ].join('\n');
}
function renderReview(){
 const state=runtime(),mode=state.diffMode;
 $('reviewMode').value=mode;
 $('reviewMode').options[0].textContent=current?.isolated?'工作树 · 相对起点':'工作区状态';
 $('branchWrap').hidden=!state.review?.branch;$('branch').textContent=state.review?.branch??'';
 const review=mode==='task'?{status:[...state.taskDiff.keys()].map(p=>' M '+p).join('\n'),patch:[...state.taskDiff].map(([p,c])=>agentPatch(p,c)).join('\n'),error:'当前会话还没有收到工具变更。'}:state.review;
 const files=String(review?.status??'').split('\n').filter(x=>x.trim()).filter(x=>!x.slice(3).startsWith('.orbit-attachments/'));
 $('diffCount').hidden=!files.length;$('diffCount').textContent=files.length?String(files.length):'';
 const frame=$('changedFiles'),patch=$('patch');
 const focused=frame.contains(document.activeElement)?document.activeElement.dataset.path:undefined;
 frame.replaceChildren();patch.replaceChildren();
 if(!files.length){frame.append(el('div',review?.error||'没有变更','ins-empty'));if(focused!==undefined)$('refresh').focus();return}
 const chunks=(review?.patch??'').split(/(?=^diff --git )/m).filter(Boolean);
 const remembered=state.reviewSelection.get(mode);
 const selected=(files.find(f=>f.slice(3)===remembered)??files[0]).slice(3);
 state.reviewSelection.set(mode,selected);
 let activeRow,focusRow;
 for(const f of files){
  const path=f.slice(3),active=path===selected;
  const b=el('button',null,'file-row'+(active?' selected':''));
  b.dataset.path=path;
  if(active){b.setAttribute('aria-current','true');activeRow=b}
  if(path===focused)focusRow=b;
  b.append(icon('file'),el('span',path),el('code',f.slice(0,2).trim()));
  b.onclick=()=>{state.reviewSelection.set(mode,path);renderReview()};frame.append(b)
 }
 if(focused!==undefined)(focusRow??activeRow)?.focus({preventScroll:true});
 const name=selected.split(' -> ').at(-1);
 const found=chunks.find(x=>x.split('\n')[0].endsWith(' b/'+name)||x.split('\n').includes('+++ b/'+name));
 if(!found){patch.append(el('div','这个文件没有可显示的文本补丁。','ins-empty'));return}
 const item=el('div',null,'patch-file'),sc=el('div',null,'scroll'),head=el('div',null,'patch-title'),follow=el('button','反馈此文件','secondary');
 head.append(el('span',selected),follow);item.append(head,sc);
 follow.onclick=()=>{const d=draft();d.text+=(d.text.trim()?'\n\n':'')+'关于文件 '+JSON.stringify(selected)+'：';$('prompt').value=d.text;$('prompt').focus();$('prompt').setSelectionRange(d.text.length,d.text.length)};
 const lines=found.split('\n'),start=lines.findIndex(s=>s.startsWith('@@'));
 for(const ln of (start>=0?lines.slice(start):lines.slice(1)).slice(0,1600)){
  const type=ln[0]==='+'?' plus':ln[0]==='-'?' minus':ln.startsWith('@@')?' hunk':'';
  sc.append(el('div',ln||' ','patch-line'+type))
 }patch.append(item)
}
async function refresh(){
 const p=folder,id=key(),t=current,s=runtime(id),revision=++runtime(id).refreshSeq;
 if(!p||!authorized.has(p)||t?.isolated&&!s.isolatedReady){s.review=null;renderReview();return}
 let next;
 try{next=await api.diff(id,p,Boolean(t?.isolated))}
 catch(e){next={error:plain(e.message)}}
 if(revision!==s.refreshSeq)return;s.review=next;
 if(key()===id)renderReview();
}
function settings(){
 const s=runtime();
 $('accountText').textContent=demo?'预览不会连接 Google 账户。':s.authState?'由 Google 在默认浏览器处理认证。':'使用本机 Antigravity CLI 的 Google 会话。';
 $('auth').textContent=demo?'离线预览':s.authState?.available?'使用 Google 登录':s.session?'已连接':'连接';
 if(!$('settings').open)$('settings').showModal();
}
async function authorize(){
 const was=folder;if(folder&&authorized.has(folder))return true;
 const prev=key(),oldDraft=draft(),p=await pickProject(folder);
 if(!p)return false;
 if(current&&p!==was){notice('要恢复原会话，请重新选它所属的项目。');return false}
 if(p!==was){folder=p;if(!drafts.has(key()))drafts.set(key(),oldDraft);drafts.delete(prev);render()}
 return true;
}
function assigned(isolated=false){
 if(current)return current;
 const prev=key(),d=draft(),s=runtime();
 const text=d.text||'新建会话';
 current={id:crypto.randomUUID(),folder,title:[...text].slice(0,30).join(''),stamp:'刚刚',messages:[],isolated};
 data.threads.unshift(current);drafts.set(current.id,d);states.set(current.id,s);drafts.delete(prev);states.delete(prev);
 persist();render();return current;
}
async function connect(allowAuth=false){
 let s=runtime();if(s.session)return true;if(s.connecting||s.submitting||!demo&&!api)return false;
 s.connecting=true;buttons();setStatus('连接中…');s.error='';$('dialogStatus').textContent='';
 let id=key();
 try{
  if(!await authorize())return false;
  s.connecting=false;s=runtime();s.connecting=true;
  const t=assigned(draft().isolated);id=t.id;s=runtime(id);
  if(demo){s.session='demo';s.isolatedReady=!!t.isolated;setStatus('',id);return true}
  clearEngineControls(id,s);
  if(t.isolated&&!s.isolatedReady){setStatus('准备工作树…',id);await api.worktree(id,t.folder);s.isolatedReady=true}
  const c=allowAuth&&s.authState?.available?await api.authenticate(id):await api.connect(id,t.folder,t.remote??null,s.extensionEnabled,Boolean(t.isolated));
  if(c.auth){s.authState=c;settings();$('dialogStatus').textContent=c.available?'点「使用 Google 登录」继续。':'请先在终端运行 agy 完成 Google 登录，再点「连接」。';setStatus('待登录',id);return false}
  if(!c.session)throw new Error('连接未返回会话。');
  s.session=c.session;s.authState=null;t.remote=s.session;setStatus('',id);persist();return true;
 }catch(e){if(!demo)clearEngineControls(id,s);settings();notice(e);setStatus('待连接');return false}
 finally{s.connecting=false;buttons()}
}
function finish(id,status){
 const s=runtime(id),labels={end_turn:'',cancelled:'已停止',refusal:'请求未执行',disconnected:'连接中断',not_sent:'未发送，可重新连接后重试',max_tokens:'达到输出上限'};
 if(s.flushFrame){cancelAnimationFrame(s.flushFrame);s.flushFrame=0}
 if(s.activityFrame){cancelAnimationFrame(s.activityFrame);s.activityFrame=0}
 const t=data.threads.find(x=>x.id===id),ops=[...s.turnTools.values()];
 if(ops.length&&t){if(s.assistant)s.assistant.tools=ops;else t.messages.push({role:'assistant',text:'',tools:ops})}
 s.turnTools.clear();s.running=false;s.approvals.clear();s.steering=false;s.assistant=null;setStatus(labels[status]??'',id);
 if(id===key()){buttons();renderChat();scanProject();if(tab==='activity'&&$('body').dataset.review==='open')renderActivity()}
 renderSidebar();persist()
}
function handle(e){
 const id=e.task??key(),s=runtime(id),t=data.threads.find(x=>x.id===id),visible=id===key();
 if(e.type==='commands'){s.commands=e.commands;if(visible)renderCommands();return}
 if(e.type==='config'){s.options=e.options??[];if(visible)renderOptions();return}
 if(e.type==='offline'||e.type==='sleep'){
  s.session=null;s.authState=null;clearEngineControls(id,s);
  if(e.type==='offline'){s.error=e.text;s.offlineWhileRunning=s.running;finish(id,s.running?'disconnected':'cancelled')}else setStatus('',id);
  return;
 }
 if(!s.running||!t)return;
 if(e.type==='delta'){
  if(!s.assistant){s.assistant={role:'assistant',text:''};t.messages.push(s.assistant)}
  s.assistant.text+=e.text??'';scheduleSave();if(visible)queueStream(s,id);
 }else if(e.type==='approval'){s.approvals.set(e.key,e);setStatus('等待审批',id);renderSidebar();if(visible){renderLive();bottom()}}
 else if(e.type==='tool'){
  const prev=s.turns.get(e.id),op={title:e.title??prev?.title,status:e.status??prev?.status};
  if(s.turnTools.has(e.id)&&prev?.title===op.title&&prev?.status===op.status)return;
  if(appendRecent(s.turns,e.id,op,recentToolLimit))s.activityClipped=true;
  appendRecent(s.turnTools,e.id,op,turnToolLimit);queueActivity(s,id);
 }else if(e.type==='activity'){
  s.activity.push(e.text);
  if(s.activity.length>activityNoteLimit){s.activity.shift();s.activityClipped=true}
  queueActivity(s,id)
 }else if(e.type==='plan'){s.plan=e.entries;queueActivity(s,id)}
 else if(e.type==='taskDiff'){
  if(s.taskDiff.size>=32&&!s.taskDiff.has(e.path))return;
  const prev=s.taskDiff.get(e.path);s.taskDiff.set(e.path,{before:prev?.before??e.before,after:e.after});
  if(visible&&s.diffMode==='task')renderReview()
 }else if(e.type==='done')finish(id,e.status)
}
async function send(){
 let s=runtime();if(s.connecting||draft().pasting)return;
 if(s.running){await steer();return}
 if(s.submitting)return;
 const d=draft(),original=d.text,files=d.files.slice();
 if(files.length>16){notice('每条消息最多发送 16 个附件。请先移除多余附件。');return}
 const text=original.trim()||(files.length?'请查看这些附件。':'');if(!text)return;
 if(!await connect()||draft()!==d)return;
 // Connection may wait for a folder picker, worktree or CLI. Only send the draft the user confirmed.
 if(d.pasting||d.text!==original||d.files.length!==files.length||files.some((f,i)=>f!==d.files[i])){
  setStatus('草稿已更新，请确认后再发送。');return;
 }
 const t=current,id=t.id,local=d;s=runtime(id);
 local.text='';local.files=[];s.error='';s.approvals.clear();s.assistant=null;
 const user={role:'user',text,files:files.map(({name,kind,size,clipboard})=>({name,kind,size,clipboard}))};t.messages.push(user);
 s.submitting=!demo;
 s.offlineWhileRunning=false;s.turnTools.clear();s.running=true;setStatus(demo?'预览运行中':'正在执行',id);render();persist();
 try{
  if(demo){
   handle({task:id,type:'delta',text:'收到。这次会话是离线预览。你可以在运行中补一句，或者切走去看别的会话。'});
   s.demoTimer=setTimeout(()=>handle({task:id,type:'approval',key:'preview',title:'运行项目测试？',text:'swift test\n仅为界面演示',actions:[{id:'allow',kind:'allow_once',label:'允许一次'},{id:'deny',kind:'reject_once',label:'拒绝'}]}),300)
  }else {
   const result=await api.prompt(id,text,files.map(f=>f.id));
   if(result?.notSent||result?.uncertain)throw Object.assign(new Error(result.message),{notSent:result.notSent,uncertain:result.uncertain});
  }
 }catch(e){
  const disconnected=!e.notSent&&(s.offlineWhileRunning||e.uncertain);
  if(disconnected){user.status='连接中断 · 未确认执行结果';s.error=(s.error||plain(e.message))+' 没有自动重发。重试之前请检查审阅面板。';}
  else {
   if(e.notSent)t.messages=t.messages.filter(x=>x!==user);else if(t.messages.at(-1)===user)t.messages.pop();
   if(!local.text)local.text=original;local.files.unshift(...files);s.error=plain(e.message);
  }
  finish(id,disconnected?'disconnected':e.notSent?'not_sent':'failed');if(key()===id)render();
 }finally{s.submitting=false;if(key()===id)buttons()}
}
async function steer(){
 const id=key(),s=runtime(id),t=current,d=draft(),text=d.text.trim();
 if(!s.running||!text||s.steering)return;
 if(d.files.length){notice('运行中只补充文字。附件会留在草稿里，等下一轮再送。');return}
 s.steering=true;s.assistant=null;d.text='';const m={role:'user',text,status:'追加指令'};t.messages.push(m);render();persist();
 try{
  if(demo){handle({task:id,type:'delta',text:'（预览）已收到你的追加指令。审批仍可在本会话里处理。'})}
  else {
   const result=await api.steer(id,text);
   if(result?.uncertain)throw Object.assign(new Error(result.message),{uncertain:true});
  }
 }catch(e){
  if(e.uncertain||s.offlineWhileRunning){m.status='连接中断 · 未确认追加结果';s.error=plain(e.message)+' 没有自动重发。'}
  else {t.messages=t.messages.filter(x=>x!==m);if(!d.text)d.text=text;s.error=plain(e.message)}
  persist();
 }finally{s.steering=false;if(key()===id)render()}
}
async function stop(){
 const id=key(),s=runtime(id);if(!s.running)return;
 try{if(demo){clearTimeout(s.demoTimer);finish(id,'cancelled')}else await api.stop(id)}catch(e){notice(e)}
}
let commandIndex=-1,dismissedCommand=null;
$('commands').setAttribute('role','listbox');
$('prompt').setAttribute('aria-controls','commands');
function selectCommand(index){
 const menu=$('commands'),prompt=$('prompt');
 commandIndex=index;
 for(const [i,option] of [...menu.children].entries())option.setAttribute('aria-selected',String(i===index));
 if(index<0)prompt.removeAttribute('aria-activedescendant');
 else prompt.setAttribute('aria-activedescendant',menu.children[index].id);
}
function renderCommands(){
 const value=$('prompt').value,menu=$('commands');
 if(dismissedCommand&&(dismissedCommand.id!==key()||dismissedCommand.value!==value))dismissedCommand=null;
 const token=!dismissedCommand&&value.match(/^\/([\w-]*)$/);
 const commands=token?runtime().commands.filter(x=>x.name.startsWith(token[1])).slice(0,7):[];
 menu.hidden=!commands.length;menu.replaceChildren();selectCommand(-1);
 for(const [i,c] of commands.entries()){
  const b=el('button');b.id='command-choice-'+i;b.setAttribute('role','option');b.setAttribute('aria-selected','false');
  b.append(el('code','/'+c.name),el('span',c.description));
  b.onclick=()=>{
   const prompt=$('prompt');draft().text='/'+c.name+' ';prompt.value=draft().text;renderCommands();
   prompt.focus();prompt.setSelectionRange(prompt.value.length,prompt.value.length);
  };menu.append(b)
 }
}
function attachMenu(open){$('attachChoices').hidden=!open;$('attachmentMenu').setAttribute('aria-expanded',String(open))}
async function attach(kind){
 attachMenu(false);const d=draft();
 try{if(demo)$(kind==='folder'?'demoFolder':'demoFiles').click();else if(api)addAttachments(await api.attach(kind),d)}
 catch(e){notice(e)}
}
function theme(s){
 const actual=s==='system'?(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light'):s;
 document.documentElement.dataset.theme=actual;$('theme').value=s;
 try{localStorage.setItem(storage+'-theme',s)}catch{}
}
let initialTheme='system';try{initialTheme=localStorage.getItem(storage+'-theme')||'system'}catch{}
theme(initialTheme);matchMedia('(prefers-color-scheme: dark)').addEventListener('change',()=>{if($('theme').value==='system')theme('system')});
$('theme').onchange=()=>theme($('theme').value);
function glass(on){document.documentElement.dataset.glass=on?'on':'off';$('glassToggle').checked=on;try{localStorage.setItem(storage+'-glass',on?'on':'off')}catch{}}
let isGlass=true;try{isGlass=localStorage.getItem(storage+'-glass')!=='off'}catch{}
glass(isGlass);$('glassToggle').onchange=()=>glass($('glassToggle').checked);
$('demoFlag').hidden=!demo;
$('prompt').oninput=()=>{draft().text=$('prompt').value;renderCommands()};
$('new').onclick=()=>go(null,folder,null,true);$('showHub').onclick=showHub;
$('folder').onclick=$('emptyPick').onclick=$('hubOpen').onclick=async()=>{const p=await pickProject(null);if(p)go(null,p,null,true)};
$('collapse').onclick=()=>sidebar(false);$('expand').onclick=()=>sidebar(true);
$('searchBtn').onclick=()=>{if(!$('searchWrap').hidden){$('searchWrap').hidden=true;$('search').value='';renderSidebar()}else opensSidebar()};
$('search').oninput=renderSidebar;
$('openDiff').onclick=()=>reviewPanel($('body').dataset.review!=='open');
$('closeDiff').onclick=()=>reviewPanel(false);$('refresh').onclick=scanProject;
$('reviewMode').onchange=()=>{runtime().diffMode=$('reviewMode').value;renderReview()};
$('reveal').onclick=async()=>{try{await api.reveal(key())}catch(e){notice(e)}};
$('exportPatch').onclick=async()=>{
 const id=key(),root=folder;
 try{
  const r=await api.exportPatch(id,root);if(key()!==id||r.canceled)return;
  const skip=r.untracked?'；不含 '+r.untracked+' 个未跟踪文件':'';
  if(r.ok)setStatus('已导出：'+r.file+skip,id);
  else if(r.empty)notice('没有可导出的已跟踪改动'+skip);
 }catch(e){if(key()===id)notice(e)}
};
document.querySelectorAll('[data-tab]').forEach(b=>b.onclick=()=>{
 tab=b.dataset.tab;document.querySelectorAll('[data-tab]').forEach(n=>n.classList.toggle('selected',n===b));
 document.querySelectorAll('[data-panel]').forEach(n=>n.hidden=n.dataset.panel!==tab);
 if(tab==='diff')scanProject();else renderActivity();motion.enter(document.querySelector('[data-panel='+tab+']'))
});
$('setup').onclick=settings;
$('auth').onclick=async()=>{if(demo){closeDlg('settings');return}if(await connect(true))closeDlg('settings')};
$('selectExe').onclick=async()=>{
 const id=key(),s=runtime(id);
 try{if(api&&await api.executable()){
  $('cliText').textContent='已选用本机 agy。';s.session=null;s.authState=null;clearEngineControls(id,s);
  if(id===key()){setStatus();settings()}
 }}catch(e){if(id===key())notice(e)}
};
function renderCua(){
 const s=runtime(),locked=s.running||s.connecting||cuaWorking;
 $('cuaProbe').disabled=locked;$('chooseDriver').disabled=locked;
 $('cuaGrant').disabled=locked||!cuaStatus?.mac||!driverSelected;
 $('cuaTest').disabled=locked||!cuaStatus?.mac||!driverSelected||cuaStatus?.permissions?.accessibility!==true||cuaStatus?.permissions?.screen!==true;
 $('useDriver').disabled=locked||(!s.extensionEnabled&&(!folder||!driverSelected||!cuaStatus?.mcp||Boolean(cuaStatus.mcp.missing.length)||!cuaStatus.mac||cuaStatus.permissions?.accessibility!==true||cuaStatus.permissions?.screen!==true));
 $('useDriver').checked=s.extensionEnabled;$('cuaDraft').disabled=locked||!folder||!s.extensionEnabled;
 $('cuaDriverState').textContent=cuaStatus?(cuaStatus.mcp?('v'+cuaStatus.version+' · '+cuaStatus.mcp.tools+' tools'):(cuaStatus.found?'MCP 未就绪':'未安装')):'未检测';
 const perms=cuaStatus?.permissions;
 const mark=x=>x==null?'未知':x?'已开':'未开';
 $('cuaPermState').textContent=perms?mark(perms.accessibility)+' / '+mark(perms.screen):cuaStatus&&!cuaStatus.mac?'需要 Mac':'未检测';
 $('cuaTestState').textContent=cuaTestResult?cuaTestResult.ok?'自测通过':'未通过':'未自测';
}
async function probing(){
 if(cuaWorking)return;
 if(demo||!api){$('extNote').textContent='离线预览不会检测或操作本机。请在桌面版打开。';return}
 cuaWorking=true;renderCua();$('extensionStatus').textContent='正在检测…';
 try{
  cuaStatus=await api.cuaProbe();driverSelected=Boolean(cuaStatus.found);
  $('extensionStatus').textContent=cuaStatus.mcp?'检测到 Cua Driver':cuaStatus.found?'需要检查驱动':'没有检测到驱动';
  $('extNote').textContent=(cuaStatus.issues??[]).join('\n')||(cuaStatus.permissions?.source?'权限报告来自：'+cuaStatus.permissions.source:'');
 }catch(e){$('extensionStatus').textContent='检测未完成';notice(e)}
 finally{cuaWorking=false;renderCua()}
}
$('extensions').onclick=()=>{$('extensionDialog').showModal();if(!folder&&!demo)$('extNote').textContent='可先检测本机；接入会话前请打开一个项目。';renderCua()};
$('cuaProbe').onclick=probing;
$('cuaDocs').onclick=async()=>{if(demo||!api){$('extNote').textContent='安装文档：https://cua.ai/docs/cua-driver/quickstart';return}try{await api.cuaDocs()}catch(e){notice(e)}};
$('chooseDriver').onclick=async()=>{
 try{if(demo){$('extNote').textContent='仅桌面版可以选择本机扩展。';return}
 if(await api.computerUse()){cuaTestResult=null;await probing()}
 }catch(e){notice(e)}
};
$('cuaGrant').onclick=async()=>{
 if(cuaWorking)return;
 cuaWorking=true;renderCua();$('extNote').textContent='请在 macOS 的授权窗口确认。';
 try{const r=await api.cuaGrant();$('extNote').textContent=r.text}
 catch(e){notice(e)}
 finally{cuaWorking=false;renderCua()}
};
$('cuaTest').onclick=async()=>{
 if(cuaWorking)return;
 cuaWorking=true;renderCua();$('extNote').textContent='正在运行官方后台自测…';
 try{cuaTestResult=await api.cuaTest();$('extNote').textContent=cuaTestResult.text}
 catch(e){notice(e)}
 finally{cuaWorking=false;renderCua()}
};
$('useDriver').onchange=async()=>{
 const id=key(),s=runtime(id),enabled=$('useDriver').checked;
 try{if(api&&s.session)await api.disconnect(id)}catch(e){notice(e);renderCua();return}
 s.extensionEnabled=enabled;s.session=null;s.authState=null;clearEngineControls(id,s);
 if(key()===id){setStatus();renderCua();$('extNote').textContent=enabled?'将在该会话下一次连接时加载。再用下面的联调提示检查 Antigravity 能否看到它。':'下一次连接不再由 Orbite 加载，CLI 全局设置不变。'}
};
$('cuaDraft').onclick=async()=>{
 if(!runtime().extensionEnabled)return;
 closeDlg('extensionDialog');
 if(view==='home')await go(current,folder,null,true);
 const d=draft();d.text+=(d.text.trim()?'\n\n':'')+'只调用 cua-driver MCP 的 check_permissions，传入 prompt:false。只报告权限状态，不截图、不点击、不授权；如果 MCP 不可用，明确说明，不用终端替代。';
 $('prompt').value=d.text;$('prompt').focus({preventScroll:true});
};
document.querySelectorAll('[data-dismiss]').forEach(b=>b.onclick=()=>closeDlg(b.dataset.dismiss));
$('prompt').addEventListener('paste',pasteImage);
$('attachmentMenu').onclick=()=>attachMenu($('attachChoices').hidden);
$('addFiles').onclick=()=>attach('file');$('addFolder').onclick=()=>attach('folder');
$('demoFiles').onchange=e=>{parseDemo(e.target.files,false);e.target.value=''};
$('demoFolder').onchange=e=>{parseDemo(e.target.files,true);e.target.value=''};
document.addEventListener('click',e=>{if(!e.target.closest('.attach-control'))attachMenu(false)});
function fromFiles(e){return [...e.dataTransfer?.types??[]].includes('Files')}
function dragState(on){$('dropBanner').hidden=!on;$('composer').classList.toggle('dragging',on)}
document.addEventListener('dragover',e=>{if(!fromFiles(e))return;e.preventDefault();e.dataTransfer.dropEffect='copy';dragState(true)});
document.addEventListener('dragleave',e=>{if(!e.relatedTarget)dragState(false)});
document.addEventListener('drop',e=>{e.preventDefault();dragState(false);if(demo&&e.dataTransfer?.files.length)parseDemo(e.dataTransfer.files,false)});
$('send').onclick=send;$('stop').onclick=stop;$('scopeMode').onchange=()=>{draft().isolated=$('scopeMode').value==='worktree';$('scopeMode').title=draft().isolated?'从当前提交创建隔离工作树，不包含未提交改动':'直接在项目目录工作'};api?.onDrop(()=>{
 const token=crypto.randomUUID();incomingDrops.set(token,{d:draft(),id:key()});return token;
},(files,token)=>{
 const origin=incomingDrops.get(token);incomingDrops.delete(token);
 if(origin)addAttachments(files,origin.d);else for(const f of files)api.forget(f.id).catch(()=>{});
},(err,token)=>{
 const origin=incomingDrops.get(token);incomingDrops.delete(token);
 if(origin?.d===draft())notice(err);else if(origin)runtime(origin.id).error=plain(err);
});api?.onEvent(handle);
document.addEventListener('keydown',e=>{
 if(e.isComposing||e.keyCode===229||e.defaultPrevented)return;
 const menu=$('commands');
 if(e.target===$('prompt')&&!menu.hidden&&!e.metaKey&&!e.ctrlKey&&!e.altKey&&!e.shiftKey){
  const count=menu.childElementCount;
  if(count&&(e.key==='ArrowDown'||e.key==='ArrowUp')){
   e.preventDefault();
   const step=e.key==='ArrowDown'?1:-1;
   selectCommand(commandIndex<0?(step>0?0:count-1):(commandIndex+step+count)%count);
   return;
  }
  if(e.key==='Enter'&&commandIndex>=0){e.preventDefault();menu.children[commandIndex].click();return}
 }
 if((e.metaKey||e.ctrlKey)&&e.key==='1'){e.preventDefault();showHub()}
 if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='k'){e.preventDefault();opensSidebar()}
 if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='n'&&!runtime().connecting){e.preventDefault();$('new').click()}
 if((e.metaKey||e.ctrlKey)&&e.key==='Enter'&&!document.querySelector('dialog[open]')){e.preventDefault();send()}
 if(e.key==='Escape'){
  if(!$('attachChoices').hidden)attachMenu(false);
  if(!menu.hidden){dismissedCommand={id:key(),value:$('prompt').value};renderCommands()}
 }
});
if(innerWidth<=640)sidebar(false);
viewLayout('home');render();scanProject();renderActivity();setStatus();
if(!demo&&!api)notice('桌面桥接未启动，请重启 Orbite。');
