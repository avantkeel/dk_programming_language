#pragma once

#include <filesystem>
#include <istream>
#include <memory>
#include <ostream>
#include <string>

namespace dk {

class Interpreter {
public:
    Interpreter(std::ostream& output, std::istream& input);
    ~Interpreter();

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    void run(const std::string& source,
             const std::filesystem::path& source_path = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dk
