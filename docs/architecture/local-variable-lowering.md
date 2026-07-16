# Local-variable lowering

TurboJS baseline IR now models mutable VM locals explicitly instead of pretending every value is a permanent SSA register.

## IR operations

- `local.get index -> register`
- `local.set index, register`

`TurboJSIRFunction.local_count` defines the valid local index range. Verification rejects invalid loads and stores before interpretation or native compilation.

## Native layout

The x86-64 baseline backend places virtual registers and locals in one aligned stack frame:

```text
rbp - 8                         virtual register 0
rbp - 8 * register_count       final virtual register
rbp - 8 * (register_count + 1) local 0
...                             remaining locals
```

This representation favors correctness and low compiler complexity. A later register allocator can promote hot locals while retaining these stack slots as deoptimization homes.

## Engine bytecode coverage

The frontend recognizes:

- `get_loc`, `put_loc`, `set_loc`
- `get_loc8`, `put_loc8`, `set_loc8`
- compact `get_loc0..3`, `put_loc0..3`, `set_loc0..3`

Local values remain guarded by the VM tier's integer-only entry policy. Dynamic values continue through the interpreter.
