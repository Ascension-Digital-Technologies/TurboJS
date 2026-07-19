let n = 1000000;
let a = new Float64Array(n);
let b = new Float64Array(n);
let out = new Float64Array(n);
for (let i = 0; i < n; i++) {
    a[i] = i + 0.5;
    b[i] = i * 2 + 0.25;
}
function add(x, y, z) {
    for (let i = 0; i < x.length; i++) {
        z[i] = x[i] + y[i];
    }
}
for (let r = 0; r < 20; r++) add(a, b, out);
if (typeof print === "function") print(out[n - 1]);
else console.log(out[n - 1]);
