function kernel(n, split, mask) {
    let a = 7;
    let b = -11;
    for (let i = 0; i < n; i++) {
        if (i & mask) a = (a + i) | 0;
        else a = (a - 3) | 0;
        if (i < split) b = (b + 5) | 0;
        else b = (b - 2) | 0;
    }
    return (a ^ b) | 0;
}
let results = [];
for (let mask = 0; mask < 8; mask++) {
    for (let splitIndex = 0; splitIndex < 8; splitIndex++) {
        const n = 20000 + mask * 137;
        const split = ((n + 1) * splitIndex / 7) | 0;
        results.push(kernel(n, split, mask));
    }
}
const output = JSON.stringify(results);
if (typeof print === "function") print(output); else console.log(output);
