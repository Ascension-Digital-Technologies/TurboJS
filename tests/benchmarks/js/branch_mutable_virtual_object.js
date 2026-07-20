function calculate(x) {
    const state = {
        a: (x + 1) | 0,
        b: (x * 2) | 0
    };

    if ((x & 1) === 0)
        state.a = (state.a + 7) | 0;
    else
        state.b = (state.b - 3) | 0;

    return (state.a + state.b) | 0;
}

let checksum = 0;
for (let i = 0; i < 2000000; i++)
    checksum = (checksum + calculate(i & 1023)) | 0;

console.log(checksum);
