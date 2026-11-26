const fs=require('node:fs/promises');
const {constants}=require('node:fs');
const path=require('node:path');
const MAX_BYTES=64*1024*1024,MAX_ENTRIES=1000;
const blocked=n=>/^(\.git|\.hg|\.svn|\.orbit-attachments|node_modules|\.DS_Store|\.ssh|\.aws|\.npmrc|\.netrc|\.env(?:\..*)?)$/i.test(n)||/\.(pem|p12|pfx|key)$/i.test(n);
async function describe(p){
 const name=path.basename(p);if(!name||blocked(name))throw new Error('不支持作为附件：'+name);
 const stat=await fs.lstat(p);if(stat.isSymbolicLink()||!stat.isDirectory()&&!stat.isFile())throw new Error('不支持符号链接或设备文件：'+name);
 return {name,kind:stat.isDirectory()?'folder':'file',size:stat.isFile()?stat.size:null};
}
async function stage(root,paths){
 if(!paths.length)return [];
 const base=path.join(root,'.orbit-attachments');
 await fs.mkdir(base,{mode:0o700}).catch(e=>{if(e.code!=='EEXIST')throw e});
 if(!(await fs.lstat(base)).isDirectory()||await fs.realpath(base)!==base)throw new Error('附件目录不是普通目录');
 const batch=await fs.mkdtemp(path.join(base,'turn-'));let bytes=0,entries=0;
 try{
  const result=[];
  async function clone(from,to,skipped,depth=0){
   if(++entries>MAX_ENTRIES||depth>32)throw new Error('附件超过 1,000 个条目或目录嵌套过深。');
   const stat=await fs.lstat(from);
   if(stat.isSymbolicLink()||!stat.isDirectory()&&!stat.isFile()){skipped.push(path.basename(from));return}
   if(stat.isDirectory()){
    await fs.mkdir(to,{mode:0o700});
    for(const e of await fs.readdir(from)){
     if(++entries>MAX_ENTRIES)throw new Error('附件超过 1,000 个条目。');
     if(blocked(e)){skipped.push(e);continue}
     entries--;await clone(path.join(from,e),path.join(to,e),skipped,depth+1);
    }
   }else{
    const remaining=MAX_BYTES-bytes;
    if(stat.size>remaining)throw new Error('附件总大小超过 64 MiB。');
    const file=await fs.open(from,constants.O_RDONLY|constants.O_NOFOLLOW);
    try{
     const now=await file.stat();if(!now.isFile()||now.size>remaining)throw new Error('附件大小发生变化或不是普通文件。');
     const buffer=Buffer.alloc(now.size+1);let bytesRead=0;
     while(bytesRead<buffer.length){
      const chunk=await file.read(buffer,bytesRead,buffer.length-bytesRead,bytesRead);
      if(!chunk.bytesRead)break;bytesRead+=chunk.bytesRead;
     }
     const after=await file.stat();
     if(bytesRead!==now.size||after.mtimeMs!==now.mtimeMs||after.size!==now.size)throw new Error('附件在读取时发生变化，请重新添加。');
     bytes+=bytesRead;await fs.writeFile(to,buffer.subarray(0,bytesRead),{flag:'wx',mode:0o600});
    }finally{await file.close()}
   }
  }
  for(const [i,from] of paths.entries()){
   const d=await describe(from);const abs=await fs.realpath(from);
   const parent=path.join(batch,String(i+1));await fs.mkdir(parent,{mode:0o700});
   const destination=path.join(parent,d.name),skipped=[];
   await clone(abs,destination,skipped);
   result.push({...d,path:destination,relative:path.relative(root,destination),skipped});
  }
  return result;
 }catch(e){await fs.rm(batch,{recursive:true,force:true});throw e}
}
module.exports={describe,stage};
