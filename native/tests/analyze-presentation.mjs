import fs from 'node:fs';
import path from 'node:path';
const run=process.argv[2];
if(!run) throw new Error('请提供测试目录');
function csv(file){const lines=fs.readFileSync(path.join(run,file),'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/);const keys=lines.shift().split(',');return lines.map(line=>Object.fromEntries(line.split(',').map((v,i)=>[keys[i],Number(v)])));}
const frames=csv('capture/frames.csv');
const events=csv('metrics.csv.events.csv').filter(e=>e.qpc_duration>0);
const source=fs.readFileSync(path.join(run,'capture/capture-source.txt'),'utf8');
// 动画时长会变化，使用测试启动器记录的实际 QPC 频率。
const frequency=JSON.parse(fs.readFileSync(path.join(run,'run.json'),'utf8').replace(/^\uFEFF/,'')).qpcFrequency;
if(!frequency)throw new Error('报告缺少实际 QPC 频率，不能计算呈现间隔');
const intervals=frames.slice(1).map((f,i)=>(f.qpc-frames[i].qpc)/frequency*1000).filter(t=>t>0).sort((a,b)=>a-b);
const median=intervals[Math.floor(intervals.length/2)];
let observedSlots=0,missingSlots=0,checked=0;
const repeated=[],reversals=[],largeGaps=[],coverage={};
for(const e of events){
 const middle=frames.filter(f=>f.qpc>=e.qpc_begin+.2*e.qpc_duration&&f.qpc<=e.qpc_begin+.8*e.qpc_duration&&f.border_pixels>2000);
 if(middle.length<3)continue;
 checked++;const key=`${e.anchor}/${e.effect}`;coverage[key]=(coverage[key]??0)+1;
 let plateauStart=null;
 for(let i=1;i<middle.length;i++){
  const previous=middle[i-1],current=middle[i];const gap=(current.qpc-previous.qpc)/frequency*1000;
  observedSlots++;missingSlots+=Math.max(0,Math.round(gap/median)-1);
  if(gap>50)largeGaps.push({begin:e.qpc_begin,gapMs:gap});
  if(current.width===previous.width&&current.height===previous.height){
   plateauStart??=previous.qpc;
   repeated.push({begin:e.qpc_begin,qpc:current.qpc,stillMs:(current.qpc-plateauStart)/frequency*1000});
  }else plateauStart=null;
  if((current.width-previous.width)*Math.sign(e.to_w-e.from_w)<-1||(current.height-previous.height)*Math.sign(e.to_h-e.from_h)<-1)reversals.push({begin:e.qpc_begin,qpc:current.qpc});
 }
}
const report={source,checkedTransitions:checked,coverage,medianCaptureIntervalMs:median,observedMiddleIntervals:observedSlots,estimatedUncapturedSlots:missingSlots,estimatedUncapturedPercent:100*missingSlots/(missingSlots+observedSlots),repeatedGeometrySamples:repeated.length,repeatedOver50ms:repeated.filter(r=>r.stillMs>50),directionReversals:reversals,captureGapsOver50ms:largeGaps,limitations:'丢失的采样时隙不能直接归因于应用掉帧；尺寸重复仅分析动画中间 60%，不替代照片内容和编码录像检查。'};
fs.writeFileSync(path.join(run,'presentation-summary.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify(report,null,2));
