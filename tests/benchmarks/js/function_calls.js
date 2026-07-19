const emit = (typeof print === "function") ? print : console.log;
function f(a,b){return (a+b)*3-7;} let s=0; for(let i=0;i<5000000;i++) s+=f(i,2); emit(s);
