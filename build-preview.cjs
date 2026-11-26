const fs=require('node:fs');
const {createHash}=require('node:crypto');
const js=fs.readFileSync('ui.js','utf8'),motion=fs.readFileSync('motion.js','utf8'),css=fs.readFileSync('style.css','utf8');
if([js,motion].some(s=>/<\/script/i.test(s))||/<\/style/i.test(css))throw new Error('unsafe preview source');
const hash=x=>"'sha256-"+createHash('sha256').update(x).digest('base64')+"'";
const html=fs.readFileSync('index.html','utf8').replace("script-src 'self'; style-src 'self';","script-src "+hash(js)+' '+hash(motion)+"; style-src "+hash(css)+";").replace('<link rel="stylesheet" href="style.css"><script defer src="motion.js"></script><script defer src="ui.js"></script>','<style>'+css+'</style>').replace('</body>','<script>'+motion+'</script><script>'+js+'</script></body>');
fs.writeFileSync('Orbit-Preview.html',html);
