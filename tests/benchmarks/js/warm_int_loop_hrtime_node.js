function benchmark(repetitions) {
  let checksum = 0;
  for (let r = 0; r < repetitions; r++) {
    let sum = 0;
    for (let i = 0; i < 1000000; i++) sum += i;
    checksum += sum;
  }
  return checksum;
}
const runs = Number(process.argv[2] || 15);
const repetitions = Number(process.argv[3] || 5);
const warmups = Number(process.argv[4] || 3);
for (let i = 0; i < warmups; i++) benchmark(repetitions);
const samples = [];
let checksum = 0;
for (let i = 0; i < runs; i++) {
  const t0 = process.hrtime.bigint();
  checksum = benchmark(repetitions);
  samples.push(Number(process.hrtime.bigint() - t0));
}
samples.sort((a,b)=>a-b);
console.log(JSON.stringify({engine:`Node ${process.version}`,clock:'hrtime_bigint_ns',runs,warmups,repetitions,median_ns:samples[Math.floor(samples.length/2)],min_ns:samples[0],max_ns:samples[samples.length-1],checksum},null,2));
