# Runtime stress testing

The focused runtime stress target is `turbojs.runtime.lifecycle-stress`.

```bash
cmake -S . -B build/stress -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/stress
TURBOJS_STRESS_CYCLES=100000 ctest --test-dir build/stress \
  -R turbojs.runtime.lifecycle-stress --output-on-failure
```

Run the same target under AddressSanitizer and UndefinedBehaviorSanitizer before
publishing a release candidate. The default CI count is intentionally short;
nightly and pre-release jobs should raise the cycle count.
