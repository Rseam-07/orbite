const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');

test('session review keeps the exact path through row selection, hunk rendering and feedback',()=>{
 const source=readFileSync(path.join(__dirname,'..','ui.js'),'utf8');
 const start=source.indexOf('function agentPatch'),end=source.indexOf('async function refresh()',start);
 assert.ok(start>=0&&end>start,'load the actual renderer functions');
 const fixtures=[
  ['archive b/report.txt','nested change'],
  ['report.txt','root change'],
  ['notes -> report.txt','literal arrow change'],
  ['目录/摘要\n@@ 续行.txt','literal newline change']
 ];
 const state={diffMode:'task',taskDiff:new Map(fixtures.map(([path,after])=>[path,{before:'old fixture',after}])),reviewSelection:new Map()};
 const nodes=new Map(),draft={text:''};
 const create=(tag,text,className)=>({
  tag,text,className,children:[],dataset:{},options:[{}],
  append(...children){this.children.push(...children)},
  replaceChildren(...children){this.children=children},
  setAttribute(){},contains(){return false},focus(){},setSelectionRange(){}
 });
 const $=id=>{if(!nodes.has(id))nodes.set(id,create(id));return nodes.get(id)};
 vm.runInNewContext(source.slice(start,end)+'\nrenderReview()',{
  $,el:create,icon:()=>create('svg'),runtime:()=>state,current:null,draft:()=>draft,document:{activeElement:null}
 });
 for(const [path,marker] of fixtures.slice(1).concat([fixtures[0]])){
  const row=$('changedFiles').children.find(r=>r.dataset.path===path);
  assert.ok(row,path);
  row.onclick();
  const [title,scroller]=$('patch').children[0].children;
  assert.equal(title.children[0].text,path);
  assert.deepEqual(scroller.children.filter(x=>x.className==='patch-line plus').map(x=>x.text),['+'+marker]);
  draft.text='';
  title.children[1].onclick();
  assert.equal($('prompt').value,'关于文件 '+JSON.stringify(path)+'：');
 }
 assert.equal($('changedFiles').children.length,fixtures.length);
});
