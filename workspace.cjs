const {execFile}=require('node:child_process');
const fs=require('node:fs/promises');
const p=require('node:path');
const {findExecutable}=require('./executables.cjs');
function git(cwd,args,timeout=10000){
 const gitPath=findExecutable('git');
 if(!gitPath)return Promise.reject(new Error('未找到 Git。安装 Git 后再使用审阅或独立工作树。'));
 const env={...Object.fromEntries(Object.entries(process.env).filter(([key])=>!/^GIT_/i.test(key))),GIT_TERMINAL_PROMPT:'0'};
 return new Promise((resolve,reject)=>{
  // An existing executable is a file, not a directory in which hooks can live.
  const child=execFile(gitPath,['--no-pager','-c','core.fsmonitor=false','-c','core.hooksPath='+process.execPath,...args],
   {cwd,timeout,maxBuffer:4*1024*1024,env,windowsHide:true},(e,stdout,stderr)=>e?reject(Object.assign(new Error(stderr.trim()||e.message),{exitCode:e.code})):resolve(stdout));
  child.stdin?.on('error',()=>{});
  child.stdin?.end();
 });
}
const validId=id=>typeof id==='string'&&/^[-a-z0-9]{1,70}$/.test(id);
const validCommit=s=>/^[0-9a-f]{40,64}$/.test(s);
async function worktree(root,task,storage){
 if(!validId(task))throw new Error('会话 ID 无效');
 const base=await fs.realpath(storage),space=p.join(base,task),dir=p.join(space,'checkout'),manifest=p.join(space,'orbit.json');
 if(p.dirname(space)!==base)throw new Error('会话目录无效');
 const top=(await git(root,['rev-parse','--show-toplevel'])).trim();
 if(await fs.realpath(top)!==root)throw new Error('请先打开 Git 仓库根目录再创建工作树。');
 const baseRepo=(await git(root,['rev-parse','--path-format=absolute','--git-common-dir'])).trim();
 const common=await fs.realpath(baseRepo);
 const action=async()=>{try{await fs.mkdir(space,{mode:0o700});return true}catch(e){if(e.code==='EEXIST')return false;throw e}};
 if(!await action()){
  if(await fs.realpath(space)!==space)throw new Error('工作树目录无效');
  const meta=JSON.parse(await fs.readFile(manifest,'utf8'));
  if(meta.common!==common||!validCommit(meta.base)||await fs.realpath(dir)!==dir)throw new Error('工作树不属于这个项目');
  const got=(await git(dir,['rev-parse','--path-format=absolute','--git-common-dir'])).trim();
  const gotRoot=(await git(dir,['rev-parse','--show-toplevel'])).trim();
  if(await fs.realpath(got)!==common||await fs.realpath(gotRoot)!==dir)throw new Error('工作树校验失败');
  return {dir,base:meta.base,isolated:true};
 }
 // No checkout hooks, force, automatic commit, push or merge.
 let head;
 try{
  // Only new tasks need the source branch to have a commit. Existing tasks use their saved base.
  head=(await git(root,['rev-parse','--verify','HEAD^{commit}']).catch(e=>{throw new Error('当前分支需要至少有一个提交才能创建工作树',{cause:e})})).trim();
  if(!validCommit(head))throw new Error('无法识别工作树的起始提交');
  await git(root,['worktree','add','-b','orbit/'+task,dir,head],90000);
 }catch(e){await fs.rmdir(space).catch(()=>{});throw e}
 await fs.writeFile(manifest,JSON.stringify({common,base:head}),{mode:0o600,flag:'wx'});
 return {dir,base:head,isolated:true};
}
// Relative to the selected project (which can be a subfolder), not the Git top level.
const NO_ATTACHMENT_COPIES=':(exclude,literal).orbit-attachments';
async function review(cwd,base){
 const args=['-c','core.quotePath=false','diff','--no-ext-diff','--no-textconv','--no-color','--no-renames'];
 if(base&&!validCommit(base))throw new Error('无效的审阅基线');
 const [local,status,branch,patch]=await Promise.all([
  base?git(cwd,[...args,'--name-status',base,'--',NO_ATTACHMENT_COPIES]):Promise.resolve(''),
  git(cwd,['--no-optional-locks','-c','core.quotePath=false','status','--short','--untracked-files=normal','--no-renames','--',NO_ATTACHMENT_COPIES]),
  git(cwd,['rev-parse','--abbrev-ref','HEAD']).catch(()=> ''),
  git(cwd,[...args,base||'HEAD','--',NO_ATTACHMENT_COPIES]).catch(async error=>{
   if(base)throw error;
   const hasHead=await git(cwd,['rev-parse','--verify','--quiet','HEAD']).then(()=>true,e=>{if(e.exitCode!==1)throw error;return false});
   if(hasHead)throw error;
   // Use Git's empty tree for this repository's object format without writing it.
   const empty=(await git(cwd,['hash-object','-t','tree','--stdin'])).trim();
   if(!validCommit(empty))throw error;
   return git(cwd,[...args,empty,'--',NO_ATTACHMENT_COPIES]);
  })
 ]);
 const files=new Map(status.trimEnd().split('\n').filter(Boolean).map(s=>[s.slice(3),s]));
 for(const s of local.trimEnd().split('\n').filter(Boolean)){const [mode,file]=s.split('\t');if(file&&!files.has(file))files.set(file,mode.padEnd(2)+' '+file)}
 return {status:[...files.values()].join('\n'),patch,branch:branch.trim()};
}
async function exportPatch(cwd,base){
 if(!validCommit(base))throw new Error('没有有效的工作树基线。');
 const [patch,names]=await Promise.all([
  git(cwd,['diff','--no-ext-diff','--no-textconv','--binary','--no-color',base,'--','.',NO_ATTACHMENT_COPIES]),
  git(cwd,['ls-files','--others','--exclude-standard','-z','--','.',NO_ATTACHMENT_COPIES])
 ]);
 return {patch,untracked:names.split('\0').filter(Boolean).length};
}
module.exports={worktree,review,git,validId,exportPatch};
