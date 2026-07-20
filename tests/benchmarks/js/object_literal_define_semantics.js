const a = { x: 1, y: 2, z: 3, w: 4, q: 5 };
const duplicate = { x: 1, x: 2 };
const proto = { set x(v) { throw new Error("prototype setter must not run"); } };
const own = { __proto__: proto, x: 7 };
if (a.x + a.y + a.z + a.w + a.q !== 15 || duplicate.x !== 2 || own.x !== 7)
  throw new Error("object literal field semantics regression");
print("ok");
