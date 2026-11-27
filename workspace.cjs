const {execFile}=require('node:child_process');
const fs=require('node:fs/promises');
const p=require('node:path');
function git(cwd,args,timeout=10000){
 return new Promise((resolve,reject)=>execFile('/usr/bin/git',['--no-pager','-c','core.fsmonitor=false','-c','core.hooksPath=/dev/null',...args],{cwd,timeout,maxBuffer:4*1024*1024,env:{...Object.fromEntries(Object.entries(process.env).filter(([k])=>!k.startsWith('GIT_'))),GIT_TERMINAL_PROMPT:'0'}},(e,stdout,stderr)=>e?reject(Object.assign(new Error(stderr.trim()||e.message),{exitCode:e.code})):resolve(stdout)));
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
// Git flushes the same ordered diff queue as NUL-delimited raw records and then
// as patch blocks. Check every header against its exact raw path before pairing.
function quotedDiffPath(prefix,file){
 const escapes={'\x07':'\\a','\b':'\\b','\t':'\\t','\n':'\\n','\x0b':'\\v','\f':'\\f','\r':'\\r','"':'\\"','\\':'\\\\'};
 const name=prefix+file;
 return /[\x00-\x1f\x7f"\\]/.test(name)?'"'+name.replace(/[\x00-\x1f\x7f"\\]/g,c=>escapes[c]??'\\'+c.charCodeAt(0).toString(8).padStart(3,'0'))+'"':name;
}
function reviewDiff(output){
 if(!output)return [];
 const boundary=output.indexOf('\0\0');
 if(boundary<0)throw new Error('无法可靠匹配 Git 补丁与文件。');
 const fields=output.slice(0,boundary).split('\0');
 const patches=output.slice(boundary+2).split(/(?<=\n)(?=diff --git )/);
 const bad=()=>new Error('无法可靠匹配 Git 补丁与文件。');
 if(fields.length!==patches.length*2)throw bad();
 const seen=new Set();
 return patches.map((patch,i)=>{
  const raw=fields[i*2].match(/^:[0-7]{6} [0-7]{6} [0-9a-f]+ [0-9a-f]+ ([A-Z])$/),path=fields[i*2+1];
  const header=patch.slice(0,patch.indexOf('\n'));
  if(!raw||!path||seen.has(path)||header!=='diff --git '+quotedDiffPath('a/',path)+' '+quotedDiffPath('b/',path))throw bad();
  seen.add(path);return {path,status:raw[1],patch};
 });
}
async function review(cwd,base){
 const args=['--no-optional-locks','-c','core.quotePath=false','diff','--raw','-z','--patch','--no-ext-diff','--no-textconv',
  '--no-color','--no-renames','--no-relative','--submodule=short','--src-prefix=a/','--dst-prefix=b/'];
 if(base&&!validCommit(base))throw new Error('无效的审阅基线');
 const results=await Promise.allSettled([
  git(cwd,['--no-optional-locks','status','--porcelain=v1','-z','--untracked-files=normal','--no-renames','--',NO_ATTACHMENT_COPIES]),
  git(cwd,['rev-parse','--abbrev-ref','HEAD']).catch(()=> ''),
  git(cwd,['rev-parse','--show-prefix']),
  git(cwd,[...args,base||'HEAD','--',NO_ATTACHMENT_COPIES]).catch(async error=>{
   if(base)throw error;
   const hasHead=await git(cwd,['rev-parse','--verify','--quiet','HEAD']).then(()=>true,e=>{if(e.exitCode!==1)throw error;return false});
   if(hasHead)throw error;
   // Read Git's empty tree for this repository's object format without writing it.
   const empty=(await git(cwd,['hash-object','-t','tree','/dev/null'])).trim();
   if(!validCommit(empty))throw error;
   return git(cwd,[...args,empty,'--',NO_ATTACHMENT_COPIES]);
  })
 ]);
 const failure=results.find(result=>result.status==='rejected');if(failure)throw failure.reason;
 const [status,branch,prefix,output]=results.map(result=>result.value);
 const files=new Map(),baseDir=prefix.replace(/\r?\n$/,'')||'.';
 for(const record of status.split('\0')){
  const path=record.slice(3);
  if(path)files.set(path,{path:p.posix.relative(baseDir,path)+(path.endsWith('/')?'/':''),status:record.slice(0,2).trim()});
 }
 for(const entry of reviewDiff(output)){
  const previous=files.get(entry.path);
  if(previous)previous.patch=entry.patch;
  else if(base)files.set(entry.path,{...entry,path:p.posix.relative(baseDir,entry.path)});
 }
 return {files:[...files.values()],branch:branch.trim()};
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
