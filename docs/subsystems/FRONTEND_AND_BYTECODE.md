# Frontend and Bytecode

The frontend converts source text into executable bytecode plus the metadata required for scopes, constants, exceptions, debugging, modules, and later optimization.

## Responsibilities

- Lexical analysis and token classification.
- Parsing expressions, statements, functions, classes, modules, and declarations.
- Scope and binding resolution.
- Constant-pool and atom management.
- Bytecode emission and branch patching.
- Stack-depth accounting and exception-region metadata.
- Source-location and function metadata.

## Bytecode contract

Each instruction has a defined operand encoding, stack effect, control-flow behavior, and exceptional behavior. The interpreter and JIT frontend must agree on all four. Adding or changing an opcode therefore requires updating the opcode definition, emitter, interpreter dispatch, bytecode reader/writer when serialized, JIT analysis or explicit unsupported handling, verifier assumptions, and tests.

## Control flow

Branches, loops, exception edges, and returns form the bytecode control-flow graph consumed by native tiers. Stack height and local state must be compatible at merge points. The frontend records enough state for the JIT to construct frame states and safe deoptimization exits.

## Serialized bytecode

The bytecode format is versioned. Readers validate version and structure before allocating or executing decoded content. Historical bytecode should never be assumed compatible solely because source-level APIs are compatible.

## Safe change checklist

1. Add focused parser and semantic tests.
2. Confirm interpreter behavior without JIT.
3. Add serialization round-trip coverage when representation changes.
4. Add JIT differential coverage for supported instructions.
5. Verify malformed input is rejected rather than partially accepted.
6. Run the relevant Test262 subset and whole validation suite.
