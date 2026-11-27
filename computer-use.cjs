const fs=require('node:fs');
const path=require('node:path');
const os=require('node:os');
const {execFile}=require('node:child_process');
const {RPC}=require('./transport.cjs');
const VERSIONS=new Set(['2025-11-25','2025-06-18','2025-03-26','2024-11-05']);
const required=['get_window_state','list_windows','click','type_text'];
function cliEnv(){return Object.fromEntries(Object.entries(process.env).filter(([k])=>!['ELECTRON_RUN_AS_NODE','NODE_OPTIONS'].includes(k)))}
function executable(file){
 try{
  if(!path.isAbsolute(file)||!fs.statSync(file).isFile())return null;
  fs.accessSync(file,fs.constants.X_OK);return fs.realpathSync(file);
 }catch{return null}
}
function findDriver(){
 const dirs=[path.join(os.homedir(),'.local/bin'),'/opt/homebrew/bin','/usr/local/bin','/usr/bin',...(process.env.PATH??'').split(path.delimiter).filter(path.isAbsolute)];
 const appPaths=['/Applications/CuaDriver.app/Contents/MacOS/cua-driver',path.join(os.homedir(),'Applications/CuaDriver.app/Contents/MacOS/cua-driver')];
 for(const f of [...dirs.map(d=>path.join(d,'cua-driver')),...appPaths]){const x=executable(f);if(x)return x}
 return null;
}
function run(exe,args,timeout){
 return new Promise(resolve=>execFile(exe,args,{cwd:os.homedir(),shell:false,env:cliEnv(),timeout,maxBuffer:192*1024},(error,stdout)=>{
  resolve({ok:!error,code:typeof error?.code==='number'?error.code:null,timeout:Boolean(error?.killed),stdout});
 }));
}
function json(raw){try{return JSON.parse(raw)}catch{return null}}
function bool(b){return typeof b==='boolean'?b:null}
function readPermission(status){
 const p=status?.permissions??status?.result??status;
 return {accessibility:bool(p?.accessibility),screen:bool(p?.screen_recording),capturable:bool(p?.screen_recording_capturable),source:typeof p?.source?.attribution==='string'?p.source.attribution.slice(0,32):null};
}
async function mcpInventory(exe,{args=['mcp'],cwd=os.homedir(),timeout=8000}={}){
 const m=new RPC(exe,args,cwd,msg=>{if(Object.hasOwn(msg,'id'))m.reject(msg.id)},()=>{});
 try{
  const r=await m.call('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'orbit-computer-use',version:'0.7.0'}},timeout);
  if(!VERSIONS.has(r?.protocolVersion)||!r.capabilities?.tools)throw new Error('MCP 握手未公布工具能力。');
  m.notify('notifications/initialized',{});
  const names=new Set();let cursor,readonlyPermissions=false;
  for(let i=0;i<5;i++){
   const t=await m.call('tools/list',cursor?{cursor}:{},timeout);
   if(!Array.isArray(t?.tools))throw new Error('MCP 工具列表无效。');
   for(const tool of t.tools)if(typeof tool?.name==='string'){
    names.add(tool.name);
    // The tool can request OS prompts by default. Require an advertised boolean switch.
    if(tool.name==='check_permissions'&&tool.inputSchema?.properties?.prompt?.type==='boolean')readonlyPermissions=true;
   }
   cursor=typeof t.nextCursor==='string'?t.nextCursor:null;if(!cursor)break;
  }
  return {protocol:r.protocolVersion,tools:names.size,missing:required.filter(t=>!names.has(t)),readonlyPermissions,server:String(r.serverInfo?.name??'').slice(0,64)};
 } finally{m.close()}
}
async function diagnose(file,platform=process.platform){
 const out={mac:platform==='darwin',found:Boolean(file),mcp:null,permissions:null,version:null,issues:[]};
 if(!file){out.issues.push('没有找到 cua-driver。请安装官方 Cua Driver 或手动选择。');return out}
 const ver=await run(file,['--version'],5000);
 const v=ver.stdout.match(/\b\d+\.\d+\.\d+(?:[-+.][\w.]+)?\b/)?.[0];
 if(ver.ok&&v)out.version=v;
 else{out.issues.push('驱动无法运行或没有报告版本。请检查安装路径。');return out}
 const results=await Promise.allSettled([
  mcpInventory(file),
  platform==='darwin'?run(file,['permissions','status','--json'],9000):Promise.resolve(null)
 ]);
 if(results[0].status==='fulfilled')out.mcp=results[0].value;
 else out.issues.push('MCP 没有完成握手。驱动可能尚未启动、版本不兼容，或本机连接不可用。');
 const perm=results[1].status==='fulfilled'&&results[1].value;
 if(perm?.ok){
  const parsed=json(perm.stdout);
  if(parsed)out.permissions=readPermission(parsed);
  else out.issues.push('驱动未返回可读的权限状态。');
 }else if(out.mac)out.issues.push('未能读取 macOS 权限状态。');
 if(out.mcp?.missing.length)out.issues.push('MCP 缺少必要工具：'+out.mcp.missing.join('、')+'。');
 if(!out.mac)out.issues.push('这不是 macOS；不能验证 Mac 的录屏或辅助功能权限。');
 return out;
}
async function selfTest(file,platform=process.platform){
 if(platform!=='darwin')return {ok:false,text:'Mac 后台输入自测只能在 macOS 上执行。'};
 const p=await run(file,['test','--json'],75000);
 return p.ok?{ok:true,text:'驱动自测通过：官方测试已完成屏幕读取和后台 TextEdit 输入。'}
  :{ok:false,text:p.timeout?'自测超时；不要把未返回的结果当作成功。':'驱动自测未通过。可以在终端运行 cua-driver test 查看详细原因。'};
}
async function grant(file,platform=process.platform){
 if(platform!=='darwin')return {ok:false,text:'此步骤只适用于 Mac。'};
 const p=await run(file,['permissions','grant'],170000);
 return {ok:p.ok,text:p.ok?'授权流程已完成。请重新检测，再执行驱动自测。':'授权未确认。可以在系统设置检查 CuaDriver，或用终端运行 cua-driver permissions grant。'};
}
module.exports={findDriver,executable,mcpInventory,readPermission,diagnose,selfTest,grant};
