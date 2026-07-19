const emit=(typeof print==='function')?print:console.log;
let o={x:1,y:2,z:3,w:4,v:5}; let s=0; for(let i=0;i<5000000;i++){o.x=i; s+=o.x+o.y+o.z+o.w+o.v;} emit(s);
