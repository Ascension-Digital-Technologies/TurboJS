# Runtime ownership

TurboJS does not use a generic runtime source bucket. Runtime code is assigned to the subsystem that owns its invariants and data.

| Responsibility | Location |
|---|---|
| Runtime/context lifecycle and configuration | `src/core/` |
| Atom table and atom conversion | `src/core/` |
| Classes, strings, shapes, and objects | `src/objects/` |
| Job queue, calls, interpreter, and generators | `src/vm/` |
| Optimization policy, JIT, tiering, and AOT | `src/jit/` |
| Public APIs and libc integration | `include/turbojs/` for declarations and `src/api/` for implementations |
| REPL and standalone bootstrap sources | `apps/turbojs/scripts/` |

This layout reduces cross-domain dumping grounds and makes subsystem ownership visible in the filesystem. The generated engine unit preserves the declaration ordering required by the inherited internal static-linkage model.
