function fromArg(n) {
    const a = [];
    for (let i = 0; i < n; i++) a[i] = i;
    return [a.length, a[0], a[n - 1]];
}
function fromLocal(n) {
    const limit = n - 3;
    const a = [];
    for (let i = 0; i < limit; i++) a[i] = i;
    return [a.length, a[0], a[limit - 1]];
}
function makeClosure(limit) {
    return function () {
        const a = [];
        for (let i = 0; i < limit; i++) a[i] = i;
        return [a.length, a[0], a[limit - 1]];
    };
}
const closure = makeClosure(1021);
const out = [];
for (let k = 0; k < 8; k++) {
    out.push(fromArg(1000 + k));
    out.push(fromLocal(1003 + k));
    out.push(closure());
}
const text = JSON.stringify(out);
if (typeof print === "function") print(text); else console.log(text);
