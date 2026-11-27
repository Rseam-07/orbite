const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const os=require('node:os');
const path=require('node:path');
const {mcpInventory,diagnose,selfTest,grant,readPermission}=require('../computer-use.cjs');
const fixture=path.join(__dirname,'cua-fixture.cjs');
async function binary(t){
 const tmp=await fs.mkdtemp(path.join(os.tmpdir(),'orbit-cua-'));t.after(()=>fs.rm(tmp,{recursive:true,force:true}));
 const exe=path.join(tmp,'cua-driver');await fs.copyFile(fixture,exe);await fs.chmod(exe,0o700);return exe;
}
test('MCP discovery negotiates versions and follows tool pagination',async()=>{
 const x=await mcpInventory(process.execPath,{args:[fixture,'mcp']});
 assert.equal(x.protocol,'2025-06-18');assert.equal(x.tools,5);assert.deepEqual(x.missing,[]);assert.equal(x.readonlyPermissions,true);
});
test('reports missing driver tools without passing its health check',async()=>{
 const x=await mcpInventory(process.execPath,{args:[fixture,'missing']});assert.deepEqual(x.missing,['get_window_state','click','type_text']);
 const y=await diagnose(null,'darwin');assert.equal(y.found,false);assert.equal(y.mcp,null);
});
test('version, permission attribution and official self-test are independent states',{skip:process.platform==='win32'},async t=>{
 const exe=await binary(t);
 const r=await diagnose(exe,'darwin');
 assert.equal(r.version,'0.40.1');assert.equal(r.mcp.tools,5);assert.equal(r.mcp.readonlyPermissions,true);
 assert.deepEqual(r.permissions,{accessibility:true,screen:false,capturable:null,source:'driver-daemon'});
 assert.deepEqual(readPermission({permissions:{accessibility:'true'}}),{accessibility:null,screen:null,capturable:null,source:null});
 assert.equal((await grant(exe,'darwin')).ok,false);
 assert.equal((await selfTest(exe,'linux')).ok,false);
 assert.equal((await selfTest(exe,'darwin')).ok,true);
});

test('only a catalog with a boolean prompt argument enables the read-only permissions check',async()=>{
 for(const mode of ['no-permission-tool','ambiguous-permission-tool']){
  const x=await mcpInventory(process.execPath,{args:[fixture,mode]});
  assert.deepEqual(x.missing,[],'core desktop actions stay independent of the permissions probe');
  assert.equal(x.readonlyPermissions,false);
 }
});
