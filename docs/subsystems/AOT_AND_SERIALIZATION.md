# AOT, Bytecode, and Persistent Artifacts

TurboJS uses multiple persistent representations for different purposes. They are independently versioned and should not be treated as interchangeable.

## Bytecode

Serialized bytecode stores executable VM instructions and supporting metadata. It is compact and close to the interpreter, but tied to bytecode format compatibility.

## TJIR

TJIR is the portable optimizing IR representation. Its header carries a magic value and format version. Readers validate sizes, instruction counts, references, and supported operations before constructing compiler state.

## TJM

TJM is a module container for one or more artifacts and their metadata. The format defines a header, version, entry table, offsets, and total size. See [TJM module format](../specifications/tjm-module-format.md).

## Generated runtime data

Generated built-ins, Unicode data, and the engine unity unit are build products derived from checked-in source or generators. Generated files must identify their generator and must not be hand-edited. Reproducible generation is part of release validation.

## Compatibility policy

A reader accepts only versions it explicitly supports. A format change increments the relevant format version, updates the specification, adds old/new rejection and round-trip tests, and records migration or regeneration requirements. Engine version 1.0.0 does not imply that every internal artifact has version 1.

## Trust boundary

Treat external artifacts as untrusted binary input. Validate magic, version, integer overflow, offsets, lengths, counts, graph references, and resource limits before allocation or execution.
