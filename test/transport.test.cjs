const {test}=require('node:test');
const assert=require('node:assert/strict');
const path=require('node:path');
const {RPC}=require('../transport.cjs');
function fixture(t,mode,frameLimit=256){
 const events=[];
 const r=new RPC(process.execPath,[path.join(__dirname,'transport-fixture.cjs'),mode],__dirname,()=>{},(msg,code)=>events.push([msg,code]),{frameLimit});
 t.after(()=>r.close());return {r,events};
}
test('transport joins fragmented multibyte messages and CRLF',async t=>{
 const {r}=fixture(t,'unicode');
 assert.equal(await r.call('ping',{}),'中文 · 🌟');assert.equal(r.pending.size,0);
});
for(const [mode,result] of [['overflow','FRAME_LIMIT'],['malformed','BAD_JSON'],['exit','EXIT']]){
 test('transport fails pending requests exactly once: '+mode,async t=>{
  const {r,events}=fixture(t,mode);
  const responses=await Promise.allSettled([r.call('one',{}),r.call('two',{})]);
  for(const response of responses){assert.equal(response.status,'rejected');assert.equal(response.reason.code,result)}
  assert.equal(events.filter(x=>x[1]===result).length,1);assert.equal(r.pending.size,0);
 });
}
test('timeout removes pending entry, close rejects the next request immediately',async t=>{
 const {r}=fixture(t,'stall');await assert.rejects(r.call('ping',{},30),e=>e.code==='TIMEOUT');
 assert.equal(r.pending.size,0);
 const p=r.call('second',{},0);r.close();
 await assert.rejects(p,e=>e.code==='CLOSED');assert.equal(r.pending.size,0);
});
