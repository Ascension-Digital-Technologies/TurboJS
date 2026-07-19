const emit = typeof print === "function" ? print : console.log;
const now = () => performance.now();

function median(xs) { xs.sort((a,b)=>a-b); return xs[(xs.length/2)|0]; }
function measure(name, fn, repetitions, warmups, runs) {
  let checksum = 0;
  function batch() {
    let value = 0;
    for (let j=0;j<repetitions;j++) value += fn();
    return value;
  }
  for (let i=0;i<warmups;i++) checksum = batch();
  const samples=[];
  for (let i=0;i<runs;i++) { const t0=now(); checksum = batch(); samples.push(now()-t0); }
  return {name, repetitions, median_ms:median(samples), min_ms:samples[0], max_ms:samples[samples.length-1], checksum};
}

function integer_mix() {
  let x=0x12345678|0;
  for (let i=0;i<60000;i++) {
    x = Math.imul(x ^ i, 1664525) + 1013904223 | 0;
    x ^= x >>> 13;
  }
  return x|0;
}
function float_numeric() {
  let x=1.000001, y=0.25;
  for (let i=0;i<6000;i++) { x=(x*1.0000003+y)/(1.0000001+(i&7)*1e-8); y=(y+x*1e-7)%3.0; }
  return +x;
}
function leafA(x){return (x*3+7)|0;} function leafB(x){return (x^0x5a5a5a5a)|0;}
function calls_monomorphic(){let s=0;for(let i=0;i<200000;i++)s=(s+leafA(i))|0;return s;}
function calls_polymorphic(){let s=0;for(let i=0;i<6000;i++){const f=(i&1)?leafA:leafB;s=(s+f(i))|0;}return s;}
function closures(){let total=0;function make(k){return function(x){return (x+k)|0;};}const fs=[make(1),make(3),make(7),make(11)];for(let i=0;i<6000;i++)total=(total+fs[i&3](i))|0;return total;}
function object_access(){const objs=[];for(let i=0;i<6000;i++)objs.push({x:i,y:i+1,z:i+2});let s=0;for(let r=0;r<8;r++)for(let i=0;i<objs.length;i++){const o=objs[i];o.x=(o.x+o.y-r)|0;s=(s+o.x+o.z)|0;}return s;}
function object_polymorphic(){const a=[];for(let i=0;i<6000;i++){if(i%3===0)a.push({x:i,y:1});else if(i%3===1)a.push({y:2,x:i,z:3});else a.push({x:i,w:4});}let s=0;for(let r=0;r<5;r++)for(let i=0;i<a.length;i++)s=(s+a[i].x)|0;return s;}
function dense_arrays(){const a=[];for(let i=0;i<12000;i++)a.push((i*3)^7);let s=0;for(let r=0;r<5;r++)for(let i=0;i<a.length;i++)s=(s+a[i])|0;return s;}
function holey_arrays(){const a=new Array(12000);for(let i=0;i<a.length;i+=3)a[i]=i;let s=0;for(let r=0;r<4;r++)for(let i=0;i<a.length;i++)if(a[i]!==undefined)s=(s+a[i])|0;return s;}
function typed_arrays(){const a=new Float64Array(60000);for(let i=0;i<a.length;i++)a[i]=i*0.25;let s=0;for(let r=0;r<6;r++)for(let i=0;i<a.length;i++){a[i]=a[i]*1.000001+0.5;s+=a[i];}return s;}
function string_ops(){let s="";for(let i=0;i<3000;i++)s += String(i&255)+":";let h=0;for(let r=0;r<8;r++){const p=s.indexOf("127:",r*100);h=(h+p+s.slice(p,p+12).length)|0;}return h+s.length;}
function json_ops(){const data=[];for(let i=0;i<2000;i++)data.push({id:i,name:"item"+i,ok:(i&1)===0,v:[i,i+1,i+2]});let sum=0;for(let r=0;r<5;r++){const text=JSON.stringify(data);const copy=JSON.parse(text);sum=(sum+text.length+copy[copy.length-1].id)|0;}return sum;}
function regexp_ops(){const text=("alpha-123 beta_456 gamma789 ").repeat(3000);const re=/[a-z]+[-_]?[0-9]+/g;let n=0,m;while((m=re.exec(text))!==null)n+=m[0].length;return n;}
function array_sort(){const a=[];let x=123456789;for(let i=0;i<6000;i++){x=(Math.imul(x,1103515245)+12345)|0;a.push(x);}a.sort((p,q)=>p-q);return (a[0]^a[a.length-1]^a[70000])|0;}
function exceptions(){let s=0;for(let i=0;i<12000;i++){try{if((i&31)===0)throw i;s+=i&7;}catch(e){s+=e&255;}}return s|0;}
function recursion(){function fib(n){return n<2?n:fib(n-1)+fib(n-2);}let s=0;for(let i=0;i<300;i++)s+=fib(18+(i&1));return s;}
function allocation_gc_pressure(){let sum=0;for(let r=0;r<8;r++){const rows=[];for(let i=0;i<2000;i++)rows.push({a:i,b:"v"+i,c:[i,i+1]});sum=(sum+rows[rows.length-1].c[1])|0;}return sum;}
function mini_application(){const users=[];for(let i=0;i<6000;i++)users.push({id:i,group:i%17,score:(i*13)%1000,name:"u"+i});const totals={};for(let r=0;r<8;r++){for(let i=0;i<users.length;i++){const u=users[i];u.score=(u.score+u.group+r)%1000;totals[u.group]=(totals[u.group]||0)+u.score;}}const text=JSON.stringify(totals);const out=JSON.parse(text);let s=text.length;for(const k in out)s=(s+out[k])|0;return s;}

const specs=[
 ["integer_mix",integer_mix,20], ["float_numeric",float_numeric,20], ["calls_monomorphic",calls_monomorphic,10],
 ["calls_polymorphic",calls_polymorphic,20], ["closures",closures,15], ["object_access",object_access,4],
 ["object_polymorphic",object_polymorphic,4], ["dense_arrays",dense_arrays,10], ["holey_arrays",holey_arrays,20],
 ["typed_arrays",typed_arrays,4], ["string_ops",string_ops,20], ["json_ops",json_ops,1], ["regexp_ops",regexp_ops,4],
 ["array_sort",array_sort,3], ["exceptions",exceptions,8], ["recursion",recursion,1],
 ["allocation_gc_pressure",allocation_gc_pressure,2], ["mini_application",mini_application,4]
];
const results=[];
for (const [name,fn,repetitions] of specs) {
  const row=measure(name,fn,repetitions,3,9);
  results.push(row);
  emit("BENCH "+name+" "+row.median_ms+" "+row.checksum);
}
emit(JSON.stringify({suite:"TurboJS full-engine sustained v2",warmups:3,runs:9,results}));
