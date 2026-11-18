CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude
TARGET := bin/dk.exe
SOURCES := src/main.cpp src/interpreter.cpp src/runtime.cpp

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCES) include/dk/interpreter.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

test: $(TARGET)
	./$(TARGET) tests/smoke.dk
	./$(TARGET) examples/main.dk < tests/input.txt
	! ./$(TARGET) tests/type_error.dk

clean:
	rm -rf bin
