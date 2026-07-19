const emit = typeof print === "function" ? print : console.log;
const argv = typeof execArgv !== "undefined" ? execArgv : process.argv;
const baseSeed = (Number(argv[argv.length - 1]) || 123456789) | 0;
const now = () => performance.now();

function median(xs) { xs.sort((a,b)=>a-b); return xs[(xs.length/2)|0]; }
function mix32(x) { x = Math.imul(x ^ (x >>> 16), 0x45d9f3b); x = Math.imul(x ^ (x >>> 16), 0x45d9f3b); return (x ^ (x >>> 16)) | 0; }
function rng(seed) { let x = seed|0; return function(){ x ^= x << 13; x ^= x >>> 17; x ^= x << 5; return x|0; }; }
function hashString(s) { let h=2166136261|0; for(let i=0;i<s.length;i++) h=Math.imul(h^s.charCodeAt(i),16777619); return h|0; }

function numericSimulation(seed) {
  const next=rng(seed), n=12000, a=new Float64Array(n), b=new Float64Array(n);
  for(let i=0;i<n;i++){ a[i]=((next()>>>0)%100000)*0.0001; b[i]=((next()>>>0)%10000)*0.00001+0.95; }
  let total=0;
  for(let r=0;r<12;r++) for(let i=0;i<n;i++){ const v=(a[i]*b[i]+(i&15)*0.0003)/(1.00001+(r&3)*0.000001); a[i]=v; total+=v; }
  return ((total*1000)|0) ^ ((a[(seed>>>3)%n]*1e6)|0);
}

function orderProcessing(seed) {
  const next=rng(seed), orders=[];
  for(let i=0;i<5000;i++) orders.push({id:i,customer:(next()>>>0)%700,sku:"sku-"+((next()>>>0)%300),qty:1+((next()>>>0)%7),price:((next()>>>0)%50000)/100,status:(i%5===0)?"hold":"open"});
  const totals={}, accepted=[];
  for(let r=0;r<3;r++) for(let i=0;i<orders.length;i++){
    const o=orders[i]; if(o.status!=="open") continue;
    const value=o.qty*o.price; totals[o.customer]=(totals[o.customer]||0)+value;
    if(value>40+(seed&31)) accepted.push({id:o.id,sku:o.sku,value,band:value>500?"high":"normal"});
  }
  const text=JSON.stringify({totals,accepted}); const copy=JSON.parse(text);
  return (text.length + copy.accepted.length + ((copy.totals[(seed>>>0)%700]||0)*100|0))|0;
}

function astPipeline(seed) {
  const next=rng(seed);
  function make(depth){ if(depth===0)return {type:"Literal",value:(next()>>>0)%97}; const t=(next()&3); return t===0?{type:"Neg",value:make(depth-1)}:t===1?{type:"Add",left:make(depth-1),right:make(depth-1)}:t===2?{type:"Mul",left:make(depth-1),right:make(depth-1)}:{type:"Sub",left:make(depth-1),right:make(depth-1)}; }
  const roots=[]; for(let i=0;i<18;i++) roots.push(make(7));
  const visitors={Literal(n){return n.value|0;},Neg(n){return (-walk(n.value))|0;},Add(n){return (walk(n.left)+walk(n.right))|0;},Sub(n){return (walk(n.left)-walk(n.right))|0;},Mul(n){return Math.imul(walk(n.left),walk(n.right))|0;}};
  function walk(n){return visitors[n.type](n);}
  let h=0; for(let r=0;r<35;r++) for(let i=0;i<roots.length;i++) h=mix32(h+walk(roots[(i+r)%roots.length]));
  return h|0;
}

function eventRouting(seed) {
  const next=rng(seed), handlers={};
  function on(name,fn){(handlers[name]||(handlers[name]=[])).push(fn);}
  on("data",x=>(x.value*3+x.id)|0); on("data",x=>(x.value^x.group)|0); on("error",x=>(x.id*17)|0); on("tick",x=>(x.value+x.group*11)|0);
  const events=[]; const names=["data","tick","data","error"];
  for(let i=0;i<30000;i++) events.push({type:names[(next()>>>0)&3],id:i,value:(next()>>>0)%10000,group:(next()>>>0)%31});
  let sum=0; for(let r=0;r<5;r++) for(let i=0;i<events.length;i++){const e=events[i],hs=handlers[e.type];for(let j=0;j<hs.length;j++)sum=(sum+hs[j](e))|0;}
  return sum|0;
}

function graphAnalytics(seed) {
  const next=rng(seed), n=3500, edges=new Array(n);
  for(let i=0;i<n;i++){const a=[];for(let j=0;j<4;j++)a.push((next()>>>0)%n);edges[i]=a;}
  const seen=new Uint8Array(n), queue=new Int32Array(n); let head=0,tail=0; queue[tail++]=(seed>>>0)%n; seen[queue[0]]=1; let score=0;
  while(head<tail){const v=queue[head++];score=(score+v)|0;const es=edges[v];for(let i=0;i<es.length;i++){const w=es[i];if(!seen[w]){seen[w]=1;queue[tail++]=w;}}}
  for(let r=0;r<15;r++) for(let i=0;i<n;i++){const es=edges[i];score=mix32(score+es[(r+i)&3]);}
  return (score^tail)|0;
}

function textIndexing(seed) {
  const next=rng(seed), levels=["INFO","WARN","ERROR","DEBUG"], lines=[];
  for(let i=0;i<10000;i++) lines.push(levels[(next()>>>0)&3]+" user="+((next()>>>0)%1200)+" code="+((next()>>>0)%900)+" latency="+((next()>>>0)%5000));
  const text=lines.join("\n"), re=/(INFO|WARN|ERROR|DEBUG) user=(\d+) code=(\d+) latency=(\d+)/g, counts={}, slow=[]; let m,total=0;
  while((m=re.exec(text))!==null){counts[m[1]]=(counts[m[1]]||0)+1;const latency=Number(m[4]);total+=latency;if(latency>4500)slow.push(m[2]+":"+m[3]);}
  slow.sort(); return (text.length+total+slow.length*17+hashString(JSON.stringify(counts))+hashString(slow.slice(0,20).join(",")))|0;
}

function collectionTransforms(seed) {
  const next=rng(seed), rows=[];
  for(let i=0;i<18000;i++) rows.push({id:i,group:(next()>>>0)%53,score:(next()>>>0)%10000,active:(next()&7)!==0});
  const selected=rows.filter(x=>x.active&&x.score>((seed>>>0)%3000));
  const mapped=selected.map(x=>({id:x.id,group:x.group,score:(x.score*3+x.group)|0}));
  mapped.sort((a,b)=>b.score-a.score||a.id-b.id);
  const totals=mapped.reduce((a,x)=>{a[x.group]=(a[x.group]||0)+x.score;return a;},{});
  return (mapped.length+(mapped[0]?mapped[0].id:0)+hashString(JSON.stringify(totals)))|0;
}

function stateMachine(seed) {
  const next=rng(seed); let state="idle", balance=0, accepted=0, rejected=0;
  for(let r=0;r<18;r++) for(let i=0;i<12000;i++){
    const op=(next()>>>0)%7, amount=(next()>>>0)%1000;
    if(state==="idle"){if(op<3)state="open";else rejected++;}
    else if(state==="open"){if(op===0){state="closed";accepted++;}else if(op<5){balance=(balance+amount-(op===4?amount>>1:0))|0;accepted++;}else rejected++;}
    else {if(op===6)state="idle";else rejected++;}
  }
  return mix32(balance+accepted*31+rejected*17+state.length);
}

function configAndTemplates(seed) {
  const next=rng(seed), base={env:"prod",features:{jit:true,cache:true,trace:false},limits:{heap:64,workers:4},prefix:"app"}; let h=0;
  for(let i=0;i<6000;i++){
    const ov={features:{jit:true,cache:(next()&1)===0,trace:(next()&31)===0},limits:{heap:64+((next()>>>0)&63),workers:1+((next()>>>0)&7)},region:"r"+((next()>>>0)%12)};
    const cfg={env:base.env,features:{jit:ov.features.jit,cache:ov.features.cache,trace:ov.features.trace},limits:{heap:ov.limits.heap,workers:ov.limits.workers},name:base.prefix+"-"+ov.region+"-"+i};
    const html="<section data-env=\""+cfg.env+"\"><h2>"+cfg.name+"</h2><span>"+cfg.limits.heap+":"+cfg.limits.workers+":"+(cfg.features.cache?1:0)+"</span></section>";
    h=mix32(h+hashString(html)+hashString(JSON.stringify(cfg)));
  }
  return h|0;
}

function allocationLifecycle(seed) {
  const next=rng(seed); let checksum=0, retained=[];
  for(let r=0;r<25;r++){
    const batch=[]; for(let i=0;i<3500;i++) batch.push({id:i,key:"k"+((next()>>>0)%5000),values:[next()&255,next()&255,next()&255],meta:{round:r,flag:(next()&1)===0}});
    for(let i=0;i<batch.length;i+=7){const x=batch[i];checksum=mix32(checksum+x.id+x.values[1]+hashString(x.key));}
    const start=((seed+r)>>>0)%19; retained=batch.slice(start,start+120); if(typeof gc==="function"&&(r%8)===7)gc();
  }
  return (checksum+retained.length+retained[0].meta.round)|0;
}

const specs=[
  ["numeric_simulation",numericSimulation], ["order_processing",orderProcessing], ["ast_pipeline",astPipeline],
  ["event_routing",eventRouting], ["graph_analytics",graphAnalytics], ["text_indexing",textIndexing],
  ["collection_transforms",collectionTransforms], ["state_machine",stateMachine], ["config_templates",configAndTemplates],
  ["allocation_lifecycle",allocationLifecycle]
];

function runWorkload(name,fn,index){
  for(let i=0;i<2;i++) fn(mix32(baseSeed+index*0x9e3779b9+i*101));
  const samples=[]; let checksum=0;
  for(let i=0;i<7;i++){const seed=mix32(baseSeed+index*0x9e3779b9+i*10007);const t0=now();const value=fn(seed);samples.push(now()-t0);checksum=mix32(checksum+value+seed);}
  return {name,median_ms:median(samples),min_ms:samples[0],max_ms:samples[samples.length-1],checksum};
}
const results=[];for(let i=0;i<specs.length;i++)results.push(runWorkload(specs[i][0],specs[i][1],i));
emit(JSON.stringify({suite:"TurboJS whole-engine parity v1",seed:baseSeed,warmups:2,runs:7,results}));
