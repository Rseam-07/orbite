const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const os=require('node:os');
const p=require('node:path');
const {git,worktree,review,exportPatch}=require('../workspace.cjs');
async function fixture(t){
 const dir=await fs.mkdtemp(p.join(await fs.realpath(os.tmpdir()),'orbit-test-'));t.after(()=>fs.rm(dir,{force:true,recursive:true}));
 const source=p.join(dir,'repo'),storage=p.join(dir,'worktrees');await fs.mkdir(source);await fs.mkdir(storage);
 const file=p.join(source,'资料.txt');
 await git(source,['init','-q']);await fs.writeFile(file,'baseline\n');await git(source,['add','.']);
 await git(source,['-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','baseline']);
 return {source,storage,file};
}
test('two real Git worktrees preserve source changes and each has its own diff',async t=>{
 const {source,storage,file}=await fixture(t);
 await fs.writeFile(file,'user local edit\n');
 const [a,b]=await Promise.all([worktree(source,'task-one',storage),worktree(source,'task-two',storage)]);
 assert.notEqual(a.dir,b.dir);
 assert.equal(await fs.readFile(p.join(a.dir,'资料.txt'),'utf8'),'baseline\n');
 await fs.writeFile(p.join(a.dir,'资料.txt'),'first task only\n');
 await git(a.dir,['add','资料.txt']);await git(a.dir,['-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','test commit']);
 assert.match((await review(a.dir,a.base)).patch,/first task only/);
 assert.match((await review(a.dir,a.base)).status,/资料\.txt/);
 assert.equal((await review(b.dir,b.base)).patch,'');
 assert.equal(await fs.readFile(file,'utf8'),'user local edit\n');
 assert.deepEqual(await worktree(source,'task-one',storage),a);
});
test('reject invalid IDs and repository subdirectories',async t=>{
 const {source,storage}=await fixture(t);await fs.mkdir(p.join(source,'sub'));
 await assert.rejects(worktree(source,'../escape',storage),/无效/);
 await assert.rejects(worktree(p.join(source,'sub'),'task-id',storage),/根目录/);
});

test('exports a base-relative patch without changing the worktree or including attachment copies',async t=>{
 const {source,storage}=await fixture(t);
 const w=await worktree(source,'patch-task',storage);
 await fs.mkdir(p.join(w.dir,'.orbit-attachments'));
 await fs.writeFile(p.join(w.dir,'.orbit-attachments','example'),'do not export');
 await fs.writeFile(p.join(w.dir,'资料.txt'),'tracked output\n');
 await fs.writeFile(p.join(w.dir,'new.txt'),'untracked output');
 const r=await exportPatch(w.dir,w.base);
 assert.match(r.patch,/tracked output/);assert.doesNotMatch(r.patch,/do not export|untracked output/);
 assert.equal(r.untracked,1);
 assert.equal(await fs.readFile(p.join(w.dir,'资料.txt'),'utf8'),'tracked output\n');
});

test('review and export hide only attachment staging in each workspace, without configuring Git',async t=>{
 const {source,storage,file}=await fixture(t);
 const isolated=await worktree(source,'review-scope',storage);
 for(const [cwd,base] of [[source,undefined],[isolated.dir,isolated.base]]){
  await fs.mkdir(p.join(cwd,'.orbit-attachments'),{recursive:true});
  await fs.mkdir(p.join(cwd,'nested','.orbit-attachments'),{recursive:true});
  await fs.writeFile(p.join(cwd,'.orbit-attachments','orbite-copy.txt'),'generated attachment');
  await fs.writeFile(p.join(cwd,'nested','.orbit-attachments','project-owned.txt'),'project content');
  await fs.writeFile(p.join(cwd,'new-project.txt'),'an ordinary untracked file');
  await fs.writeFile(p.join(cwd,p.basename(file)),'edited project file\n');
  await git(cwd,['add','-f','.orbit-attachments/orbite-copy.txt','nested/.orbit-attachments/project-owned.txt']);
  if(base)await git(cwd,['-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','stage fixture']);
  await fs.writeFile(p.join(cwd,'.orbit-attachments','private-untracked.txt'),'another generated copy');
  const before=await git(cwd,['status','--short','--untracked-files=all']);
  assert.match(before,/private-untracked/);
  const result=await review(cwd,base);
  assert.doesNotMatch(result.status,/orbite-copy|private-untracked/);
  assert.doesNotMatch(result.patch,/generated attachment/);
  assert.match(result.status,/new-project\.txt/);
  assert.match(result.status,/nested\/\.orbit-attachments\/project-owned\.txt/);
  assert.match(result.patch,/edited project file/);
  assert.match(result.patch,/project content/);
  if(base){
   const exported=await exportPatch(cwd,base);
   assert.equal(exported.untracked,1);
   assert.doesNotMatch(exported.patch,/generated attachment/);
   assert.match(exported.patch,/project content/);
  }
  assert.equal(await git(cwd,['status','--short','--untracked-files=all']),before);
 }
});

test('review from a repository subfolder excludes only its own generated attachment directory',async t=>{
 const {source,file}=await fixture(t);
 const project=p.join(source,'subproject');await fs.mkdir(p.join(project,'.orbit-attachments'),{recursive:true});
 await fs.writeFile(p.join(project,'.orbit-attachments','orbite-copy.txt'),'generated attachment');
 await git(source,['add','-f','subproject/.orbit-attachments/orbite-copy.txt']);
 await fs.writeFile(file,'sibling edit\n');
 const {status,patch}=await review(project);
 assert.doesNotMatch(status,/\.orbit-attachments/);assert.doesNotMatch(patch,/generated attachment/);
 assert.match(status,/资料\.txt/);assert.match(patch,/sibling edit/);
});
