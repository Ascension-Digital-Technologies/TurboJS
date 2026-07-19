# API implementations

This directory implements the stable embedding facade and optional libc integration. Supported public declarations are owned by `include/turbojs/`; this directory contains implementation only.

Changes here require embedding tests, lifecycle review, and API/ABI compatibility review. JIT backend structures, GC internals, parser state, and private engine types must not leak through these files.
