function kernel(n, lo, hi, pivot) {
    let sum = 0;
    for (let i = 0; i < n; i++) {
        if (i <= lo) sum = (sum + 1) | 0; else sum = (sum - 1) | 0;
        if (i > hi) sum = (sum + 2) | 0; else sum = (sum - 2) | 0;
        if (i >= pivot) sum = (sum + 3) | 0; else sum = (sum - 3) | 0;
        if (i != pivot) sum = (sum + 4) | 0; else sum = (sum - 4) | 0;
    }
    return sum;
}
const n = 10000000, lo = 2000000, hi = 7000000, pivot = 5000000;
for (let k = 0; k < 5; k++) kernel(n, lo, hi, pivot);
let samples = [], result = 0;
for (let k = 0; k < 15; k++) {
    const start = performance.now();
    result = kernel(n, lo, hi, pivot);
    samples.push(performance.now() - start);
}
samples.sort((a, b) => a - b);
const output = JSON.stringify({ result, median: samples[7], min: samples[0], max: samples[14], samples });
if (typeof print === "function") print(output); else console.log(output);
