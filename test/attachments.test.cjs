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

const {clipboardImage}=require('../attachments.cjs');
const examplePng=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aP1sAAAAASUVORK5CYII=','base64');
test('clipboard images have no workspace copy before send and stage alongside Finder files',async t=>{
 const {root,src}=await fixture(t);const plain=p.join(src,'notes.txt');await fs.writeFile(plain,'notes');
 const source=Uint8Array.from(examplePng),image=clipboardImage('image/png',source);source.fill(0);
 await assert.rejects(fs.stat(p.join(root,'.orbit-attachments')),e=>e.code==='ENOENT');
 const [staged,other]=await stage(root,[image,plain]);
 assert.equal(staged.name,'剪贴板图片.png');assert.equal(staged.clipboard,true);assert.equal(staged.size,examplePng.length);
 assert.equal(Object.hasOwn(staged,'bytes'),false);
 assert.deepEqual(await fs.readFile(staged.path),examplePng);assert.equal(await fs.readFile(other.path,'utf8'),'notes');
 assert.equal((await fs.stat(staged.path)).mode&0o777,0o600);
});
test('clipboard input rejects invalid MIME, signatures and oversized payloads',()=>{
 assert.throws(()=>clipboardImage('image/svg+xml',examplePng),/格式无效/);
 assert.throws(()=>clipboardImage('image/jpeg',examplePng),/格式无效/);
 assert.throws(()=>clipboardImage('image/png',new Uint8Array(16*1024*1024+1)),/16 MiB/);
 assert.throws(()=>clipboardImage('image/png',{data:[1,2,3]}));
});
test('staging caps combined clipboard attachments and cleans up a rejected batch',async t=>{
 const {root}=await fixture(t);
 const payload=Buffer.alloc(16*1024*1024);examplePng.copy(payload);
 const image=clipboardImage('image/png',payload);
 await assert.rejects(stage(root,Array(5).fill(image)),/超过限制/);
 assert.deepEqual(await fs.readdir(p.join(root,'.orbit-attachments')),[]);
});
