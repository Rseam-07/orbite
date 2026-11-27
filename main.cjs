const {app,BrowserWindow,ipcMain,dialog,nativeTheme,shell}=require('electron');
const fs=require('node:fs');const path=require('node:path');const os=require('node:os');const {worktree,review,validId,exportPatch}=require('./workspace.cjs');
const cua=require('./computer-use.cjs');
const {Engine}=require('./core.cjs');
const {registerPaths,stage,clipboardImage}=require('./attachments.cjs');
const {randomUUID}=require('node:crypto');
const attachments=new Map();
let draftImageBytes=0;
function forgetAttachment(id){
 const image=attachments.get(id);
 if(image?.clipboard)draftImageBytes-=image.size;
 attachments.delete(id);
}
let win,cliPath,computerUsePath,cuaBusy=false;const roots=new Set(),runners=new Map(),scopes=new Map(),creating=new Set();
function exe(){
 const n='agy';
 const dirs=['/opt/homebrew/bin','/usr/local/bin','/usr/bin',path.join(os.homedir(),'.local/bin'),path.join(os.homedir(),'.antigravity/bin'),...(process.env.PATH??'').split(path.delimiter)];
 for(const x of [cliPath,...dirs.filter(p=>path.isAbsolute(p)).map(p=>path.join(p,n))].filter(Boolean)){
  try{if(fs.statSync(x).isFile()){fs.accessSync(x,fs.constants.X_OK);return x}}catch{}
 }
 throw new Error('未找到 '+n+'。请安装官方 CLI，或在这里选取它的可执行文件。');
}
function ipc(name,run){ipcMain.handle(name,(e,...args)=>{if(e.senderFrame!==e.sender.mainFrame||e.sender!==win?.webContents)throw new Error('Invalid sender');return run(...args)})}
function send(task,event){if(win&&!win.isDestroyed())win.webContents.send('engine-event',{...event,task})}
function close(task,notify=false){
 const r=runners.get(task);if(!r)return;runners.delete(task);cancelPreparation(r);r.close();
 if(notify)send(task,{type:'sleep'});
}
function get(task){if(!validId(task)||!runners.has(task))throw new Error('这个会话尚未连接');return runners.get(task)}
function busy(r){return r&&(r.active||r.preparing||r.connecting)}
function limit(){
 if(runners.size<8)return;
 for(const [id,r] of runners)if(!busy(r)){close(id,true);return}
 throw new Error('已达到 8 个并行会话的上限。');
}
ipc('pick',async suggestion=>{
 const x=await dialog.showOpenDialog(win,{properties:['openDirectory'],defaultPath:typeof suggestion==='string'&&path.isAbsolute(suggestion)?suggestion:undefined});if(x.canceled)return null;
 const cwd=fs.realpathSync(x.filePaths[0]);if([path.parse(cwd).root,os.homedir()].includes(cwd))throw new Error('请选择项目目录，而不是整个磁盘或主目录。');
 roots.add(cwd);return cwd;
});
async function pickExecutable(allowed,title){
 const x=await dialog.showOpenDialog(win,{title,properties:['openFile']});if(x.canceled)return null;
 if(!allowed.includes(path.basename(x.filePaths[0])))throw new Error('请选择 '+allowed.join(' 或 ')+' 可执行文件。');
 const f=fs.realpathSync(x.filePaths[0]);if(!fs.statSync(f).isFile())throw new Error('不是普通文件');fs.accessSync(f,fs.constants.X_OK);return f;
}
ipc('executable',async()=>{const p=await pickExecutable(['agy'],'选择官方 agy 可执行文件');if(p)cliPath=p;return p?path.basename(p):null});
ipc('computer-use',async()=>{const p=await pickExecutable(['cua-driver'],'选择已安装的 cua-driver');if(p)computerUsePath=p;return Boolean(p)});
function driver(){return cua.executable(computerUsePath??'')??(computerUsePath=cua.findDriver())}
function guardCua(){
 if(cuaBusy)throw new Error('Computer Use 正在检查或自测。');
 if([...runners.values()].some(r=>r.extensions?.length&&(r.active||r.preparing)))throw new Error('先等正在使用桌面控制的会话完成。');
}
async function localCua(fn){
 guardCua();cuaBusy=true;try{return await fn(driver())}finally{cuaBusy=false}
}
ipc('cua-local-probe',()=>localCua(file=>cua.diagnose(file)));
ipc('cua-local-test',()=>localCua(file=>{if(!file)throw new Error('没有找到 Cua Driver。');return cua.selfTest(file)}));
ipc('cua-local-grant',()=>localCua(file=>{if(!file)throw new Error('没有找到 Cua Driver。');return cua.grant(file)}));
ipc('cua-docs',()=>shell.openExternal('https://cua.ai/docs/cua-driver/quickstart'));
ipc('disconnect',task=>{if(busy(runners.get(task)))throw new Error('请先停止当前回合。');close(task)});
ipc('worktree',async(task,root)=>{
 if(!roots.has(root)||!validId(task))throw new Error('请先通过文件选择器授权项目。');
 if(scopes.has(task))return scopes.get(task);
 if(creating.has(task))throw new Error('正在准备工作树。');
 creating.add(task);
 try{
  const homes=path.join(app.getPath('userData'),'worktrees');await fs.promises.mkdir(homes,{mode:0o700,recursive:true});
  const w=await worktree(root,task,homes);scopes.set(task,{...w,root});
  return {branch:'orbit/'+task.slice(0,8)};
 }finally{creating.delete(task)}
});
ipc('connect',async(task,root,id,enableExtension,isolated)=>{
 if(!validId(task)||!roots.has(root))throw new Error('请先用「打开项目」授权目录。');
 if(busy(runners.get(task)))throw new Error('当前会话正在运行。');
 const scope=isolated?scopes.get(task):{root,dir:root};
 if(!scope||scope.root!==root)throw new Error('请先准备工作树。');
 if(enableExtension&&!driver())throw new Error('请先在扩展中选取已安装的 Cua Driver。');
 close(task);limit();
 const mcp=enableExtension?[{name:'cua-driver',command:computerUsePath,args:['mcp'],env:[]}]:[];
 const runner=new Engine({exe:exe(),cwd:scope.dir,emit:e=>{
  if(runners.get(task)!==runner)return;
  if(e.type==='offline')cancelPreparation(runner);
  send(task,e);
 }});
 runners.set(task,runner);runner.connecting=true;
 try{
  await runner.init();if(runners.get(task)!==runner)throw new Error('连接已退出');
  runner.requestedSession=id??null;runner.extensions=mcp;
  try{return {session:await runner.open(runner.requestedSession,mcp)}}
  catch(e){
   // ACP v1 auth_required is a protocol code; the agent may localize the message.
   if(e.code===-32000&&runners.get(task)===runner){
    runner.awaitingAuth=true;
    return {auth:true,available:runner.canAuthenticate};
   }
   throw e;
  }
 }catch(e){
  if(runners.get(task)===runner)close(task);
  throw e;
 }finally{runner.connecting=false}
});
ipc('authenticate',async task=>{
 const r=get(task);
 if(busy(r))throw new Error('当前会话正在连接或运行，请稍候。');
 if(!r.awaitingAuth)throw new Error('此会话没有待处理的登录请求。');
 if(!r.canAuthenticate)throw new Error('请在终端运行 agy 完成 Google 登录，再回到 Orbite。');
 r.connecting=true;
 try{
  await r.auth();if(r!==runners.get(task))throw new Error('连接已切换');
  const session=await r.open(r.requestedSession,r.extensions);
  r.awaitingAuth=false;
  return {session};
 }finally{r.connecting=false}
});
ipc('configure',(task,id,value)=>get(task).configure(id,value));
ipc('attach',(kind)=>{if(!['file','folder'].includes(kind))throw new Error('未知附件类型');
 return dialog.showOpenDialog(win,{title:'添加到下一条消息',properties:[kind==='file'?'openFile':'openDirectory','multiSelections']}).then(a=>a.canceled?[]:registerPaths(a.filePaths,attachments));
});
ipc('drop',files=>{if(!Array.isArray(files))throw new Error('无效拖拽内容');return registerPaths(files,attachments)});
ipc('paste-image',(mime,bytes)=>{
 const image=clipboardImage(mime,bytes);
 if(draftImageBytes+image.size>64*1024*1024)throw new Error('草稿图片已达 64 MiB。请先发送或移除一些图片。');
 const {name,kind,size,clipboard}=image,id=randomUUID();
 attachments.set(id,image);draftImageBytes+=image.size;
 return {name,kind,size,clipboard,id};
});
ipc('forget',forgetAttachment);
const disconnectedBeforeSend={notSent:true,message:'连接在发送前中断，消息和附件均未发送。'};
function cancelPreparation(r){r.preparing?.abort(new Error('已取消发送；本次附件未发送。'))}
ipc('prompt',async(task,text,ids)=>{
 if(typeof text!=='string'||!text.trim()||text.length>100000)throw new Error('输入为空或超过长度限制');
 if(!Array.isArray(ids)||ids.length>16||ids.some(i=>!attachments.has(i)))throw new Error('附件已失效，请重新选取。');
 const r=get(task);if(r.rpc.closed)return disconnectedBeforeSend;
 if(r.active||r.preparing)throw new Error('当前回合仍在运行');
 if([...runners.values()].some(other=>other!==r&&(other.active||other.preparing)&&(other.cwd===r.cwd||other.cwd.startsWith(r.cwd+path.sep)||r.cwd.startsWith(other.cwd+path.sep))))throw new Error('当前目录正在被另一会话使用。可用独立工作树并行运行。');if(r.extensions.length&&(cuaBusy||[...runners.values()].some(x=>x!==r&&x.extensions?.length&&(x.active||x.preparing))))throw new Error('另一会话正在使用桌面控制。请等它完成再运行。');
 const preparation=new AbortController();r.preparing=preparation;
 try{
  let files;
  try{files=await stage(r.cwd,ids.map(i=>attachments.get(i)),{signal:preparation.signal})}
  catch(e){if(r.rpc.closed)return disconnectedBeforeSend;throw e}
  for(const f of files)if(f.skipped.length)r.emit({type:'activity',text:'附件 '+f.name+'：跳过 '+f.skipped.length+' 项（符号链接、依赖目录或常见凭据文件）'});
  const turn=r.prompt(text,files);r.preparing=null;
  try{await turn}catch(e){
   if(r.rpc.closed){ids.forEach(forgetAttachment);return {uncertain:true,message:e.message};}
   throw e;
  }
  ids.forEach(forgetAttachment);
  return {ok:true};
 }finally{if(r.preparing===preparation)r.preparing=null}
});
ipc('steer',async(task,text)=>{
 if(typeof text!=='string'||!text.trim()||text.length>100000)throw new Error('补充内容为空或过长');
 const r=get(task);
 try{return await r.steer(text)}catch(e){if(r.rpc.closed)return {uncertain:true,message:e.message};throw e}
});
ipc('stop',task=>{const r=get(task);if(r.preparing)return cancelPreparation(r);return r.cancel()});
ipc('decide',(task,key,choice)=>get(task).decide(key,choice));
ipc('reveal',async task=>{
 const s=scopes.get(task);if(!s)throw new Error('工作树尚未准备。');
 shell.showItemInFolder(s.dir);
});
ipc('export-patch',async(task,root)=>{
 const scope=scopes.get(task);
 if(!roots.has(root)||!scope||scope.root!==root)throw new Error('只有已准备的独立工作树可以导出补丁。');
 if(busy(runners.get(task)))throw new Error('请等这条会话结束，再导出。');
 const result=await exportPatch(scope.dir,scope.base);
 if(!result.patch)return {empty:true,untracked:result.untracked};
 const pick=await dialog.showSaveDialog(win,{title:'导出工作树补丁',defaultPath:'Orbite-'+task.slice(0,8)+'.patch',filters:[{name:'Git patch',extensions:['patch']}]});
 if(pick.canceled||!pick.filePath)return {canceled:true};
 await fs.promises.writeFile(pick.filePath,result.patch,{mode:0o600});
 return {ok:true,file:path.basename(pick.filePath),untracked:result.untracked};
});
ipc('diff',async(task,root,isolated)=>{
 if(!roots.has(root))throw new Error('项目尚未授权');
 const scope=isolated?scopes.get(task):{root,dir:root};
 if(!scope||scope.root!==root)throw new Error('工作树尚未准备');
 return review(scope.dir,scope.base);
});
// Keep the same profile across the Orbit → Orbite rename.
const profileDir=path.join(app.getPath('appData'),'orbit-antigravity');
fs.mkdirSync(profileDir,{recursive:true,mode:0o700});
app.setPath('userData',profileDir);
app.setPath('sessionData',profileDir);
app.setName('Orbite');
if(!app.requestSingleInstanceLock())app.quit();else{
 app.on('second-instance',()=>{win?.show();win?.focus()});
 app.whenReady().then(()=>{
  win=new BrowserWindow({width:1320,height:850,minWidth:770,minHeight:540,backgroundColor:nativeTheme.shouldUseDarkColors?'#191a1c':'#ffffff',title:'Orbite',
   ...(process.platform==='darwin'?{titleBarStyle:'hiddenInset',trafficLightPosition:{x:16,y:20}}:{}),
   webPreferences:{preload:path.join(__dirname,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true,webSecurity:true}});
  win.webContents.setWindowOpenHandler(()=>({action:'deny'}));
  win.webContents.on('will-navigate',e=>e.preventDefault());
  win.loadFile('index.html',{query:{desktop:'1'}});
 });
 app.on('window-all-closed',()=>{attachments.clear();draftImageBytes=0;for(const task of runners.keys())close(task);app.quit()});
}
