# Embedding TurboJS

TurboJS can be linked as a compact engine library without requiring a browser, DOM, event loop, or application framework. The host owns process integration, I/O policy, scheduling, module resolution, and security boundaries.

## Choose an API

- Use `turbojs_embed.h` for the narrow, versioned stable embedding table. It is the preferred starting point for long-lived downstream integrations.
- Use `turbojs.h` for the full engine API when you need contexts, values, objects, modules, custom classes, or detailed runtime control. This surface is larger and requires more ownership discipline.
- Use `jit.h` only for compiler/JIT integration work; it is not a replacement for the language API.

## Stable lifecycle

1. Request the API table with `TurboJS_GetEmbedAPI(TURBOJS_EMBED_ABI_VERSION)`.
2. Validate the returned table and `struct_size`.
3. Fill `TurboJSEmbedConfig` with its size, API version, memory limit, and stack limit.
4. Create an engine instance.
5. Evaluate source and inspect the returned status.
6. Read `last_error` before another engine operation when a call fails.
7. Optionally request garbage collection.
8. Destroy the engine exactly once.

## Threading

Treat an engine/runtime/context as thread-confined unless an API explicitly states otherwise. A host may create independent engines on different threads. Do not call into the same engine concurrently without a host-owned serialization policy.

## Host functions and values

The full API permits JavaScript/C interoperation. Every created or retained `JSValue` must follow documented duplication/freeing rules. Never retain context-owned pointers after their context is destroyed. Convert and validate arguments before performing host side effects.

## Modules and I/O

TurboJS deliberately does not impose a module filesystem, network stack, or event loop. The embedder supplies loaders and native capabilities. This makes the engine suitable for applications, plugins, tools, isolated workers, game scripting, and constrained runtimes.

## Sandboxing

Memory limits reduce resource exposure but do not create a complete security sandbox. For hostile scripts, combine bounded memory, execution deadlines or interrupts, restricted native capabilities, validated module loading, process isolation where appropriate, and defensive artifact parsing.

## Compatibility

The stable table has independent API and ABI versions plus `struct_size` fields for additive evolution. Callers must request the ABI they were compiled for and must not access fields beyond the reported size.
