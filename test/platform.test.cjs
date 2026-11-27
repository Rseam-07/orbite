const {test}=require('node:test');
const assert=require('node:assert/strict');
const {candidates}=require('../executables.cjs');
const {diagnose}=require('../computer-use.cjs');

test('Windows uses the official per-user CLI location and native executables only',()=>{
 const opts={platform:'win32',home:'C:\\Users\\Ada',env:{LOCALAPPDATA:'C:\\Users\\Ada\\AppData\\Local',Path:';"D:\\Tools and Apps";.;C:relative;\\root-relative',ProgramFiles:'C:\\Program Files'}};
 assert.deepEqual(candidates('agy',opts),['C:\\Users\\Ada\\AppData\\Local\\agy\\bin\\agy.exe','D:\\Tools and Apps\\agy.exe']);
 assert.deepEqual(candidates('git',opts),['C:\\Program Files\\Git\\cmd\\git.exe','D:\\Tools and Apps\\git.exe']);
});

test('POSIX uses the native installer location and checks only absolute PATH entries',()=>{
 assert.deepEqual(candidates('agy',{platform:'linux',home:'/home/ada',env:{PATH:':.:./bin:/some/bin'}}),['/home/ada/.local/bin/agy','/home/ada/.antigravity/bin/agy','/opt/homebrew/bin/agy','/usr/local/bin/agy','/usr/bin/agy','/some/bin/agy']);
});

test('unsupported Computer Use stops before running any supplied driver',async()=>{
 for(const platform of ['win32','linux']){
  const result=await diagnose(process.execPath,platform);
  assert.equal(result.found,false);
  assert.equal(result.mcp,null);
  assert.match(result.issues[0],/仅支持 macOS/);
 }
});
