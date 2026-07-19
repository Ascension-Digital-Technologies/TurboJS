const emit = (typeof print === "function") ? print : console.log;
function sumDense(a) { let sum = 0; for (let i = 0; i < a.length; i++) { sum += a[i]; } return sum; }
let a = []; for (let i = 0; i < 1000000; i++) a[i] = i & 1023;
let total=0; for(let r=0;r<20;r++) total += sumDense(a); emit(total);
