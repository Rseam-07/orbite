const fs=require('node:fs');
const path=require('node:path');
const os=require('node:os');

function executable(candidate){
 try{
  if(!path.isAbsolute(candidate)||!fs.statSync(candidate).isFile())return null;
  fs.accessSync(candidate,fs.constants.X_OK);
  return fs.realpathSync(candidate);
 }catch{return null}
}

// Resolve a native executable by absolute path; do not ask a shell (or use
// Windows' current-directory executable search from a selected workspace).
function candidates(tool,{platform=process.platform,env=process.env,home=os.homedir()}={}){
 const win=platform==='win32',p=win?path.win32:path.posix;
 const envPath=Object.entries(env).find(([key])=>key.toUpperCase()==='PATH')?.[1]??'';
 const fromPath=envPath.split(p.delimiter).map(dir=>win?dir.replace(/^"(.*)"$/,'$1'):dir);
 const defaults=win?tool==='agy'
  ?[...(env.LOCALAPPDATA?[p.join(env.LOCALAPPDATA,'agy','bin')]:[]),p.join(home,'AppData','Local','agy','bin')]
  :[env.ProgramW6432,env.ProgramFiles,env['ProgramFiles(x86)']].filter(dir=>dir&&p.isAbsolute(dir)).map(dir=>p.join(dir,'Git','cmd'))
  :tool==='agy'?[p.join(home,'.local/bin'),p.join(home,'.antigravity/bin'),'/opt/homebrew/bin','/usr/local/bin','/usr/bin']
  :['/usr/bin','/opt/homebrew/bin','/usr/local/bin','/bin'];
 const file=tool+(win?'.exe':'');
 return [...new Set([...defaults,...fromPath].filter(dir=>p.isAbsolute(dir)&&(!win||/^[a-z]:[\\/]/i.test(dir))).map(dir=>p.join(dir,file)))];
}

function findExecutable(tool,preferred){
 if(preferred){const chosen=executable(preferred);if(chosen)return chosen}
 for(const file of candidates(tool)){const found=executable(file);if(found)return found}
 return null;
}

module.exports={executable,candidates,findExecutable};
