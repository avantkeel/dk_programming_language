CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude
TARGET := bin/dk.exe
SOURCES := src/main.cpp src/interpreter.cpp src/runtime.cpp stdlib/stdlib_random.cpp \
           stdlib/stdlib_assert.cpp stdlib/stdlib_json.cpp stdlib/stdlib_http.cpp
HEADERS := $(wildcard include/dk/*.hpp)

# libcurl (needed by the http library).
#   * Linux/macOS/MSYS2: found automatically through pkg-config or curl-config.
#   * Elsewhere (e.g. w64devkit): point CURL_PREFIX at a folder that contains
#     include/curl and lib/, for example:  make CURL_PREFIX=C:/libs/curl
#   * Or override the flags directly:  make CURL_CFLAGS=... CURL_LIBS=...
ifdef CURL_PREFIX
CURL_CFLAGS ?= -I$(CURL_PREFIX)/include
CURL_LIBS ?= -L$(CURL_PREFIX)/lib -lcurl
else
CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null)
CURL_LIBS ?= $(shell pkg-config --libs libcurl 2>/dev/null || curl-config --libs 2>/dev/null || echo -lcurl)
endif

.PHONY: all test test-http bench clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) $(SOURCES) -o $(TARGET) $(CURL_LIBS)

# Offline tests: nothing here needs a network connection.
test: $(TARGET)
	./$(TARGET) tests/smoke.dk
	./$(TARGET) examples/main.dk < tests/input.txt
	! ./$(TARGET) tests/type_error.dk
	./$(TARGET) tests/unknown_type.dk 2>&1 | grep -q "Unknown type 'MissingType'"
	./$(TARGET) tests/type_assignment_error.dk 2>&1 | grep -q "must be 'int'"
	./$(TARGET) tests/untyped_variable.dk 2>&1 | grep -q "Variables must declare a type"
	./$(TARGET) tests/untyped_parameter.dk 2>&1 | grep -q "Function parameters must declare a type"
	./$(TARGET) tests/untyped_loop.dk 2>&1 | grep -q "Loop variables must declare a type"
	./$(TARGET) tests/loop_type_error.dk 2>&1 | grep -q "loop variable 'item' must be 'string'"
	./$(TARGET) tests/random.dk
	! ./$(TARGET) tests/assert_fail.dk
	./$(TARGET) tests/random_module.dk
	./$(TARGET) tests/switch.dk
	! ./$(TARGET) tests/builtin_readonly.dk
	./$(TARGET) tests/json.dk
	./$(TARGET) tests/map.dk
	./$(TARGET) tests/type_alias_container.dk
	./$(TARGET) tests/type_alias_circular.dk 2>&1 | grep -q "Circular type alias"
	./$(TARGET) tests/map_bad_key.dk 2>&1 | grep -q "Map keys must be"
	./$(TARGET) tests/map_bad_key_type.dk 2>&1 | grep -q "Map key type must be"
	./$(TARGET) tests/map_type_error.dk 2>&1 | grep -q "must be '\[string: int\]'"
	./$(TARGET) tests/map_key_type_error.dk 2>&1 | grep -q "must be '\[string: int\]'"
	./$(TARGET) tests/map_missing_compound.dk 2>&1 | grep -q "missing map key"
	./$(TARGET) tests/map_for.dk 2>&1 | grep -q "cannot iterate a map"
	./$(TARGET) tests/map_mixed_literal.dk 2>&1 | grep -q "cannot contain"
	./$(TARGET) tests/http_offline.dk
	./$(TARGET) tests/http_not_imported.dk 2>&1 | grep -q "Undefined name 'http'"
	./$(TARGET) tests/http_before_import.dk 2>&1 | grep -q "Undefined name 'http'"
	./$(TARGET) tests/http_bad_import.dk 2>&1 | grep -q "does not export 'get'"
	./$(TARGET) tests/http_unknown_std.dk 2>&1 | grep -q "Unknown standard library"
	./$(TARGET) tests/http_double_import.dk 2>&1 | grep -q "already defined"
	./$(TARGET) tests/http_module_scope.dk 2>&1 | grep -q "Undefined name 'http'"

# Runs each benchmark once and reports wall-clock time.
bench: $(TARGET)
	time ./$(TARGET) bench/loop.dk
	time ./$(TARGET) bench/fib.dk

# Starts a throwaway HTTP server on 127.0.0.1:8765 (needs python3), runs the
# HTTP tests against it, then stops it.
test-http: $(TARGET)
	python3 tests/http_server.py 8765 & server=$$!; \
	cleanup() { \
		if [ -n "$$server" ]; then \
			kill "$$server" 2>/dev/null || true; \
			wait "$$server" 2>/dev/null || true; \
		fi; \
	}; \
	trap cleanup EXIT; \
	ready=0; attempt=0; \
	while [ $$attempt -lt 50 ]; do \
		if ! kill -0 "$$server" 2>/dev/null; then \
			wait "$$server"; server_status=$$?; server=; \
			echo "HTTP test server exited before becoming ready (status $$server_status)" >&2; \
			exit 1; \
		fi; \
		if python3 -c 'import urllib.request; urllib.request.urlopen("http://127.0.0.1:8765/json", timeout=0.2).read()' >/dev/null 2>&1; then \
			ready=1; break; \
		fi; \
		attempt=$$((attempt + 1)); sleep 0.1; \
	done; \
	if [ $$ready -ne 1 ]; then \
		echo "HTTP test server did not become ready at 127.0.0.1:8765 after 50 attempts" >&2; \
		exit 1; \
	fi; \
	./$(TARGET) tests/http_local.dk

clean:
	rm -rf bin
