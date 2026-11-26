const {test}=require('node:test');const assert=require('node:assert/strict');
const {EventEmitter,once}=require('node:events');const path=require('node:path');
const {Engine}=require('../core.cjs');
function fixture(t,stopTimeout){
 const stream=new EventEmitter();const r=new Engine({exe:process.execPath,cwd:__dirname,stopTimeout,args:[path.resolve(__dirname,'mock.cjs')],emit:e=>stream.emit(e.type,e)});
 t.after(()=>r.close());return {r,stream};
}
test('Google auth, ACP session restore and model controls',async t=>{
 const {r}=fixture(t);await r.init();assert.equal(r.canAuthenticate,true);
 await assert.rejects(r.open(null),e=>e.code===-32000);await r.auth();
 assert.equal(await r.open(null),'a1');
 assert.equal(r.config[0].currentValue,'flash');
 assert.deepEqual(r.config[1].options.map(x=>x.value),['plan']);
 await assert.rejects(r.configure('mode','bypassPermissions'),/不可用/);
 assert.equal((await r.configure('model','pro'))[0].currentValue,'pro');
 assert.equal(await r.open('a1'),'a1');
});
test('Cua Driver is opt-in and is passed only as stdio when supplied',async t=>{
 const {r}=fixture(t);await r.init();await r.auth();await r.open(null);
 assert.deepEqual((await r.rpc.call('fixture/last_session',{})).mcpServers,[]);
 const mcp=[{name:'cua-driver',command:'/fixture/cua-driver',args:['mcp'],env:[]}];
 await r.open(null,mcp);
 assert.deepEqual((await r.rpc.call('fixture/last_session',{})).mcpServers,mcp);
});
test('streams reply, presents scoped approvals and cancels an ongoing turn',async t=>{
 const {r,stream}=fixture(t);await r.init();await r.auth();await r.open(null);
 const delta=once(stream,'delta'),approval=once(stream,'approval'),done=once(stream,'done');
 const p=r.prompt('inspect');
 assert.equal((await delta)[0].text,'ok');
 const [a]=await approval;assert.deepEqual(a.actions.map(o=>o.id),['once','deny']);r.decide(a.key,'once');
 await p;assert.equal((await done)[0].status,'end_turn');
 const started=once(stream,'delta'),stopped=once(stream,'done');
 const pending=r.prompt('cancel');await started;r.cancel();await pending;assert.equal((await stopped)[0].status,'cancelled');
});
test('parallel sessions do not share events; steering does not finish a different conversation',async t=>{
 const a=fixture(t),b=fixture(t);await Promise.all([a.r.init(),b.r.init()]);await Promise.all([a.r.auth(),b.r.auth()]);await Promise.all([a.r.open(null),b.r.open(null)]);
 const aStart=once(a.stream,'delta'),bStart=once(b.stream,'delta'),aEnd=once(a.stream,'done'),bEnd=once(b.stream,'done');
 const p=a.r.prompt('background'),q=b.r.prompt('cancel');await Promise.all([aStart,bStart]);
 await a.r.steer('supplement');await p;
 assert.equal((await aEnd)[0].status,'end_turn');assert.equal(b.r.active,true);
 b.r.cancel();await q;assert.equal((await bEnd)[0].status,'cancelled');
});
test('only in-workspace agent diffs become task diffs',async t=>{
 const {r,stream}=fixture(t);await r.init();await r.auth();await r.open(null);const changes=[];stream.on('taskDiff',d=>changes.push(d));
 await r.prompt('with-diff');
 assert.deepEqual(changes,[{type:'taskDiff',path:'demo.swift',before:'old',after:'new'}]);
});
test('unacknowledged stop closes its connection rather than waiting forever',async t=>{
 const a=fixture(t,75),b=fixture(t);
 await Promise.all([a.r.init(),b.r.init()]);await Promise.all([a.r.auth(),b.r.auth()]);await Promise.all([a.r.open(null),b.r.open(null)]);
 const start=once(a.stream,'delta'),offline=once(a.stream,'offline'),pending=a.r.prompt('unstoppable');
 const rejected=assert.rejects(pending,e=>e.code==='STOP_TIMEOUT');
 await start;a.r.cancel();await rejected;
 assert.equal((await offline)[0].code,'STOP_TIMEOUT');assert.equal(b.r.rpc.closed,false);
});
