const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {createRequire}=require('node:module'),{runInNewContext}=require('node:vm');

// Run the actual preload and main attachment handlers with Electron's shell mocked.
async function desktop(t,{Engine}={}){
 const tmp=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-ipc-'));
 t.after(()=>fs.rm(tmp,{recursive:true,force:true}));
 const handlers=new Map(),listeners=new Map(),calls=[],events=[];
 const frame={},sender={mainFrame:frame,setWindowOpenHandler(){},on(){},send(_channel,data){events.push(data)}};
 let api,pick={canceled:true};
 const electron={
  app:{getPath:()=>tmp,setPath(){},setName(){},on(){},requestSingleInstanceLock:()=>true,whenReady:async()=>{}},
  BrowserWindow:class{webContents=sender;loadFile(){}isDestroyed(){return false}},
  nativeTheme:{},dialog:{showOpenDialog:async()=>pick},
  ipcMain:{handle:(name,fn)=>handlers.set(name,fn)},
  ipcRenderer:{invoke:async(name,...args)=>{calls.push(name);return handlers.get(name)({sender,senderFrame:frame},...args)}},
  contextBridge:{exposeInMainWorld:(name,value)=>{assert.equal(name,'orbit');api=value}},
  webUtils:{getPathForFile:file=>file.selectedPath||''}
 };
 async function load(file){
  const filename=path.join(__dirname,'..',file),req=createRequire(filename);
  runInNewContext(await fs.readFile(filename,'utf8'),{
   require:name=>name==='electron'?electron:name==='./core.cjs'&&Engine?{Engine}:req(name),__dirname:path.dirname(filename),process,AbortController,
   window:{addEventListener:(type,fn)=>listeners.set(type,fn)}
  },{filename});
 }
 await load('main.cjs');await load('preload.cjs');
 return {api,tmp,calls,events,drop:()=>listeners.get('drop'),setPick:value=>{pick=value}};
}

test('desktop picker cancels cleanly and reports a vanished file as a selection error',async t=>{
 const {api,tmp,setPick}=await desktop(t);
 assert.equal((await api.attach('file')).length,0);
 const file=path.join(tmp,'文档.txt'),missing=path.join(tmp,'已移动.txt');
 await fs.writeFile(file,'hello');
 setPick({filePaths:[file,missing]});
 await assert.rejects(api.attach('file'),e=>e.message.includes('已移动.txt')&&!e.message.includes(tmp));
 setPick({filePaths:[file]});
 const [attachment]=await api.attach('file');
 assert.equal(attachment.name,'文档.txt');
 assert.equal(attachment.kind,'file');assert.equal(attachment.size,5);
 assert.equal(Object.hasOwn(attachment,'path'),false);
});

test('desktop drop uses platform file paths and preserves origin tokens on success and error',{timeout:3000},async t=>{
 const {api,tmp,drop,calls}=await desktop(t);
 const folder=path.join(tmp,'项目'),missing=path.join(tmp,'missing');
 await fs.mkdir(folder);
 const answers=[];let token=0,done;
 const completed=new Promise(resolve=>{done=resolve});
 const record=answer=>{answers.push(answer);if(answers.length===2)done()};
 api.onDrop(()=>++token,(files,id)=>record({id,file:files[0].name,kind:files[0].kind}),(error,id)=>record({id,error}));
 function emit(isTrusted,...files){drop()({isTrusted,dataTransfer:{files},preventDefault(){}})}
 emit(false,{selectedPath:folder});emit(true,{name:'pretend.txt'});
 assert.deepEqual(calls,[]);assert.equal(token,0);
 emit(true,{selectedPath:missing});emit(true,{selectedPath:folder});
 // File validation is asynchronous; wait for both callbacks without assuming their order.
 await completed;
 assert.deepEqual(calls,['drop','drop']);
 assert.equal(answers.length,2);
 assert.match(answers.find(a=>a.id===1)?.error,/missing/);
 assert.deepEqual(answers.find(a=>a.id===2),{id:2,file:'项目',kind:'folder'});
});

test('desktop Stop during attachment copy restores eligibility to send the same draft',async t=>{
 const prompts=[];
 class Engine{
  constructor(options){Object.assign(this,options);this.rpc={closed:false}}
  async init(){}
  async open(){return 'fixture-session'}
  async prompt(text,files){prompts.push({text,files})}
 }
 const {api,tmp,setPick}=await desktop(t,{Engine});
 const executable=path.join(tmp,process.platform==='win32'?'agy.exe':'agy'),project=path.join(tmp,'project'),file=path.join(tmp,'draft.txt');
 await Promise.all([fs.writeFile(executable,'fixture (not executed)',{mode:0o700}),fs.mkdir(project),fs.writeFile(file,'draft')]);
 setPick({filePaths:[executable]});await api.executable();
 setPick({filePaths:[project]});await api.pick();
 await api.connect('example',project,null,false,false);
 setPick({filePaths:[file]});const [{id}]=await api.attach('file');
 const base=path.join(project,'.orbit-attachments'),writeFile=fs.writeFile;let cancel=true;
 t.mock.method(fs,'writeFile',async(dest,...args)=>{
  const result=await writeFile(dest,...args);
  if(cancel&&dest.startsWith(base+path.sep)){cancel=false;await api.stop('example')}
  return result;
 });
 await assert.rejects(api.prompt('example','work',[id]),/本次附件未发送/);
 assert.equal(prompts.length,0);assert.deepEqual(await fs.readdir(base),[]);
 await api.prompt('example','work',[id]);
 assert.equal(prompts.length,1);
 assert.equal(await fs.readFile(prompts[0].files[0].path,'utf8'),'draft');
});

test('desktop distinguishes disconnect during copy from disconnect after handing off the prompt',async t=>{
 const prompts=[];
 let runner,failAfterDispatch=false;
 class TestEngine{
  constructor(options){Object.assign(this,options);this.rpc={closed:false};runner=this}
  async init(){}
  async open(){return 'fixture-session'}
  close(){}
  offline(){this.rpc.closed=true;this.emit({type:'offline',text:'fixture disconnected'})}
  async prompt(text,files){
   prompts.push({text,files});
   if(this.rpc.closed||failAfterDispatch){this.offline();throw new Error('fixture disconnected')}
  }
 }
 const {api,tmp,setPick,events}=await desktop(t,{Engine:TestEngine});
 const executable=path.join(tmp,process.platform==='win32'?'agy.exe':'agy'),project=path.join(tmp,'project'),file=path.join(tmp,'draft.txt');
 await Promise.all([fs.writeFile(executable,'fixture (not executed)',{mode:0o700}),fs.mkdir(project),fs.writeFile(file,'draft')]);
 setPick({filePaths:[executable]});await api.executable();
 setPick({filePaths:[project]});await api.pick();
 await api.connect('example',project,null,false,false);
 setPick({filePaths:[file]});const [{id}]=await api.attach('file');
 const base=path.join(project,'.orbit-attachments'),writeFile=fs.writeFile;let disconnect=true;
 t.mock.method(fs,'writeFile',async(dest,...args)=>{
  const result=await writeFile(dest,...args);
  if(disconnect&&dest.startsWith(base+path.sep)){disconnect=false;runner.offline()}
  return result;
 });
 const first=await api.prompt('example','work',[id]);
 assert.equal(first.notSent,true);assert.match(first.message,/未发送/);
 assert.equal(prompts.length,0);assert.deepEqual(await fs.readdir(base),[]);
 assert.equal(events.filter(x=>x.type==='offline').length,1);
 await api.connect('example',project,'fixture-session',false,false);
 failAfterDispatch=true;
 const second=await api.prompt('example','work',[id]);
 assert.equal(second.uncertain,true);assert.equal(second.notSent,undefined);
 assert.equal(prompts.length,1);
 assert.equal(await fs.readFile(prompts[0].files[0].path,'utf8'),'draft');
 await assert.rejects(api.prompt('example','work',[id]),/附件已失效/);
});

test('desktop picker and drop report oversized batches without returning a partial selection',async t=>{
 const {api,tmp,drop,setPick}=await desktop(t);
 const paths=Array.from({length:17},(_,i)=>path.join(tmp,'file-'+i+'.txt'));
 await Promise.all(paths.map(file=>fs.writeFile(file,'test')));
 setPick({filePaths:paths});
 await assert.rejects(api.attach('file'),/最多选择 16.*本次未添加/);
 const received=new Promise(resolve=>{
  api.onDrop(()=>17,(files,id)=>resolve({files,id}),(error,id)=>resolve({error,id}));
  drop()({isTrusted:true,dataTransfer:{files:paths.map(selectedPath=>({selectedPath}))},preventDefault(){}});
 });
 const result=await received;
 assert.equal(result.id,17);assert.equal(result.files,undefined);assert.match(result.error,/最多选择 16.*本次未添加/);
 setPick({filePaths:paths.slice(0,16)});
 assert.equal((await api.attach('file')).length,16,'the user can retry with a selection within the limit');
});

test('desktop never registers a trusted drop when the renderer declines the destination',async t=>{
 const {api,tmp,drop,calls}=await desktop(t);
 const folder=path.join(tmp,'fixture-folder');await fs.mkdir(folder);
 let destination=null,prevented=false;
 const added=new Promise((resolve,reject)=>api.onDrop(()=>destination,(files,id)=>resolve({files,id}),reject));
 const event={isTrusted:true,dataTransfer:{files:[{selectedPath:folder}]},preventDefault(){prevented=true}};
 drop()(event);
 assert.equal(prevented,true,'blocked drops must not fall through to OS or browser navigation');
 assert.deepEqual(calls,[]);
 destination='conversation-B';
 drop()(event);
 const {files,id}=await added;
 assert.equal(id,destination);
 assert.equal(files[0].name,'fixture-folder');assert.equal(files[0].kind,'folder');
 assert.deepEqual(calls,['drop']);
});
