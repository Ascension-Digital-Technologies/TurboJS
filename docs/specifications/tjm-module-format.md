# TJM1 portable AOT module

`TJM1` is TurboJS v1's checksummed, multi-function portable AOT container. It stores named exports and embedded verified TJIR function images. The format is little-endian and versioned.

The `turbojs-aot-inspect` utility validates a module before printing its version, image size, checksum, exports, and IR instruction counts.

```sh
turbojs-aot-inspect application.tjm
```

A module is rejected when its magic, version, table bounds, declared size, embedded function image, or whole-module checksum is invalid.
