function kernel(n) {
    const values = [];
    for (let i = 0; i < n; i++)
        values[i] = i;
    return (values.length + values[0] + values[n - 1]) | 0;
}
for (let k = 0; k < 5; k++) kernel(250000);
let samples = [], result = 0;
for (let k = 0; k < 15; k++) {
    const start = performance.now();
    result = kernel(250000);
    samples.push(performance.now() - start);
}
samples.sort((a, b) => a - b);
const output = JSON.stringify({ result, median: samples[7], min: samples[0], max: samples[14] });
if (typeof print === "function") print(output); else console.log(output);
