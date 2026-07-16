# Engine Bytecode Tiering

TurboJS Phase 4 connects the existing engine bytecode format to the standalone
baseline JIT without exposing private runtime structures through the public API.

## Pipeline

```text
JSFunctionBytecode.byte_code_buf
        |
        v
TurboJS_EngineBytecodeToIR
        |
        +-- supported integer-safe stack operations --> verified register IR
        |
        `-- dynamic/unsupported operation -----------> interpreter fallback
        |
        v
TurboJS_TieredInvoke
        |
        +-- cold function --> IR reference interpreter
        `-- hot function  --> baseline compiler --> code cache --> native call
```

## Supported engine opcodes

The first direct bridge recognizes optimized bytecode sequences containing:

- `push_i32`
- `get_arg` and `get_arg0` through `get_arg3`
- `add`
- `sub`
- `mul`
- `lt`
- `return`
- `nop`

These operations are only lowered as signed 64-bit numeric operations. The
runtime must use the bridge only after proving or guarding that incoming
`JSValue` arguments satisfy the integer fast-path contract.

Every unsupported opcode returns `TURBOJS_IR_UNSUPPORTED` with its byte offset.
This is a normal tiering result, not a fatal engine error.

## Code cache ownership

`TurboJSCodeCache` owns compiled native functions and executable memory. It:

- keys entries by an embedding-supplied function identity;
- tracks hits, misses, compilations, evictions, entries, and code bytes;
- enforces entry-count and executable-byte limits;
- evicts the least-recently-used entry when capacity is exceeded;
- supports targeted invalidation and complete clearing.

Runtime teardown must destroy the cache before function identities become
invalid. Mutation that changes a function bytecode body must invalidate that
function's cache key.

## Automatic tiering

`TurboJSTieredFunction` tracks call count and one compilation attempt. Before
its threshold it executes the verified IR interpreter. At the threshold it
compiles once and immediately invokes the generated code. Later calls use the
cached native function.

A failed compilation caused by unsupported IR remains on the interpreter path.
Memory errors and malformed IR remain explicit errors.

## Next integration boundary

The engine call path should next attach a tiering record to function bytecode
metadata and add guarded `JSValue` unboxing/reboxing:

1. Verify all arguments required by the compiled trace are integer values.
2. Unbox them into an `int64_t` argument array.
3. Invoke the cached native function.
4. Rebox the result using the smallest valid JavaScript numeric representation.
5. Fall back to the existing interpreter when any guard fails.

This keeps JavaScript semantics authoritative while allowing proven numeric hot
functions to enter the baseline native tier.
