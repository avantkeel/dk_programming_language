#include "dk/interpreter.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: dk <file.dk>\n";
        return 2;
    }

    const std::filesystem::path path(argv[1]);
    if (path.extension() != ".dk") {
        std::cerr << "Error: the input file must have a .dk extension\n";
        return 2;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: cannot open '" << path.string() << "'\n";
        return 1;
    }
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

    try {
        dk::Interpreter interpreter(std::cout, std::cin);
        interpreter.run(source, std::filesystem::absolute(path));
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
