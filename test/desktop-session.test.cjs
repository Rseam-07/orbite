const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {createRequire}=require('node:module'),{runInNewContext}=require('node:vm');

test('desktop uses the ACP auth code during restore, not the error language',async t=>{
 const tmp=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-session-ipc-'));
 t.after(()=>fs.rm(tmp,{recursive:true,force:true}));
 const handlers=new Map(),frame={},sender={mainFrame:frame,setWindowOpenHandler(){},on(){}};
 const electron={
  app:{getPath:()=>tmp,setPath(){},setName(){},on(){},requestSingleInstanceLock:()=>true,whenReady:()=>({then:fn=>fn()})},
  BrowserWindow:class{webContents=sender;loadFile(){}},
  nativeTheme:{},dialog:{showOpenDialog:async()=>({filePaths:[selection]})},
  ipcMain:{handle:(name,fn)=>handlers.set(name,fn)}
 };
 const project=path.join(tmp,'project'),cli=path.join(tmp,'agy'),calls=[];
 await fs.mkdir(project);await fs.writeFile(cli,'fixture (not executed)',{mode:0o700});
 let selection=cli,initCode,openCode=-32000,canAuthenticate=true;
 const failure=code=>Object.assign(new Error('認証情報を入力してください'),{code});
 class Engine{
  get canAuthenticate(){return canAuthenticate}
  async init(){if(initCode)throw failure(initCode)}
  async open(id){calls.push(['open',id]);if(openCode)throw failure(openCode);return id}
  async auth(){calls.push(['auth']);openCode=null}
  close(){calls.push(['close'])}
 }
 const filename=path.join(__dirname,'..','main.cjs'),req=createRequire(filename);
 runInNewContext(await fs.readFile(filename,'utf8'),{
  require:name=>name==='electron'?electron:name==='./core.cjs'?{Engine}:req(name),
  __dirname:path.dirname(filename),process
 },{filename});
 const invoke=(method,...args)=>handlers.get(method)({sender,senderFrame:frame},...args);
 await invoke('executable');selection=project;await invoke('pick');
 const connect=id=>invoke('connect',id,project,'saved-session',false,false);
 const initial=await connect('task-one');
 assert.equal(initial.auth,true);assert.equal(initial.available,true);
 assert.deepEqual(calls,[['open','saved-session']],'no authentication starts until explicitly requested');
 assert.equal((await invoke('authenticate','task-one')).session,'saved-session');
 assert.deepEqual(calls,[['open','saved-session'],['auth'],['open','saved-session']]);

 openCode=-32002;
 await assert.rejects(connect('task-two'),e=>e.code===-32002);
 assert.deepEqual(calls.slice(-2),[['open','saved-session'],['close']]);
 calls.length=0;initCode=-32000;
 await assert.rejects(connect('task-three'),e=>e.code===-32000&&e.message.includes('agy acp --help')&&!e.message.includes('認証情報'));
 assert.deepEqual(calls,[['close']],'an initialization failure must not enable session authentication');
 initCode=null;openCode=-32000;canAuthenticate=false;
 const terminal=await connect('task-four');
 assert.equal(terminal.auth,true);assert.equal(terminal.available,false);
});

test('missing CLI defeats both preflight and real Git worktree creation',async t=>{
 const native=require('node:fs'),{git}=require('../workspace.cjs');
 const tmp=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-cli-check-'));
 t.after(()=>fs.rm(tmp,{recursive:true,force:true,maxRetries:4,retryDelay:50}));
 const project=path.join(tmp,'project'),cli=path.join(tmp,process.platform==='win32'?'agy.exe':'agy');
 await fs.mkdir(project);
 await git(project,['init','-q']);
 await git(project,['-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','--allow-empty','-qm','start']);
 const branchList=()=>git(project,['branch','--list','orbit/*']);
 const handlers=new Map(),frame={},sender={mainFrame:frame,setWindowOpenHandler(){},on(){}};
 let selection=project,installed=false;
 const electron={
  app:{getPath:()=>tmp,setPath(){},setName(){},on(){},requestSingleInstanceLock:()=>true,whenReady:()=>({then:fn=>fn()})},
  BrowserWindow:class{webContents=sender;loadFile(){}},
  nativeTheme:{},dialog:{showOpenDialog:async()=>({filePaths:[selection]})},
  ipcMain:{handle:(name,fn)=>handlers.set(name,fn)}
 };
 const filename=path.join(__dirname,'..','main.cjs'),req=createRequire(filename);
 const mockFs={...native,statSync(file,...args){
  if(/^agy(?:\.exe)?$/i.test(path.basename(file))&&(!installed||file!==cli))throw Object.assign(new Error('fixture CLI missing'),{code:'ENOENT'});
  return native.statSync(file,...args);
 }};
 runInNewContext(await fs.readFile(filename,'utf8'),{
  require:name=>name==='electron'?electron:name==='node:fs'?mockFs:name==='./executables.cjs'?{findExecutable:()=>installed?cli:null}:req(name),
  __dirname:path.dirname(filename),process
 },{filename});
 const invoke=(method,...args)=>handlers.get(method)({sender,senderFrame:frame},...args);
 await invoke('pick');
 assert.throws(()=>invoke('cli-preflight'),/未找到 agy/);
 await assert.rejects(invoke('worktree','task-preflight',project),/未找到 agy/);
 assert.equal(await branchList(),'');
 await fs.writeFile(cli,'unexecuted resolver fixture',{mode:0o700});installed=true;selection=cli;
 await invoke('executable');
 assert.equal(invoke('cli-preflight'),true);
 const result=await invoke('worktree','task-preflight',project);
 assert.equal(result.branch,'orbit/task-pre');
 assert.match(await branchList(),/orbit\/task-preflight/);
});
