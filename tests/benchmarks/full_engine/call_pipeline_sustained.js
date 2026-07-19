const emit = typeof print === 'function' ? print : console.log;
const now = () => performance.now();
function median(a){a.sort((x,y)=>x-y);return a[(a.length/2)|0];}
function measure(name,fn,reps){let checksum=0;function batch(){let x=0;for(let i=0;i<reps;i++)x+=fn();return x;}for(let i=0;i<3;i++)checksum=batch();const s=[];for(let i=0;i<7;i++){const t=now();checksum=batch();s.push(now()-t);}return {name,median_ms:median(s),checksum};}
function leafA(x){return (x*3+7)|0;} function leafB(x){return (x^0x5a5a5a5a)|0;}
function mono(){let s=0;for(let i=0;i<200000;i++)s=(s+leafA(i))|0;return s;}
function poly(){let s=0;for(let i=0;i<6000;i++){const f=(i&1)?leafA:leafB;s=(s+f(i))|0;}return s;}
function closures(){let total=0;function make(k){return function(x){return (x+k)|0;};}const fs=[make(1),make(3),make(7),make(11)];for(let i=0;i<6000;i++)total=(total+fs[i&3](i))|0;return total;}
function recursion(){function fib(n){return n<2?n:fib(n-1)+fib(n-2);}let s=0;for(let i=0;i<300;i++)s+=fib(18+(i&1));return s;}
const results=[measure('calls_monomorphic',mono,10),measure('calls_polymorphic',poly,20),measure('closures',closures,15),measure('recursion',recursion,1)];
emit(JSON.stringify({results}));
