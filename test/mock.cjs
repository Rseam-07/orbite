const {createInterface}=require('node:readline');
let last,auth=false,promptId,promptText;const out=x=>console.log(JSON.stringify({jsonrpc:'2.0',...x}));
const res=(id,result)=>out({id,result}),notify=(method,params)=>out({method,params});
const settings=model=>[{id:'model',type:'select',category:'model',name:'模型',currentValue:model,options:[{value:'flash',name:'Flash'},{value:'pro',name:'Pro'}]},{id:'mode',type:'select',category:'mode',name:'模式',currentValue:'plan',options:[{value:'plan',name:'Plan'},{value:'bypassPermissions',name:'Bypass'}]}];
createInterface({input:process.stdin}).on('line',l=>{
 const {method,id,params,result}=JSON.parse(l);
 if(method==='initialize')res(id,{protocolVersion:1,agentCapabilities:{loadSession:true},authMethods:[{id:'oauth-personal',name:'Google'}]});
 else if(method==='authenticate'){auth=params.methodId==='oauth-personal';res(id,{})}
 else if(method==='session/new'||method==='session/load'){
  last=params;if(!auth)out({id,error:{code:-32000,message:'Authentication required'}});else res(id,{sessionId:'a1',configOptions:settings('flash')});
 }else if(method==='fixture/last_session')res(id,last);
 else if(method==='session/set_config_option')res(id,{configOptions:settings(params.value)});
 else if(method==='session/prompt'){
  const text=params.prompt[0].text;
  if(text==='supplement'){res(id,{stopReason:'end_turn'});res(promptId,{stopReason:'end_turn'});return}
  promptId=id;promptText=text;notify('session/update',{sessionId:'a1',update:{sessionUpdate:'agent_message_chunk',content:{type:'text',text:'ok'}}});
  if(text==='with-diff'){
   notify('session/update',{sessionId:'a1',update:{sessionUpdate:'tool_call_update',status:'completed',toolCallId:'x',content:[{type:'diff',path:require('node:path').join(process.cwd(),'demo.swift'),oldText:'old',newText:'new'},{type:'diff',path:'/private-outside',oldText:'do not log',newText:'do not log'}]}});
   res(id,{stopReason:'end_turn'});return;
  }
  if(!['cancel','background','unstoppable'].includes(text))out({id:'approval-test',method:'session/request_permission',params:{sessionId:'a1',toolCall:{title:'读取项目',rawInput:{path:'README.md'}},options:[{optionId:'once',kind:'allow_once'},{optionId:'deny',kind:'reject_once'},{optionId:'always',kind:'allow_always'}]}});
 }else if(method==='session/cancel'){if(promptText!=='unstoppable')res(promptId,{stopReason:'cancelled'});}
 else if(id==='approval-test')res(promptId,{stopReason:result?.outcome?.optionId==='once'?'end_turn':'refusal'});
});
