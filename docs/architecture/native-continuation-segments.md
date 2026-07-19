# Spool native continuation segments

After a runtime helper returns, TurboJS may compile the following straight-line IR suffix into a temporary Spool native segment. The segment receives the materialized virtual registers and locals as arguments, reconstructs the frame, and executes natively until it returns or reaches another runtime-helper boundary.

The first implementation is deliberately conservative. Branches, jumps, tagged-only operations, and shapes that cannot be relocated safely use the portable continuation engine. Runtime counters expose segment compilations, entries, and fallbacks.
