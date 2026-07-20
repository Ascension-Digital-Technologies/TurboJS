# Runtime and Virtual Machine

The VM is the semantic execution center of TurboJS. It evaluates bytecode, manages JavaScript frames and calls, invokes runtime helpers, propagates exceptions, collects feedback, and transfers execution into and out of native tiers.

## Execution state

A runtime owns shared engine state. A context owns realm-level state and global objects. A function invocation creates frame state containing arguments, locals, operand stack values, the current bytecode position, and exception/call metadata. Ownership must remain explicit because allocation, garbage collection, and deoptimization can observe this state.

## Dispatch

The interpreter decodes an opcode, executes its semantic operation, updates the frame, and advances or redirects the bytecode position. Hot operations may use inline caches or specialized helpers, but the fallback must preserve generic semantics.

## Calls and exceptions

Calls normalize the receiver, arguments, callable identity, and constructor state before entering JavaScript or native code. Exceptions use the engine's pending-exception protocol and unwind to the nearest valid handler or host boundary. Native helpers must never silently convert an exception into an ordinary value.

## Feedback and tiering

The VM records counters and type/shape/call observations used by optimization policy. Feedback is advisory. Missing, stale, or polymorphic feedback must lead to a generic path, a guarded specialization, or no optimization—not incorrect execution.

## Native transitions

VM-to-native entry is governed by the JavaScript frame ABI. Native code may call helpers, reach safepoints, request OSR transitions, or deoptimize. Each transition must identify live managed values and a continuation point that can reconstruct valid managed execution.

## Debugging order

For a suspected VM/JIT bug, first reproduce with native tiers disabled. If interpreter-only execution is correct, compare bytecode state, feedback, lowered IR, guard behavior, and reconstructed deoptimization state. Differential tests should compare results and exceptions, not only process exit codes.
