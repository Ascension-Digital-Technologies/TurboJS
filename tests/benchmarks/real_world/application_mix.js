const emit = typeof print === "function" ? print : console.log;
const now = () => performance.now();
function median(xs) { xs.sort((a,b)=>a-b); return xs[(xs.length/2)|0]; }
function hashString(s) { let h=2166136261|0; for(let i=0;i<s.length;i++) h=Math.imul(h^s.charCodeAt(i),16777619); return h|0; }

function add(x){ return (x+3)|0; }
function mul(x){ return Math.imul(x,5)|0; }
function xor(x){ return (x^0x5a5a5a5a)|0; }
function routeCallbacks(rounds) {
  const callbacks=[add,mul];
  let s=0;
  for(let r=0;r<rounds;r++) {
    for(let i=0;i<20000;i++) s=(s+callbacks[i&1](i+r))|0;
  }
  callbacks[1]=xor;
  for(let i=0;i<5000;i++) s=(s+callbacks[i&1](i))|0;
  return s|0;
}

function buildTree(depth, seed) {
  if(depth===0) return {type:"Literal",value:seed};
  return {type:(depth&1)?"Add":"Mul",left:buildTree(depth-1,seed+1),right:buildTree(depth-1,seed+3)};
}
function visitTree(root) {
  const visitors={
    Literal(n){ return n.value|0; },
    Add(n){ return (walk(n.left)+walk(n.right))|0; },
    Mul(n){ return Math.imul(walk(n.left),walk(n.right))|0; }
  };
  function walk(node){ return visitors[node.type](node); }
  let s=0;
  for(let i=0;i<80;i++) s=(s+walk(root))|0;
  return s|0;
}

function transformRecords(count) {
  const rows=[];
  for(let i=0;i<count;i++) rows.push({id:i,group:i%23,active:(i&3)!==0,score:(i*17)%997,name:"user-"+i});
  const totals={};
  const output=[];
  for(let i=0;i<rows.length;i++) {
    const row=rows[i];
    if(!row.active) continue;
    const score=(row.score+row.group*7)|0;
    totals[row.group]=(totals[row.group]||0)+score;
    output.push({id:row.id,label:row.name+":"+score,score});
  }
  const json=JSON.stringify({totals,output});
  const copy=JSON.parse(json);
  return (copy.output.length+json.length+copy.totals[7])|0;
}

function mergeConfigs(rounds) {
  const base={mode:"prod",retry:3,flags:{jit:true,osr:true,trace:false},limits:{heap:64,stack:2}};
  let h=0;
  for(let i=0;i<rounds;i++) {
    const override={retry:(i%7)+1,flags:{jit:true,osr:(i&1)===0,trace:(i%11)===0},limits:{heap:64+(i&15),stack:2}};
    const merged={mode:base.mode,retry:override.retry,flags:{jit:override.flags.jit,osr:override.flags.osr,trace:override.flags.trace},limits:{heap:override.limits.heap,stack:override.limits.stack}};
    h=(h+hashString(JSON.stringify(merged)))|0;
  }
  return h|0;
}

function workload() {
  const tree=buildTree(7,3);
  let result=0;
  result=(result+routeCallbacks(8))|0;
  result=(result+visitTree(tree))|0;
  result=(result+transformRecords(5000))|0;
  result=(result+mergeConfigs(400))|0;
  return result|0;
}

for(let i=0;i<3;i++) workload();
const samples=[]; let checksum=0;
for(let i=0;i<9;i++){ const t0=now(); checksum=workload(); samples.push(now()-t0); }
emit(JSON.stringify({suite:"TurboJS real-world application mix v1",median_ms:median(samples),min_ms:samples[0],max_ms:samples[samples.length-1],checksum}));
