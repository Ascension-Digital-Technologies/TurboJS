# Testing

Run the native suite with:

```sh
ctest --test-dir build --output-on-failure
```

Tests are grouped into API, unit, integration, conformance, differential, and benchmark layers. Missing optional suites are not silently represented as built targets. The Test262 runner is enabled only when its source is present and `TURBOJS_BUILD_TEST262_RUNNER=ON` is requested.
