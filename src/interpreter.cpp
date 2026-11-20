#include "dk/interpreter.hpp"
#include "dk/runtime.hpp"

#include <memory>

namespace dk {
struct Interpreter::Impl {
    Runtime runtime;
    Impl(std::ostream& output, std::istream& input) : runtime(output, input) {}
};

Interpreter::Interpreter(std::ostream& output, std::istream& input)
    : impl_(std::make_unique<Impl>(output, input)) {}

Interpreter::~Interpreter() = default;

void Interpreter::run(const std::string& source, const std::filesystem::path& source_path) {
    impl_->runtime.run(source, source_path);
}

} // namespace dk
