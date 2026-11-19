# dk language interpreter


## Build

The `http` library uses [libcurl](https://curl.se/libcurl/), so its headers and
library must be available when building.

- **Linux / macOS / MSYS2:** install the dev package (for example
  `libcurl4-openssl-dev`, `brew install curl`, or
  `pacman -S mingw-w64-x86_64-curl`); `make` finds it via `pkg-config`.
- **w64devkit:** w64devkit does not ship libcurl. Download a MinGW build of
  curl (e.g. from curl.se/windows) and run
  `make CURL_PREFIX=C:/path/to/curl` (a folder containing `include/curl` and
  `lib/`). Put `libcurl-x64.dll` next to `bin/dk.exe` or on `PATH` if the build
  is dynamic.
- You can also set `CURL_CFLAGS` and `CURL_LIBS` yourself.

With the toolchain on `PATH`, run:

```text
./setup_windows.sh
make CURL_PREFIX=third_party/curl
```

This creates `bin/dk.exe`. Other useful targets are `make test` (offline),
`make test-http` (runs HTTP tests against a local Python server) and
`make clean`.

## Run a program

```text
cp third_party/curl/bin/libcurl-x64.dll bin/
bin/dk.exe examples/main.dk

```

Top-level declarations and statements execute in source order. If a program
declares a zero-argument `function main()`, it is called after top-level code
finishes. Input is read from standard input.

## Language features

- `int`, `float`, `string`, `bool`, list types such as `[int]`, map types such as
  `[string: int]`, and type aliases
- Variables, arithmetic, comparisons, assignment, `??`, and boolean operators
- Functions with parameters, return types, and `return`
- Selective imports from `.dk` modules and explicit exported declarations
- Structs, enums, classes, constructors, inheritance, `this`, `super`, and
  dynamic method dispatch
- `if` / `else if` / `else`, `switch` / `case` / `default`, `while`,
  `for value in list`, and integer ranges
  written as `start..end` (the end is exclusive)
- `terminal.print`, `terminal.read`, and `terminal.readInt`

Statements end with semicolons, except block statements. Type mismatches,
lexical errors, syntax errors, and runtime errors produce a nonzero exit status.

Switch statements match one value and execute only the matching case:

```text
switch value {
    case 1 {
        terminal.print("one");
    }
    case 2 {
        terminal.print("two");
    }
    default {
        terminal.print("other");
    }
}
```

## Maps

A map is written like a list with `key: value` entries, and its type is
`[KeyType: ValueType]`. The empty map is `[:]`.

```text
variable ages: [string: int] = ["ana": 31, "luis": 28];
ages["marta"] = 40;
ages["ana"] += 1;
terminal.print(ages["ana"]);          // 32
terminal.print(ages["zoe"] ?? 0);     // a missing key reads as null

if ages.has("luis") {
    ages.remove("luis");
}
for name in ages.keys() {
    terminal.print(name + " is " + ages[name]);
}
```

| Member | Description |
|---|---|
| `m[key]` | Value for the key, or `null` if it is missing |
| `m[key] = value` | Sets or replaces an entry (`+=` and friends need an existing key) |
| `m.has(key)` | Whether the key exists |
| `m.remove(key)` | Removes the key; `true` if it existed |
| `m.keys()` / `m.values()` | Lists of keys / values, ordered by key |
| `m.length` | Number of entries |

Keys can be `int`, `string`, `bool` or enum values. Keys are kept sorted, so
iteration order is deterministic. Maps are shared like lists: a function that
receives a map changes the caller's map. A map cannot be used directly in a
`for` loop; loop over `m.keys()` or `m.values()`. `json.stringify` writes a map
with string keys as a JSON object.

## Random

The built-in `random` library provides `seed(int)`, inclusive `int(min, max)`,
`float()` in `[0.0, 1.0)`, `bool()`, `choice(list)`, and `shuffle(list)` (which
returns a new list). For example:

```text
random.seed(42);
terminal.print(random.int(1, 6));
```

The built-in `assert(condition, message?)` stops execution with an error when
the condition is false.

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

## HTTP

`http` fetches web APIs over HTTP and HTTPS (redirects are followed, only
`http://` and `https://` URLs are allowed). Unlike `terminal`, `random`, `json`
and `assert`, it is **not global**: each file must import it before using it,
and the import must run before the first use. The `std:` prefix is reserved for
standard libraries, so it never refers to a `.dk` file.

```text
import { http } from "std:http";

http.timeout(15);   // seconds, default 30

variable res = http.get("https://api.github.com/repos/torvalds/linux",
                        ["Accept: application/vnd.github+json"]);
if res == null {
    terminal.print("failed: " + http.lastError());
} else if res.ok {
    variable repo = json.parse(res.body);
    terminal.print(repo.full_name);
}
```

| Function | Description |
|---|---|
| `http.get(url, headers?)` | GET request |
| `http.post(url, body, headers?)` | POST with a string body |
| `http.request(method, url, body?, headers?)` | Any method, e.g. `"PUT"`, `"DELETE"`; `body` may be `null` |
| `http.header(response, name)` | Response header (case-insensitive), or `null` |
| `http.timeout(seconds)` | Total time limit for requests |
| `http.lastError()` | Reason the last request failed |
| `http.encode(text)` | Percent-encodes text for URLs |

`headers` is a list of `"Name: value"` strings. A request that completes returns
a `Response` with `status`, `ok` (true for 2xx), `body`, `url` (after redirects)
and `headers`. A 404 or 500 is still a normal response. If the request cannot
complete at all (DNS failure, refused connection, timeout, TLS error), the
function returns `null`, so `http.get(url) ?? fallback` works. Bad arguments
stop the program with an error. Responses over 64 MB are rejected.

Imports are per file: a module that uses `http` must import it itself, even if
the program that imports the module has already done so. Using `http` without
importing it is an error (`Undefined name 'http'`), as is importing it twice or
importing any name other than `http` from `std:http`.

For HTTPS certificate problems, set the `DK_CA_BUNDLE` environment variable to
a CA bundle file (`CURL_CA_BUNDLE` and `SSL_CERT_FILE` are also honoured).

## JSON

| Function | Description |
|---|---|
| `json.parse(text)` | Parses JSON; returns `null` if invalid (see `json.lastError()`) |
| `json.valid(text)` | `true` if the text is valid JSON |
| `json.stringify(value, pretty?)` | Serialises ints, floats, strings, bools, `null`, lists and structs |
| `json.get(value, key)` | Field or list element, or `null` if missing |
| `json.has(object, key)` | Whether the object has the key |
| `json.keys(object)` | List of the object's keys |

JSON objects become structs, so fields read with dot syntax (`repo.owner.login`)
and arrays become lists. Reading a missing field with `.` is an error; use
`json.get` or `json.has` for optional fields and for keys that are not valid
identifiers. Whole numbers become `int`, other numbers `float`. Object keys are
kept in alphabetical order.
