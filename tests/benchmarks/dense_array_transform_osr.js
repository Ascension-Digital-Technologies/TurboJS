const n = 1000000;
const input = [];
const output = [];
for (let i = 0; i < n; i++) { input[i] = i * 0.5; output[i] = 0; }
function transform(input, output, scale, offset) {
  for (let i = 0; i < input.length; i++) output[i] = input[i] * scale + offset;
  return output[0] + output[input.length - 1];
}
let check = 0;
for (let r = 0; r < 20; r++) check += transform(input, output, 1.25 + r * 0.001, 3.5);
if (typeof print === 'function') print(check); else console.log(check);
