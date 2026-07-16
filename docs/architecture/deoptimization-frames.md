# Native deoptimization frames

TurboJS baseline native code records a conservative machine-state snapshot whenever a checked operation bails out.

## Snapshot contents

`TurboJS_NativeLastDeoptFrame()` exposes:

- the structured bailout reason;
- the failing IR instruction index;
- the originating engine bytecode offset;
- virtual-register and local counts;
- materialization masks;
- captured register values;
- captured local values.

The snapshot storage belongs to the compiled function and remains valid until the next invocation or destruction of that function.

## Native ABI

The private native entry point receives a deoptimization buffer. A cold bailout block writes the failing instruction index and copies stack-backed virtual registers and locals into that buffer before returning a structured status code. Normal successful execution does not perform the copy.

## Materialization policy

The current baseline compiler records conservative forward materialization masks. A register is marked after an instruction defines it, and a local is marked after a `local.set`. Consumers must inspect these masks before using captured values.

This is sufficient for reconstructing straight-line baseline frames and is the foundation for later control-flow-aware liveness, boxed `JSValue` reconstruction, and exact interpreter re-entry.

## Remaining work

Exact bytecode resumption still requires:

1. control-flow-aware live-value analysis;
2. operand-stack maps;
3. boxed/unboxed value-kind metadata;
4. interpreter frame creation from a snapshot;
5. resumption after the failing bytecode without repeating prior side effects.
