# Engine architecture

TurboJS uses a generated unity translation unit for tightly coupled engine internals while keeping subsystem ownership explicit in the source tree. Public and progressively extracted APIs compile independently.

## Execution pipeline

1. The lexer and parser produce bytecode and metadata.
2. The interpreter executes stack-based bytecode.
3. Runtime profiling records function hotness and requests an optimization tier.
4. Future JIT backends consume a backend-neutral IR rather than reaching into parser state.
5. Serialized bytecode provides the first portable AOT format.

The tiering API is intentionally independent of a native code emitter. This keeps policy, profiling, code generation, and platform-specific executable-memory management testable in isolation.
