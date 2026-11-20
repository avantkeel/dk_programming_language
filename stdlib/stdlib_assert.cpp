#include "dk/stdlib_assert.hpp"

#include "dk/runtime.hpp"

namespace dk {
void install_assert(Runtime& runtime) {
    auto assertion = std::make_shared<NativeFunction>(
        NativeFunction{"assert", 1, 2, [](const std::vector<Value>& args) {
            const auto* condition = std::get_if<bool>(&args[0].data);
            if (!condition)
                throw LanguageError("assert expects a bool condition");

            const std::string* message = nullptr;
            if (args.size() == 2) {
                message = std::get_if<std::string>(&args[1].data);
                if (!message)
                    throw LanguageError("assert expects a string message");
            }

            if (!*condition) {
                if (message)
                    throw LanguageError("Assertion failed: " + *message);
                throw LanguageError("Assertion failed");
            }
            return Value{};
        }});
    runtime.globals->define_builtin("assert", Value(assertion));
}
} // namespace dk
