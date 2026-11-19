CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude
TARGET := bin/dk.exe
SOURCES := src/main.cpp src/interpreter.cpp src/runtime.cpp src/stdlib_random.cpp src/stdlib_assert.cpp
HEADERS := $(wildcard src/*.hpp) include/dk/interpreter.hpp

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

test: $(TARGET)
	./$(TARGET) tests/smoke.dk
	./$(TARGET) examples/main.dk < tests/input.txt
	! ./$(TARGET) tests/type_error.dk
	./$(TARGET) tests/random.dk
	! ./$(TARGET) tests/assert_fail.dk
	./$(TARGET) tests/random_module.dk
	! ./$(TARGET) tests/builtin_readonly.dk

clean:
	rm -rf bin
