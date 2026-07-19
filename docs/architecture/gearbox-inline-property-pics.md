# Gearbox inline property PICs

Gearbox lowers exact property regions directly into x64 machine code when the embedding supplies a private object layout.

## Load path

The generated code validates the argument count, optional object tag, decoded object pointer, and up to four receiver shapes. A matching case validates its property dependency generation and loads the known own-slot offset directly.

## Store path

Exact writable own-data stores use the same guarded case chain and write the known slot directly. Shape, dependency, tag, or argument failures return a normal TurboJS bailout without committing a store.

## Safety

The machine-code path never trusts feedback without guards. Property generations are checked through the embedding callback from generated code. Operations not matching the exact supported region shape remain on the verified guarded evaluator.

## Telemetry

`TurboJSRegionNativeStats` reports inline PIC case count, load/store count, and dependency-guard count.
