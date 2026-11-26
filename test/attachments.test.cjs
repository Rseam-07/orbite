const {test}=require('node:test');const assert=require('node:assert/strict');
const fs=require('node:fs/promises');const os=require('node:os');const p=require('node:path');
const {stage}=require('../attachments.cjs');
async function fixture(t){const base=await fs.mkdtemp(p.join(await fs.realpath(os.tmpdir()),'orbit-files-'));t.after(()=>fs.rm(base,{recursive:true,force:true}));const root=p.join(base,'repo'),src=p.join(base,'资料');await Promise.all([fs.mkdir(root),fs.mkdir(src)]);return {base,root,src}}
test('copies Unicode files and folders while omitting secrets and symlinks',async t=>{
 const {root,src}=await fixture(t);const f=p.join(src,'文档.txt');
 await fs.writeFile(f,'你好');await fs.writeFile(p.join(src,'.env'),'secret');await fs.symlink(f,p.join(src,'link'));
 const [d,x]=await stage(root,[src,f]);assert.equal(d.kind,'folder');assert.equal(x.kind,'file');assert.deepEqual(d.skipped.sort(),['.env','link']);
 assert.equal(await fs.readFile(p.join(d.path,'文档.txt'),'utf8'),'你好');assert.equal(await fs.readFile(x.path,'utf8'),'你好');
 assert.ok(x.relative.startsWith('.orbit-attachments/'));
});
test('rejects a symlink target for attachment storage',async t=>{
 const {root,src}=await fixture(t);await fs.symlink(src,p.join(root,'.orbit-attachments'));
 await assert.rejects(stage(root,[src]),/普通目录/);
});
