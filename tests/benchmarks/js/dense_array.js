const emit = (typeof print === "function") ? print : console.log;
let a=[]; for(let i=0;i<1000000;i++) a[i]=i; let s=0; for(let r=0;r<5;r++) for(let i=0;i<a.length;i++) s+=a[i]; emit(s);
