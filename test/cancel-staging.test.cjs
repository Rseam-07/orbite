const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {stage}=require('../attachments.cjs');

test('canceling attachment preparation removes only the incomplete copy and allows retry',async t=>{
 const root=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-cancel-'));
 t.after(()=>fs.rm(root,{recursive:true,force:true}));
 const project=path.join(root,'project'),source=path.join(root,'folder'),staging=path.join(project,'.orbit-attachments');
 await Promise.all([fs.mkdir(project),fs.mkdir(source)]);
 await fs.writeFile(path.join(source,'notes.txt'),'draft source');
 const previous=await stage(project,[source]);
 const previousBatches=await fs.readdir(staging);
 const controller=new AbortController(),reason=new Error('stop during copy preparation'),writeFile=fs.writeFile;
 t.mock.method(fs,'writeFile',async (dest,...args)=>{
  const result=await writeFile(dest,...args);
  if(dest.startsWith(staging+path.sep))controller.abort(reason);
  return result;
 });
 await assert.rejects(stage(project,[source],{signal:controller.signal}),error=>error===reason);
 assert.deepEqual(await fs.readdir(staging),previousBatches);
 assert.equal(await fs.readFile(path.join(previous[0].path,'notes.txt'),'utf8'),'draft source');
 assert.equal(await fs.readFile(path.join(source,'notes.txt'),'utf8'),'draft source');
 const [retry]=await stage(project,[source]);
 assert.equal(await fs.readFile(path.join(retry.path,'notes.txt'),'utf8'),'draft source');
});
