# Compiler subsystem

Lexes and parses JavaScript source and emits engine bytecode. Compiler output is consumed by the interpreter, JIT frontend, and serializers.

## Rules

Bytecode changes must update opcode metadata, decoding, validation, tests, and relevant JIT/AOT translators.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.
