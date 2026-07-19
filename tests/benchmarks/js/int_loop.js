const emit = (typeof print === "function") ? print : console.log;
let s=0; for(let i=0;i<10000000;i++) s=(s+i)|0; emit(s);
