import fs from 'node:fs';
import path from 'node:path';
const run=process.argv[2];
function csv(file){const lines=fs.readFileSync(path.join(run,file),'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/);const keys=lines.shift().split(',');return lines.map(line=>Object.fromEntries(line.split(',').map((value,i)=>[keys[i],Number(value)])));}
const frames=csv('capture/frames.csv'),events=csv('metrics.csv.events.csv').filter(e=>e.qpc_duration>0),coverage={},failures=[],insufficient=[];
let checked=0,index=0;
for(const event of events){
 while(index<frames.length&&frames[index].qpc<event.qpc_begin)index++;
 const valid=[];let total=0;
 for(let i=index;i<frames.length&&frames[i].qpc<=event.qpc_begin+event.qpc_duration;i++){total++;if(frames[i].border_pixels>2000)valid.push(frames[i]);}
 if(valid.length<12||valid.length<total*.9){insufficient.push({begin:event.qpc_begin,total,valid:valid.length});continue;}
 const xs=valid.map(f=>f.x+(event.anchor%2?f.width:0)),ys=valid.map(f=>f.y+(event.anchor>=2?f.height:0));
 const dx=Math.max(...xs)-Math.min(...xs),dy=Math.max(...ys)-Math.min(...ys);checked++;const key=`${event.anchor}/${event.effect}`;coverage[key]=(coverage[key]??0)+1;
 if(dx>1||dy>1)failures.push({begin:event.qpc_begin,anchor:event.anchor,effect:event.effect,dx,dy});
}
const report={checkedTransitions:checked,coverage,cornerFailures:failures,insufficientTransitions:insufficient,passed:checked>=200&&Object.keys(coverage).length===12&&failures.length===0&&insufficient.length<=2,limitations:'工作区内渲染像素尺度测试；不代表真实系统 DPI 或跨屏目视验收。采样缺失或遮挡不得作为通过。'};
fs.writeFileSync(path.join(run,'geometry-summary.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
