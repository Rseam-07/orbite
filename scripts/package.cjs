const {execFileSync}=require('node:child_process');
const fs=require('node:fs/promises');
const os=require('node:os');
const path=require('node:path');
const {createHash}=require('node:crypto');
const {createReadStream}=require('node:fs');
const {packager}=require('@electron/packager');
const meta=require('../package.json');
const root=path.join(__dirname,'..');
const appFiles=['package.json','LICENSE','main.cjs','preload.cjs','attachments.cjs','computer-use.cjs','core.cjs','executables.cjs','transport.cjs','workspace.cjs','index.html','motion.js','style.css','ui.js','assets/orbit-logo.svg'];
const targets={darwin:['arm64','x64'],win32:['x64'],linux:['x64']};

function exec(bin,args,opts={}){return execFileSync(bin,args,{stdio:'inherit',...opts})}
async function main(){
 const [platform=process.platform,arch=process.arch,...flags]=process.argv.slice(2);
 if(platform!==process.platform||!targets[platform]?.includes(arch)||arch!==process.arch||flags.some(x=>x!=='--smoke'))throw new Error('Package on a native macOS arm64/x64, Windows x64 or Linux x64 runner.');
 const input=await fs.mkdtemp(path.join(os.tmpdir(),'orbite-package-'));
 try{
  for(const file of appFiles){
   await fs.mkdir(path.dirname(path.join(input,file)),{recursive:true});
   await fs.copyFile(path.join(root,file),path.join(input,file));
  }
  // Stage only named runtime files, never local attachments, env files, or credentials.
  const {name,productName,version,description,main,license}=meta;
  await fs.writeFile(path.join(input,'package.json'),JSON.stringify({name,productName,version,description,main,license}));
  const [bundle]=await packager({dir:input,out:path.join(root,'dist'),name:productName,platform,arch,
   electronVersion:meta.devDependencies.electron,appBundleId:meta.config.appId,appVersion:version,overwrite:true,prune:false,asar:true});
  const artifacts=path.join(root,'dist','artifacts');
  await fs.mkdir(artifacts,{recursive:true});
  const folder=path.basename(bundle),suffix=platform==='linux'?'.tar.gz':'.zip';
  const target='Orbite-'+version+'-'+platform+'-'+arch+suffix,archive=path.join(artifacts,target);
  await fs.rm(archive,{force:true});
  if(platform==='darwin')exec('/usr/bin/ditto',['-c','-k','--keepParent',path.join(bundle,'Orbite.app'),archive]);
  else if(platform==='win32')exec('tar.exe',['-a','-cf',archive,'-C',path.dirname(bundle),folder]);
  else exec('tar',['-czf',archive,'-C',path.dirname(bundle),folder]);

  if(flags.includes('--smoke')){
   // Test the unpacked, distributable artifact, not the working tree.
   const extracted=path.join(input,'test');
   await fs.mkdir(extracted);
   if(platform==='darwin')exec('/usr/bin/ditto',['-x','-k',archive,extracted]);
   else exec(platform==='win32'?'tar.exe':'tar',['-xf',archive,'-C',extracted]);
   const exe=platform==='darwin'?path.join(extracted,'Orbite.app','Contents','MacOS','Orbite'):path.join(extracted,folder,platform==='win32'?'Orbite.exe':'Orbite');
   const env={...process.env,ELECTRON_ENABLE_LOGGING:'1'};
   delete env.ELECTRON_RUN_AS_NODE;
   exec(exe,['--orbite-package-smoke'],{timeout:30000,env});
  }
  const digest=createHash('sha256');
  for await(const chunk of createReadStream(archive))digest.update(chunk);
  await fs.writeFile(archive+'.sha256',digest.digest('hex')+'  '+target+'\n');
  console.log(archive);
 }finally{await fs.rm(input,{recursive:true,force:true,maxRetries:3,retryDelay:200})}
}
main().catch(e=>{console.error(e);process.exitCode=1});
