const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {createRequire}=require('node:module'),{runInNewContext}=require('node:vm');

// Run the actual preload and main attachment handlers with only Electron's shell mocked.
async function desktop(t){
 const tmp=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-ipc-'));
 t.after(()=>fs.rm(tmp,{recursive:true,force:true}));
 const handlers=new Map(),listeners=new Map(),calls=[];
 const frame={},sender={mainFrame:frame,setWindowOpenHandler(){},on(){}};
 let api,pick={canceled:true};
 const electron={
  app:{getPath:()=>tmp,setPath(){},setName(){},on(){},requestSingleInstanceLock:()=>true,whenReady:async()=>{}},
  BrowserWindow:class{webContents=sender;loadFile(){}},
  nativeTheme:{},dialog:{showOpenDialog:async()=>pick},
  ipcMain:{handle:(name,fn)=>handlers.set(name,fn)},
  ipcRenderer:{invoke:async(name,...args)=>{calls.push(name);return handlers.get(name)({sender,senderFrame:frame},...args)}},
  contextBridge:{exposeInMainWorld:(name,value)=>{assert.equal(name,'orbit');api=value}},
  webUtils:{getPathForFile:file=>file.selectedPath||''}
 };
 async function load(file){
  const filename=path.join(__dirname,'..',file),req=createRequire(filename);
  runInNewContext(await fs.readFile(filename,'utf8'),{
   require:name=>name==='electron'?electron:req(name),__dirname:path.dirname(filename),process,
   window:{addEventListener:(type,fn)=>listeners.set(type,fn)}
  },{filename});
 }
 await load('main.cjs');await load('preload.cjs');
 return {api,tmp,calls,drop:()=>listeners.get('drop'),setPick:value=>{pick=value}};
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
