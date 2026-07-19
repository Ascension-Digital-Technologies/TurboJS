function kernel(n) {
    let a = 1;
    let b = 2;
    for (let i = 0; i < n; i++) {
        a = (a + i) | 0;
        b = (b + a) | 0;
    }
    return (a + b) | 0;
}
for (let i = 0; i < 5; i++) kernel(1000000);
let times=[]; let result=0;
for (let r=0;r<15;r++) {
  const t0=performance.now(); result=kernel(1000000); const t1=performance.now();
  times.push(t1-t0);
}
times.sort((a,b)=>a-b);
console.log(JSON.stringify({result, median:times[7], min:times[0], max:times[14]}));
