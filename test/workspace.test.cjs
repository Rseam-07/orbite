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
test('reopens an existing task when the source branch has no commit yet',async t=>{
 const {source,storage}=await fixture(t);
 const task=await worktree(source,'existing-task',storage);
 const edited=p.join(task.dir,'资料.txt');
 await fs.writeFile(edited,'in-progress task edit\n');
 // Point only the disposable source fixture at a new branch, without touching its files.
 await git(source,['symbolic-ref','HEAD','refs/heads/new-unborn-branch']);
 await assert.rejects(git(source,['rev-parse','--verify','HEAD']));
 assert.deepEqual(await worktree(source,'existing-task',storage),task);
 assert.equal(await fs.readFile(edited,'utf8'),'in-progress task edit\n');
 assert.match((await review(task.dir,task.base)).patch,/in-progress task edit/);
 await assert.rejects(worktree(source,'new-task',storage),/至少有一个提交/);
 assert.deepEqual(await fs.readdir(storage),['existing-task']);
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
