#!/usr/bin/env node
const {createInterface}=require('node:readline');
const cmd=process.argv.slice(2).join(' ');
if(cmd==='--version'){console.log('cua-driver 0.40.1');process.exit(0)}
if(cmd==='permissions status --json'){console.log(JSON.stringify({accessibility:true,screen_recording:false,screen_recording_capturable:null,source:{attribution:'driver-daemon',responsible_executable:'/private/no-display'}}));process.exit(0)}
if(cmd==='test --json'){console.log(JSON.stringify({result:'fixture only'}));process.exit(0)}
if(cmd==='permissions grant'){process.exit(1)}
if(['missing','mcp','no-permission-tool','ambiguous-permission-tool'].includes(cmd)){
 createInterface({input:process.stdin}).on('line',line=>{
  const m=JSON.parse(line);
  const tool=name=>({name,inputSchema:{type:'object'}});
  const send=result=>console.log(JSON.stringify({jsonrpc:'2.0',id:m.id,result}));
  if(m.method==='initialize')send({protocolVersion:'2025-06-18',capabilities:{tools:{}},serverInfo:{name:'cua-driver'}});
  else if(m.method==='tools/list'){
   if(cmd==='missing'){send({tools:['list_windows'].map(tool)});return}
   if(!m.params.cursor)send({tools:['list_windows','get_window_state'].map(tool),nextCursor:'more'});
   else {
    const tools=['click','type_text'].map(tool);
    if(cmd==='mcp')tools.push({name:'check_permissions',inputSchema:{type:'object',properties:{prompt:{type:'boolean'}}}});
    if(cmd==='ambiguous-permission-tool')tools.push(tool('check_permissions'));
    send({tools});
   }
  }
 });
}
