# Spool runtime-helper exits

Spool may compile a supported bytecode prefix and terminate it with
`TURBOJS_IR_RUNTIME_HELPER` when the next opcode requires full Pulse semantics.
The native backend records a runtime-call safepoint at the exact bytecode offset
and returns `TURBOJS_IR_BAILOUT`, allowing Rewind to resume from that opcode.

This is intentionally conservative: unsupported operations are not guessed or
partially executed. The compiled prefix remains useful, while Pulse remains the
source of truth for the unsupported operation and all following semantics.

`TurboJSSpoolLoweringStats` exposes `runtime_helper_exit_count` and
`partial_function_count` so benchmark runs can distinguish broad partial native
coverage from complete-function compilation.
