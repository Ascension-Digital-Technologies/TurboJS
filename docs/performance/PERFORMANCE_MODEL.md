# Performance Model

TurboJS targets three dimensions simultaneously: sustained execution speed, startup/footprint efficiency, and low embedding overhead. No single benchmark represents all three.

## Measurements

- **Whole-engine elapsed time:** includes the engine behavior exercised by a complete workload.
- **Steady-state kernel time:** isolates hot execution after warmup.
- **Startup latency:** process creation through completion of a minimal script.
- **Resident memory:** RSS or another explicitly declared process measure.
- **Executable/library size:** exact file and build configuration.
- **Compile cost:** time and memory spent producing native code.
- **Code size:** installed native bytes and metadata.

## Correctness gates

Every comparison uses identical source or documents any adaptation. Outputs are checked through checksums or exact semantic assertions. Warmup, measured runs, statistic, CPU, OS, compiler, power state, and engine flags must be recorded.

## Interpreting retained results

Files under `benchmarks/results` preserve historical snapshots and their recorded version strings. They are evidence for those configurations, not an automatic promise for every program or later build. Re-run the supplied harness on the target system before making deployment decisions.

## Optimization tradeoffs

A change can improve a hot kernel while increasing startup, memory, compile time, or code size. TurboJS performance reviews should report all materially affected dimensions and retain the interpreter fallback.
