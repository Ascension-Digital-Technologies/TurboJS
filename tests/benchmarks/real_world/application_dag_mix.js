const emit = typeof print === "function" ? print : console.log;
const now = () => performance.now();
function median(xs){ xs.sort((a,b)=>a-b); return xs[(xs.length/2)|0]; }
function buildSharedDAG(depth, seed) {
  let node = {type:"Literal", value:seed};
  for (let i=0;i<depth;i++) node = {type:"Add", left:node, right:node};
  return node;
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
const graph=buildSharedDAG(13,3);
for(let i=0;i<1;i++) visitTree(graph);
const samples=[]; let checksum=0;
for(let i=0;i<5;i++){ const t0=now(); checksum=visitTree(graph); samples.push(now()-t0); }
emit(JSON.stringify({suite:"TurboJS shared DAG visitor v1",median_ms:median(samples),min_ms:samples[0],max_ms:samples[samples.length-1],checksum}));
