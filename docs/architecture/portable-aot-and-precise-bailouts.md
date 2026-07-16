# Portable AOT IR and Precise Bailouts

TurboJS now serializes verified IR into a versioned, endian-defined `TJIR` image. The format stores function metadata and fixed-width instruction records, followed by an FNV-1a integrity checksum. Deserialization validates magic, version, declared size, checksum, register limits, and the reconstructed IR before it can execute or compile.

Checked native arithmetic records the exact IR instruction that caused a bailout. The x86-64 entry ABI receives a bailout-index output pointer; each checked operation emits its own cold bailout block, writes its instruction index, and returns `TURBOJS_IR_BAILOUT`. This is the first concrete prerequisite for bytecode-position maps and instruction-precise interpreter resumption.

The portable image deliberately contains IR rather than native machine code. It can be produced once, loaded on another machine with the same TurboJS IR format version, interpreted immediately, or compiled through the local baseline backend.
