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
 let selection=cli,initCode,openCode=-32000,canAuthenticate=true,onAuth=async()=>{};
 const failure=code=>Object.assign(new Error('認証情報を入力してください'),{code});
 class Engine{
  get canAuthenticate(){return canAuthenticate}
  async init(){if(initCode)throw failure(initCode)}
  async open(id){calls.push(['open',id]);if(openCode)throw failure(openCode);return id}
  async auth(){calls.push(['auth']);await onAuth();openCode=null}
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
 let finishAuth;
 onAuth=()=>{if(finishAuth)throw new Error('fixture saw duplicate authentication');return new Promise((_resolve,reject)=>{finishAuth=reject})};
 const firstAuth=invoke('authenticate','task-one');
 await assert.rejects(invoke('authenticate','task-one'),/请稍候/);
 assert.deepEqual(calls,[['open','saved-session'],['auth']]);
 finishAuth(new Error('fixture login cancelled'));
 await assert.rejects(firstAuth,/fixture login cancelled/);
 onAuth=async()=>{};
 assert.equal((await invoke('authenticate','task-one')).session,'saved-session','a cancelled attempt can be retried');
 await assert.rejects(invoke('authenticate','task-one'),/没有待处理/);
 assert.deepEqual(calls,[['open','saved-session'],['auth'],['auth'],['open','saved-session']]);

 openCode=-32002;
 await assert.rejects(connect('task-two'),e=>e.code===-32002);
 assert.deepEqual(calls.slice(-2),[['open','saved-session'],['close']]);
 calls.length=0;initCode=-32000;
 await assert.rejects(connect('task-three'),e=>e.code===-32000);
 assert.deepEqual(calls,[['close']],'an initialization failure must not enable session authentication');
 initCode=null;openCode=-32000;canAuthenticate=false;
 const terminal=await connect('task-four');
 assert.equal(terminal.auth,true);assert.equal(terminal.available,false);
 await assert.rejects(invoke('authenticate','task-four'),/在终端运行 agy/);
 assert.deepEqual(calls,[['close'],['open','saved-session']]);
});
