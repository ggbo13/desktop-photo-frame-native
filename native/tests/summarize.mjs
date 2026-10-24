import fs from 'node:fs';
import path from 'node:path';
const root=path.resolve(import.meta.dirname,'../..');
function csv(file){const lines=fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/);const cells=s=>s.split(',').map(x=>x.replace(/^"|"$/g,''));const keys=cells(lines.shift());return lines.map(line=>Object.fromEntries(cells(line).map((v,i)=>[keys[i],Number.isNaN(Number(v))?v:Number(v)])));}
const mean=a=>a.reduce((x,y)=>x+y,0)/a.length;
const mib=n=>Math.round(n/1048576*100)/100;
const folder=path.join(root,'native/artifacts');
const soak=csv(path.join(folder,'soak-20260923-003329/memory.csv'));
const samples=csv(path.join(folder,'soak-20260923-003329/metrics.csv'));
const baseline=csv(path.join(folder,'electron-baseline-20260923-003531/memory.csv')).slice(3);
const start=Date.parse(soak[0].Time);
const early=soak.filter(r=>Date.parse(r.Time)-start>=300000&&Date.parse(r.Time)-start<600000);
const late=soak.filter(r=>Date.parse(r.Time)-start>=1500000);
const result={
  status:'第一阶段，视觉矩阵尚未完成；不能交付正式版',
  durationSeconds:samples.at(-1).seconds,transitions:samples.at(-1).transitions,decodeErrors:samples.at(-1).decode_errors,
  privateWorkingSetMiB:{nativeAverage:mib(mean(soak.slice(3).map(r=>r.PrivateWorkingSet))),nativeLast:mib(soak.at(-1).PrivateWorkingSet),electronAverage:mib(mean(baseline.map(r=>r.PrivateWorkingSet)))},
  privateCommitMiB:{early:mib(mean(early.map(r=>r.PrivateBytes))),late:mib(mean(late.map(r=>r.PrivateBytes)))},
  gpuMiB:{localLast:mib(samples.at(-1).gpu_local_bytes),nonlocalLast:mib(samples.at(-1).gpu_nonlocal_bytes)},
  notes:['Electron 为整个测试进程树；原生为单进程。测试工具不计入应用占用。','私有工作集与提交量、GPU 内存分别报告，不相加伪装为同一指标。','长测验证内存稳定性，不证明锁屏或遮挡时画面的实际呈现。','私有工作集会受 Windows 自然换页影响，提交量用于同时观察增长。']
};
result.reductionPercent=Math.round((1-result.privateWorkingSetMiB.nativeAverage/result.privateWorkingSetMiB.electronAverage)*1000)/10;
result.commitGrowthPercent=Math.round((result.privateCommitMiB.late/result.privateCommitMiB.early-1)*1000)/10;
const output=path.join(folder,'stage1-memory-summary.json');fs.writeFileSync(output,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
