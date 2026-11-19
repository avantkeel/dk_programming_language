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
`http://` and `https://` URLs are allowed):

```text
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
