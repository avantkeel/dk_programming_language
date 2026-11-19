CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude
TARGET := bin/dk.exe
SOURCES := src/main.cpp src/interpreter.cpp src/runtime.cpp src/stdlib_random.cpp \
           src/stdlib_assert.cpp src/stdlib_json.cpp src/stdlib_http.cpp
HEADERS := $(wildcard src/*.hpp) include/dk/interpreter.hpp

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

.PHONY: all test test-http clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) $(SOURCES) -o $(TARGET) $(CURL_LIBS)

# Offline tests: nothing here needs a network connection.
test: $(TARGET)
	./$(TARGET) tests/smoke.dk
	./$(TARGET) examples/main.dk < tests/input.txt
	! ./$(TARGET) tests/type_error.dk
	./$(TARGET) tests/random.dk
	! ./$(TARGET) tests/assert_fail.dk
	./$(TARGET) tests/random_module.dk
	! ./$(TARGET) tests/builtin_readonly.dk
	./$(TARGET) tests/json.dk
	./$(TARGET) tests/http_offline.dk

# Starts a throwaway HTTP server on 127.0.0.1:8765 (needs python3), runs the
# HTTP tests against it, then stops it.
test-http: $(TARGET)
	python3 tests/http_server.py 8765 & server=$$!; sleep 1; \
	./$(TARGET) tests/http_local.dk; status=$$?; \
	kill $$server 2>/dev/null; exit $$status

clean:
	rm -rf bin
