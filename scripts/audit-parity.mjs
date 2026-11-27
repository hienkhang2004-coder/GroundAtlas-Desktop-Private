import {readFile,writeFile,mkdir,readdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),web=path.resolve(root,'../groundatlas-v2');
const manifest=JSON.parse(await readFile(path.join(root,'parity/features.json'),'utf8'));
const files=[];
async function walk(dir,prefix=''){for(const entry of await readdir(dir,{withFileTypes:true})){const name=prefix+entry.name;if(entry.isDirectory()){if(!['vendor','snapshots','environment-data','weather-data','models'].includes(entry.name))await walk(path.join(dir,entry.name),name+'/');}else if(/\.(?:js|mjs|html|css)$/.test(name)){const bytes=await readFile(path.join(dir,entry.name));files.push({file:name,sha256:createHash('sha256').update(bytes).digest('hex'),bytes:bytes.length});}}}
await walk(path.join(web,'public'));files.sort((a,b)=>a.file.localeCompare(b.file));
const missing=[];for(const group of manifest.groups)for(const feature of group.features){if(!['pending','partial','implemented','verified','web-unavailable'].includes(feature.status))throw Error('Invalid status '+feature.id);for(const ref of feature.web){try{await readFile(path.join(web,'public',ref));}catch{missing.push({feature:feature.id,file:ref});}}}
if(missing.length)throw Error('Missing web reference files: '+JSON.stringify(missing));
const statuses={};for(const group of manifest.groups)for(const f of group.features)statuses[f.status]=(statuses[f.status]||0)+1;
await mkdir(path.join(root,'parity'),{recursive:true});const report={schema:1,baseline:'GroundAtlas local web source 2.5.0',baselineDate:'2026-10-27',timezone:'Asia/Bangkok',publicSourceRoot:'../groundatlas-v2/public',featureCount:Object.values(statuses).reduce((a,b)=>a+b,0),statuses,complete:manifest.groups.every(g=>g.features.every(f=>['verified','web-unavailable'].includes(f.status))),sourceHash:createHash('sha256').update(JSON.stringify(files)).digest('hex'),files};
await writeFile(path.join(root,'parity/baseline.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({features:report.featureCount,statuses,complete:report.complete,firstPartyFiles:files.length,sourceHash:report.sourceHash},null,2));
