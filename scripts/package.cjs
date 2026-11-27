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
async function checksum(archive){
 const digest=createHash('sha256');
 for await(const chunk of createReadStream(archive))digest.update(chunk);
 await fs.writeFile(archive+'.sha256',digest.digest('hex')+'  '+path.basename(archive)+'\n');
}
async function deb(bundle,archive,input){
 const tree=path.join(input,'debian'),install=path.join(tree,'opt','orbite');
 await fs.mkdir(path.dirname(install),{recursive:true});
 await fs.cp(bundle,install,{recursive:true,verbatimSymlinks:true});
 // Electron Packager stages its top directory as 0700; the system package must be traversable.
 await fs.chmod(install,0o755);
 // Chromium's fallback sandbox must be root-owned and setuid on distros
 // that restrict unprivileged user namespaces. Do not disable sandboxing.
 await fs.chmod(path.join(install,'chrome-sandbox'),0o4755);
 const files={
  'DEBIAN/control':[
   'Package: orbite','Version: '+meta.version,'Architecture: amd64','Section: devel','Priority: optional',
   'Maintainer: Orbite maintainers <140034002+Rseam-07@users.noreply.github.com>',
   'Depends: libgtk-3-0t64 | libgtk-3-0, libasound2t64 | libasound2, libnss3, libxss1, libgbm1, libnotify4, libxtst6, xdg-utils',
   'Description: Independent desktop client for Google Antigravity',''
  ].join('\n'),
  'usr/share/applications/com.github.rseam07.orbite.desktop':[
   '[Desktop Entry]','Name=Orbite','Exec=/opt/orbite/Orbite','Icon=com.github.rseam07.orbite','Type=Application','Terminal=false','Categories=Development;',''
  ].join('\n'),
  'usr/share/icons/hicolor/scalable/apps/com.github.rseam07.orbite.svg':await fs.readFile(path.join(root,'assets','orbit-logo.svg'))
 };
 for(const [name,content] of Object.entries(files)){
  const file=path.join(tree,name);
  await fs.mkdir(path.dirname(file),{recursive:true});await fs.writeFile(file,content);
 }
 exec('dpkg-deb',['--root-owner-group','--build',tree,archive]);
}
async function main(){
 const [platform=process.platform,arch=process.arch,...flags]=process.argv.slice(2);
 if(platform!==process.platform||!targets[platform]?.includes(arch)||arch!==process.arch||flags.some(x=>x!=='--smoke')||(platform==='linux'&&flags.length))throw new Error('Use a native macOS arm64/x64, Windows x64 or Linux x64 runner. Linux smoke tests the installed .deb in CI.');
 const input=await fs.mkdtemp(path.join(os.tmpdir(),'orbite-package-'));
 try{
  for(const file of appFiles){
   await fs.mkdir(path.dirname(path.join(input,file)),{recursive:true});
   await fs.copyFile(path.join(root,file),path.join(input,file));
  }
  // Stage only named runtime files, never local attachments, env files, or credentials.
  const {name,productName,version,description,main,license}=meta;
  await fs.writeFile(path.join(input,'package.json'),JSON.stringify({name,productName,version,description,main,license,author:'Orbite maintainers'}));
  const [bundle]=await packager({dir:input,out:path.join(root,'dist'),name:productName,platform,arch,
   electronVersion:meta.devDependencies.electron,appBundleId:meta.config.appId,appVersion:version,overwrite:true,prune:false,asar:true});
  const artifacts=path.join(root,'dist','artifacts');
  await fs.mkdir(artifacts,{recursive:true});
  const folder=path.basename(bundle),suffix=platform==='linux'?'.deb':'.zip';
  const target='Orbite-'+version+'-'+platform+'-'+arch+suffix,archive=path.join(artifacts,target);
  await fs.rm(archive,{force:true});
  if(platform==='darwin')exec('/usr/bin/ditto',['-c','-k','--keepParent',path.join(bundle,'Orbite.app'),archive]);
  else if(platform==='win32')exec('tar.exe',['-a','-cf',archive,'-C',path.dirname(bundle),folder]);
  else await deb(bundle,archive,input);

  if(flags.includes('--smoke')){
   // Test the unpacked, distributable artifact, not the working tree.
   const extracted=path.join(input,'test');
   await fs.mkdir(extracted);
   if(platform==='darwin')exec('/usr/bin/ditto',['-x','-k',archive,extracted]);
   else exec('tar.exe',['-xf',archive,'-C',extracted]);
   const exe=platform==='darwin'?path.join(extracted,'Orbite.app','Contents','MacOS','Orbite'):path.join(extracted,folder,platform==='win32'?'Orbite.exe':'Orbite');
   const env={...process.env,ELECTRON_ENABLE_LOGGING:'1'};
   delete env.ELECTRON_RUN_AS_NODE;
   exec(exe,['--orbite-package-smoke'],{timeout:30000,env});
  }
  await checksum(archive);
  console.log(archive);
 }finally{await fs.rm(input,{recursive:true,force:true,maxRetries:3,retryDelay:200})}
}
main().catch(e=>{console.error(e);process.exitCode=1});
