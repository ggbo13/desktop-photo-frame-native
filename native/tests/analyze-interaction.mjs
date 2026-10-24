import fs from 'node:fs';
import path from 'node:path';
const run=process.argv[2];
const read=name=>fs.readFileSync(path.join(run,name),'utf8').replace(/^\uFEFF/,'');
function csv(name){const lines=read(name).trim().split(/\r?\n/);const keys=lines.shift().split(',');return lines.map(line=>Object.fromEntries(line.split(',').map((v,i)=>[keys[i],Number(v)])));}
const actions=JSON.parse(read('actions.json')),frames=csv('capture/frames.csv'),events=csv('metrics.csv.events.csv');
const pause=actions.find(a=>a.name==='pause'),resume=actions.find(a=>a.name==='resume');
const event=events.findLast(e=>e.qpc_begin<=pause.qpc&&pause.qpc<e.qpc_begin+e.qpc_duration);
const valid=frames.filter(f=>f.border_pixels>2000);
const before=valid.findLast(f=>f.qpc<=pause.qpc);
const held=valid.filter(f=>f.qpc>pause.qpc+.15*pause.frequency&&f.qpc<resume.qpc-.1*pause.frequency);
const ranges=Object.fromEntries(['x','y','width','height'].map(k=>[k,held.length?Math.max(...held.map(f=>f[k]))-Math.min(...held.map(f=>f[k])):null]));
const fixed=f=>({x:f.x+(event.anchor%2?f.width:0),y:f.y+(event.anchor>=2?f.height:0)});
const report={interruptedActiveAnimation:!!event,heldSamples:held.length,heldRanges:ranges,stable:held.length>=10&&Object.values(ranges).every(r=>r<=1),noReverseJump:null,fixedCornerDrift:null,limitations:'系统帧采样验证暂停中断；隐藏恢复和快速选图的消息回归另见 summary.json。'};
if(event&&before&&held.length){
 const after=held[0];const a=fixed(before),b=fixed(after);
 report.fixedCornerDrift={x:Math.abs(a.x-b.x),y:Math.abs(a.y-b.y)};
 report.noReverseJump=(after.width-before.width)*Math.sign(event.w1-event.w0)>=-1&&(after.height-before.height)*Math.sign(event.h1-event.h0)>=-1;
 report.before={x:before.x,y:before.y,width:before.width,height:before.height};
 report.after={x:after.x,y:after.y,width:after.width,height:after.height};
}
fs.writeFileSync(path.join(run,'interaction-visual-summary.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify(report,null,2));
