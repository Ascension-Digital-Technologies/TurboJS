# Built-ins and Language Services

Several subsystems support language behavior outside the core bytecode loop.

## Built-ins

`src/builtins` implements standard constructors, prototypes, functions, and intrinsic helpers. Built-ins must obey ordinary property, exception, coercion, and GC rules even when implemented in C. Generated built-in tables live under `generated/builtins`.

## Modules and eval

`src/modules` owns module loading hooks, eval entry points, source execution options, and related host integration. Hosts control how module names map to source or artifacts. The engine does not assume a browser or Node-style module environment.

## Regular expressions

`src/regexp` parses and executes regular-expression programs. Changes require syntax, matching, Unicode, capture, backtracking, malformed-pattern, and resource-behavior tests.

## Unicode

`src/unicode` and generated tables implement character classification and transformation needed by parsing, strings, and regular expressions. Generated Unicode data retains its own required license notice.

## Numeric conversion

`src/numeric` contains decimal/binary conversion and formatting support. Numeric changes must test boundary values, negative zero, infinities, NaN behavior, rounding, large exponents, and parse/print round trips.

## Serialization

`src/serialization` persists bytecode and related runtime data. It is a security-sensitive parser and must reject malformed data before use.
