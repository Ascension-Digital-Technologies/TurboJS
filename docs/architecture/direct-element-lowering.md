# Direct element lowering

TurboJS provides an embedding-owned `TurboJSRegionElementLayout` contract for exact, single-block element regions. On SysV x64, Gearbox can directly emit constant-index packed, holey, and typed 64-bit element loads plus writable stores.

The generated guard chain validates the tagged object representation, element kind, storage generation, and current length before computing the storage address. Any failed guard returns `TURBOJS_IR_BAILOUT` before reading or writing the element storage.

Unsupported graphs retain the guarded region-value backend. This keeps the public semantic oracle and bailout behavior unchanged while providing a measurable machine-code path for hot exact accesses.
