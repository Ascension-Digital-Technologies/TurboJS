function kernel(n) {
    let sum = 0;
    for (let i = 0; i < n; i++) {
        if (i < 5000000)
            sum = (sum + i) | 0;
        else
            sum = (sum - 3) | 0;
    }
    return sum;
}
for (let k = 0; k < 5; k++) kernel(10000000);
let samples = [], result = 0;
for (let k = 0; k < 15; k++) {
    const start = performance.now();
    result = kernel(10000000);
    samples.push(performance.now() - start);
}
samples.sort((a, b) => a - b);
console.log(JSON.stringify({ result, median: samples[7], min: samples[0], max: samples[14] }));
