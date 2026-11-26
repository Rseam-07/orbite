const {createInterface}=require('node:readline');
const mode=process.argv[2];
createInterface({input:process.stdin}).on('line',raw=>{
 const p=JSON.parse(raw);
 if(mode==='exit'){process.exit(10);return}
 if(mode==='overflow'){process.stdout.write(Buffer.alloc(512,65));return}
 if(mode==='malformed'){process.stdout.write('{oops\n');return}
 if(mode==='stall')return;
 const data=Buffer.from(JSON.stringify({id:p.id,result:'中文 · 🌟'})+'\r\n');
 const i=data.indexOf(Buffer.from('中'))+1;
 process.stdout.write(data.subarray(0,i));setTimeout(()=>process.stdout.write(data.subarray(i)),20);
});
