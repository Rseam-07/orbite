const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const os=require('node:os'),path=require('node:path');
const {stage}=require('../attachments.cjs');

test('folder staging streams entries, counts skipped files, and removes a rejected batch',async t=>{
 const dir=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-folder-'));
 t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const root=path.join(dir,'project'),src=path.join(dir,'folder');
 await Promise.all([fs.mkdir(root),fs.mkdir(src)]);
 // Real directories and file copying, but fail if the source listing is materialized.
 const readdir=fs.readdir;
 t.mock.method(fs,'readdir',async (dir,...args)=>{
  assert.notEqual(dir,src,'must not read the entire source listing');
  return readdir(dir,...args);
 });
 for(let i=0;i<998;i+=32){
  await Promise.all(Array.from({length:Math.min(32,998-i)},(_,j)=>fs.writeFile(path.join(src,'.env.'+(i+j)),'excluded')));
 }
 await fs.writeFile(path.join(src,'keep.txt'),'kept');
 const [folder]=await stage(root,[src]); // root + 998 skipped + 1 regular file
 assert.equal(folder.skipped.length,998);
 assert.deepEqual(await readdir(folder.path),['keep.txt']);
 assert.equal(await fs.readFile(path.join(folder.path,'keep.txt'),'utf8'),'kept');
 const staging=path.join(root,'.orbit-attachments'),successful=await readdir(staging);
 await fs.writeFile(path.join(src,'.env.extra'),'excluded');
 await assert.rejects(stage(root,[src]),/1,000 个条目/);
 assert.deepEqual(await readdir(staging),successful,'a rejected attempt must not leave another batch');
});
