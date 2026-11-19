#pragma once

#include <string>

namespace dk {
struct Runtime;
struct Value;

void install_json(Runtime& runtime);

// Serialises a dk value to JSON text. Throws LanguageError for values JSON
// cannot represent (functions, classes, NaN, infinity, ...).
std::string json_stringify(const Value& value, bool pretty = false);
}
