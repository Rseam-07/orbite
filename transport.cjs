const { spawn }=require('node:child_process');
const { StringDecoder }=require('node:string_decoder');
const maxFrame=16*1024*1024;
class RpcError extends Error {
 constructor(message,code){super(message);this.name='RpcError';this.code=code}
}
class RPC{
 constructor(exe,args,cwd,onMessage,onExit,{frameLimit=maxFrame,spawnProcess=spawn}={}){
  this.next=0;this.pending=new Map();this.closed=false;
  this.decoder=new StringDecoder('utf8');this.tail='';this.tailBytes=0;this.frameLimit=frameLimit;
  this.onMessage=onMessage;this.onExit=onExit;
  this.child=spawnProcess(exe,args,{cwd,stdio:['pipe','pipe','pipe'],shell:false});
  // Do not expose CLI stderr: it can contain auth material and local details.
  this.child.stderr.resume();
  this.child.stdin.on('error',()=>this.terminate('Antigravity 输入流中断。','PIPE'));
  this.child.on('error',e=>this.terminate('无法启动 Antigravity（'+e.code+'）。','SPAWN'));
  this.child.on('close',(c,sig)=>this.terminate('Antigravity 已断开（'+(sig??c??'未知原因')+'）。','EXIT',false));
  this.child.stdout.on('error',()=>this.terminate('Antigravity 输出流中断。','PIPE'));
  this.child.stdout.on('data',b=>this.feed(b));
 }
 feed(buffer){
  for(let i=0;i<buffer.length&&!this.closed;){
   const sep=buffer.indexOf(10,i),end=sep===-1?buffer.length:sep,n=end-i;
   this.tailBytes+=n;
   if(this.tailBytes>this.frameLimit){this.terminate('Antigravity 返回了过大的消息。请重新连接。','FRAME_LIMIT');return}
   this.tail+=this.decoder.write(buffer.subarray(i,end));
   if(sep===-1)break;
   const line=this.tail+this.decoder.end();this.decoder=new StringDecoder('utf8');this.tail='';this.tailBytes=0;i=end+1;
   if(line.trim())this.receive(line);
  }
 }
 receive(line){
  let m;
  try{m=JSON.parse(line)}catch{this.terminate('Antigravity 返回了无法读取的协议消息。','BAD_JSON');return}
  if(!m||typeof m!=='object'||Array.isArray(m))return;
  if(typeof m.method==='string'){
   try{this.onMessage(m)}catch{this.terminate('Antigravity 返回了无法处理的事件。','BAD_EVENT')}
   return;
  }
  const entry=this.pending.get(m.id);if(!entry)return;
  this.pending.delete(m.id);clearTimeout(entry.timer);
  if(m.error)entry.reject(new RpcError(m.error.message||'引擎请求失败',m.error.code));
  else if(Object.hasOwn(m,'result'))entry.resolve(m.result);
  else entry.reject(new RpcError('引擎没有返回结果。','BAD_RESPONSE'));
 }
 write(m){
  if(this.closed||!this.child.stdin.writable)throw new RpcError('引擎未连接。','CLOSED');
  const payload=JSON.stringify(m)+'\n';
  if(Buffer.byteLength(payload)>this.frameLimit)throw new RpcError('发送内容超过协议大小限制。','FRAME_LIMIT');
  this.child.stdin.write(payload);
 }
 call(method,params,timeout=25000){
  return new Promise((resolve,reject)=>{
   const id=++this.next;
   const timer=timeout?setTimeout(()=>{this.pending.delete(id);reject(new RpcError(method+' 请求超时。','TIMEOUT'))},timeout):null;
   this.pending.set(id,{resolve,reject,timer});
   try{this.write({jsonrpc:'2.0',id,method,params})}
   catch(e){this.pending.delete(id);clearTimeout(timer);reject(e)}
  });
 }
 notify(method,params){this.write({jsonrpc:'2.0',method,params})}
 reply(id,result){this.write({jsonrpc:'2.0',id,result})}
 reject(id){this.write({jsonrpc:'2.0',id,error:{code:-32601,message:'This operation is not exposed by Orbit'}})}
 terminate(reason,code,kill=true){
  if(this.closed)return;
  this.closed=true;
  for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(new RpcError(reason,code))}
  this.pending.clear();
  if(kill){
   this.child.stdin.destroy();
   this.child.kill();
   if(this.child.pid&&this.child.exitCode===null&&this.child.signalCode===null){
    const force=setTimeout(()=>{if(this.child.exitCode===null&&this.child.signalCode===null)this.child.kill('SIGKILL')},1800);
    force.unref();this.child.once('close',()=>clearTimeout(force));
   }
  }
  this.onExit(reason,code);
 }
 close(){this.terminate('Antigravity 连接已关闭。','CLOSED')}
}
module.exports={RPC,RpcError};
