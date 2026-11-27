const fs=require('node:fs/promises');
const {constants}=require('node:fs');
const path=require('node:path');
const os=require('node:os');
const {randomUUID}=require('node:crypto');
const MAX_BYTES=64*1024*1024,MAX_ENTRIES=1000;
const MAX_CLIPBOARD_IMAGE=16*1024*1024;
const PNG=Buffer.from([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a]);
function clipboardImage(mime,data){
 if(!(data instanceof Uint8Array))throw new Error('剪贴板图片数据无效。');
 if(data.byteLength>MAX_CLIPBOARD_IMAGE)throw new Error('剪贴板图片单张不能超过 16 MiB。');
 const bytes=Buffer.from(data);
 let ext;
 if(mime==='image/png'&&bytes.length>8&&bytes.subarray(0,8).equals(PNG))ext='png';
 if(mime==='image/jpeg'&&bytes.length>4&&bytes[0]===0xff&&bytes[1]===0xd8&&bytes[2]===0xff)ext='jpg';
 if(mime==='image/webp'&&bytes.length>16&&bytes.toString('ascii',0,4)==='RIFF'&&bytes.toString('ascii',8,12)==='WEBP')ext='webp';
 if(!ext)throw new Error('剪贴板图片格式无效。支持 PNG、JPEG 和 WebP。');
 return {name:'剪贴板图片.'+ext,kind:'file',size:bytes.length,bytes,clipboard:true};
}
const blocked=n=>/^(\.git|\.hg|\.svn|\.orbit-attachments|node_modules|\.DS_Store|\.ssh|\.aws|\.npmrc|\.netrc|\.env(?:\..*)?)$/i.test(n)||/\.(pem|p12|pfx|key)$/i.test(n);
async function describe(p){
 const name=path.basename(p);if(!name||blocked(name))throw new Error('不支持作为附件：'+name);
 const stat=await fs.lstat(p);if(stat.isSymbolicLink()||!stat.isDirectory()&&!stat.isFile())throw new Error('不支持符号链接或设备文件：'+name);
 return {name,kind:stat.isDirectory()?'folder':'file',size:stat.isFile()?stat.size:null};
}
async function registerPaths(files,registry){
 const pending=[];
 for(const file of files.slice(0,16)){
  if(typeof file!=='string'||!path.isAbsolute(file))continue;
  try{
   const real=await fs.realpath(file);
   if([path.parse(real).root,os.homedir()].includes(real))throw new Error('不能添加整个磁盘或主目录。');
   pending.push({file,details:await describe(file)});
  }catch(e){
   if(['ENOENT','ENOTDIR'].includes(e.code))throw new Error('找不到「'+path.basename(file)+'」。本次未添加附件，请重新选择。');
   if(['EACCES','EPERM','ELOOP'].includes(e.code))throw new Error('无法读取「'+path.basename(file)+'」。本次未添加附件，请重新选择。');
   throw e;
  }
 }
 // Register only after the whole selection is validated; rejected batches never leave hidden IDs.
 return pending.map(({file,details})=>{const id=randomUUID();registry.set(id,file);return {...details,id}});
}
async function stage(root,paths,{signal}={}){
 signal?.throwIfAborted();
 if(!paths.length)return [];
 const base=path.join(root,'.orbit-attachments');
 await fs.mkdir(base,{mode:0o700}).catch(e=>{if(e.code!=='EEXIST')throw e});
 if(!(await fs.lstat(base)).isDirectory()||await fs.realpath(base)!==base)throw new Error('附件目录不是普通目录');
 const batch=await fs.mkdtemp(path.join(base,'turn-'));let bytes=0,entries=0;
 try{
  const result=[];
  async function clone(from,to,skipped,depth=0){
   signal?.throwIfAborted();
   if(++entries>MAX_ENTRIES||depth>32)throw new Error('附件超过 1,000 个条目或目录嵌套过深。');
   const stat=await fs.lstat(from);
   if(stat.isSymbolicLink()||!stat.isDirectory()&&!stat.isFile()){skipped.push(path.basename(from));return}
   if(stat.isDirectory()){
    await fs.mkdir(to,{mode:0o700});
    // Enforce the entry limit without reading a potentially huge listing into memory.
    for await(const {name} of await fs.opendir(from)){
     if(entries>=MAX_ENTRIES)throw new Error('附件超过 1,000 个条目。');
     if(blocked(name)){entries++;skipped.push(name);continue}
     await clone(path.join(from,name),path.join(to,name),skipped,depth+1);
    }
   }else{
    const remaining=MAX_BYTES-bytes;
    if(stat.size>remaining)throw new Error('附件总大小超过 64 MiB。');
    const file=await fs.open(from,constants.O_RDONLY|constants.O_NOFOLLOW);
    try{
     const now=await file.stat();if(!now.isFile()||now.size>remaining)throw new Error('附件大小发生变化或不是普通文件。');
     const buffer=Buffer.alloc(now.size+1);let bytesRead=0;
     while(bytesRead<buffer.length){
      signal?.throwIfAborted();
      const chunk=await file.read(buffer,bytesRead,buffer.length-bytesRead,bytesRead);
      if(!chunk.bytesRead)break;bytesRead+=chunk.bytesRead;
     }
     const after=await file.stat();
     if(bytesRead!==now.size||after.mtimeMs!==now.mtimeMs||after.size!==now.size)throw new Error('附件在读取时发生变化，请重新添加。');
     signal?.throwIfAborted();
     bytes+=bytesRead;await fs.writeFile(to,buffer.subarray(0,bytesRead),{flag:'wx',mode:0o600,signal});
    }finally{await file.close()}
   }
  }
  for(const [i,from] of paths.entries()){
   signal?.throwIfAborted();
   const image=typeof from!=='string',d=image?{name:from.name,kind:from.kind,size:from.size,clipboard:true}:await describe(from);
   const parent=path.join(batch,String(i+1));await fs.mkdir(parent,{mode:0o700});
   const destination=path.join(parent,d.name),skipped=[];
   if(image){
    if(++entries>MAX_ENTRIES||from.bytes.length>MAX_BYTES-bytes)throw new Error('附件总大小或条目超过限制。');
    await fs.writeFile(destination,from.bytes,{flag:'wx',mode:0o600,signal});bytes+=from.bytes.length;
   }else await clone(await fs.realpath(from),destination,skipped);
   result.push({...d,path:destination,relative:path.relative(root,destination),skipped});
  }
  signal?.throwIfAborted();
  return result;
 }catch(e){await fs.rm(batch,{recursive:true,force:true});throw signal?.aborted?signal.reason:e}
}
module.exports={describe,stage,clipboardImage,registerPaths};
