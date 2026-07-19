const emit = (typeof print === "function") ? print : console.log;
let s=''; for(let i=0;i<100000;i++) s += String.fromCharCode(97+(i%26)); emit(s.length);
