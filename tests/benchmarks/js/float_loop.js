const emit = (typeof print === "function") ? print : console.log;
let x=1.000001; for(let i=0;i<5000000;i++) x=(x*1.0000001+0.000001)/(1.00000001); emit(x>0);
