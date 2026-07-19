function addMasked(n, mask) {
    let sum = 7;
    for (let i = 0; i < n; i++) {
        if (i & mask) sum = (sum + i) | 0;
        else sum = (sum - 2) | 0;
    }
    return sum;
}

function subtractMasked(n, mask) {
    let sum = -9;
    for (let i = 0; i < n; i++) {
        if (i & mask) sum = (sum - i) | 0;
        else sum = (sum + i) | 0;
    }
    return sum;
}

const result = [
    addMasked(1000003, 3),
    subtractMasked(777777, 5),
    addMasked(123456, 0)
];
const output = JSON.stringify(result);
if (typeof print === "function") print(output); else console.log(output);
