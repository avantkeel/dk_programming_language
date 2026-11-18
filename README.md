# dk language interpreter


## Build

With w64devkit on `PATH`, run:

```text
make
```

This creates `bin/dk.exe`. Other useful targets are `make test` and
`make clean`.

## Run a program

```text
bin/dk.exe examples/main.dk
```

Top-level declarations and statements execute in source order. If a program
declares a zero-argument `function main()`, it is called after top-level code
finishes. Input is read from standard input.

## Language features

- `int`, `float`, `string`, `bool`, list types such as `[int]`, and type aliases
- Variables, arithmetic, comparisons, assignment, `??`, and boolean operators
- Functions with parameters, return types, and `return`
- Selective imports from `.dk` modules and explicit exported declarations
- Structs, enums, classes, constructors, inheritance, `this`, `super`, and
  dynamic method dispatch
- `if` / `else if` / `else`, `while`, `for value in list`, and integer ranges
  written as `start..end` (the end is exclusive)
- `terminal.print`, `terminal.read`, and `terminal.readInt`

Statements end with semicolons, except block statements. Type mismatches,
lexical errors, syntax errors, and runtime errors produce a nonzero exit status.

## Modules

Module paths are resolved relative to the importing file. Select names to
import and mark declarations available to other modules with `export`:

```text
import { add } from "math/utils";

export function add(a: int, b: int) -> int {
    return a + b;
}
```

The `.dk` extension may be omitted from an import path. Only explicitly
exported declarations can be imported.
