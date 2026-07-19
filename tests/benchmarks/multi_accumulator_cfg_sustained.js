function kernel(n, split) {
    let sum = 1;
    let bias = 2;
    for (let i = 0; i < n; i++) {
        if (i & 1)
            sum = (sum + i) | 0;
        else
            sum = (sum - 1) | 0;
        if (i < split)
            bias = (bias + 3) | 0;
        else
            bias = (bias - 2) | 0;
    }
    return (sum + bias) | 0;
}
for (let k = 0; k < 5; k++) kernel(10000000, 4000000);
let samples = [], result = 0;
for (let k = 0; k < 15; k++) {
    const start = performance.now();
    result = kernel(10000000, 4000000);
    samples.push(performance.now() - start);
}
samples.sort((a, b) => a - b);
const output = JSON.stringify({ result, median: samples[7], min: samples[0], max: samples[14] });
if (typeof print === "function") print(output); else console.log(output);
