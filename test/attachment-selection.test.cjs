const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const os=require('node:os'),path=require('node:path');
const {registerPaths}=require('../attachments.cjs');

test('failed Finder batches leave no invisible attachment IDs and can be retried',async t=>{
 const dir=await fs.mkdtemp(path.join(await fs.realpath(os.tmpdir()),'orbite-selection-'));
 t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const file=path.join(dir,'笔记.txt'),vanished=path.join(dir,'已移走.txt'),link=path.join(dir,'链接'),folder=path.join(dir,'示例目录');
 await Promise.all([fs.writeFile(file,'text'),fs.mkdir(folder),fs.symlink(file,link)]);
 const registry=new Map([['older',file]]);
 await assert.rejects(registerPaths([file,vanished],registry),e=>e.message.includes('已移走.txt')&&!e.message.includes(dir));
 await assert.rejects(registerPaths([file,link],registry),/不支持符号链接/);
 assert.deepEqual([...registry],[['older',file]]);
 const added=await registerPaths([file,folder],registry);
 assert.deepEqual(added.map(({name,kind,size})=>({name,kind,size})),[{name:'笔记.txt',kind:'file',size:4},{name:'示例目录',kind:'folder',size:null}]);
 assert.deepEqual(added.map(a=>registry.get(a.id)),[file,folder]);
 assert.equal(registry.size,3);
});
