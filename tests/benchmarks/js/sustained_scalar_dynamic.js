const emit = (typeof print === "function") ? print : console.log;

function intKernel(n) {
    let s = 0;
    for (let i = 0; i < n; i++) s = (s + i) | 0;
    return s;
}

function floatKernel(n) {
    let x = 1.25;
    for (let i = 0; i < n; i++) x = (x * 1.000001 + 0.25) / 1.0000005;
    return x;
}

function multiKernel(n) {
    let sum = 0;
    let bias = 1;
    let debt = 2;
    for (let i = 0; i < n; i++) {
        sum = (sum + i) | 0;
        bias = (bias + 2) | 0;
        debt = (debt - 3) | 0;
    }
    return sum + bias + debt;
}

function leaf(a, b) { return (a + b) * 3 - 7; }
function callKernel(n) {
    let s = 0;
    for (let i = 0; i < n; i++) s += leaf(i, 2);
    return s;
}

const warmups = 5;
const runs = 15;
const intN = 10000000;
const floatN = 1000000;
const callN = 5000000;
for (let i = 0; i < warmups; i++) {
    intKernel(intN);
    floatKernel(floatN);
    multiKernel(intN);
    callKernel(callN);
}

function measure(fn, n) {
    const samples = [];
    let checksum = 0;
    for (let i = 0; i < runs; i++) {
        const start = performance.now();
        checksum += fn(n);
        samples.push(performance.now() - start);
    }
    samples.sort((a, b) => a - b);
    return {
        median_ms: samples[(samples.length / 2) | 0],
        min_ms: samples[0],
        max_ms: samples[samples.length - 1],
        checksum
    };
}

emit(JSON.stringify({
    runs,
    warmups,
    int32_dynamic_bound: measure(intKernel, intN),
    float64_dynamic_bound: measure(floatKernel, floatN),
    multi_accumulator_int32: measure(multiKernel, intN),
    affine_call_dynamic_bound: measure(callKernel, callN)
}));
