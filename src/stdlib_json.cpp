#include "stdlib_json.hpp"

#include "runtime.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>

namespace dk {
namespace {
constexpr int kMaxDepth = 200;

struct ParseError {
    std::string message;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    Value parse_document() {
        skip_space();
        Value value = parse_value(0);
        skip_space();
        if (pos_ != text_.size()) fail("unexpected trailing characters");
        return value;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        throw ParseError{what + " at offset " + std::to_string(pos_)};
    }
    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return at_end() ? '\0' : text_[pos_]; }
    void skip_space() {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' ||
                             peek() == '\r'))
            ++pos_;
    }
    void expect_word(const char* word) {
        for (const char* c = word; *c; ++c, ++pos_)
            if (peek() != *c) fail("invalid literal");
    }

    Value parse_value(int depth) {
        if (depth > kMaxDepth) fail("nesting is too deep");
        if (at_end()) fail("unexpected end of input");
        switch (peek()) {
        case '{': return parse_object(depth);
        case '[': return parse_array(depth);
        case '"': return Value(parse_string());
        case 't': expect_word("true"); return Value(true);
        case 'f': expect_word("false"); return Value(false);
        case 'n': expect_word("null"); return Value{};
        default: return parse_number();
        }
    }

    Value parse_object(int depth) {
        auto object = std::make_shared<StructValue>();
        object->type_name = "object";
        ++pos_; // {
        skip_space();
        if (peek() == '}') { ++pos_; return Value(object); }
        while (true) {
            skip_space();
            if (peek() != '"') fail("expected a string key");
            std::string key = parse_string();
            skip_space();
            if (peek() != ':') fail("expected ':'");
            ++pos_;
            skip_space();
            object->fields[key] = parse_value(depth + 1);
            skip_space();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            fail("expected ',' or '}'");
        }
        return Value(object);
    }

    Value parse_array(int depth) {
        auto list = std::make_shared<ListValue>();
        ++pos_; // [
        skip_space();
        if (peek() == ']') { ++pos_; return Value(list); }
        while (true) {
            skip_space();
            list->elements.push_back(parse_value(depth + 1));
            skip_space();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            fail("expected ',' or ']'");
        }
        return Value(list);
    }

    unsigned parse_hex4() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_end()) fail("truncated \\u escape");
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid \\u escape");
        }
        return code;
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string parse_string() {
        ++pos_; // opening quote
        std::string out;
        while (true) {
            if (at_end()) fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return out;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (at_end()) fail("unterminated escape");
            const char escape = text_[pos_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = parse_hex4();
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (peek() != '\\' || pos_ + 1 >= text_.size() ||
                        text_[pos_ + 1] != 'u')
                        fail("unpaired surrogate");
                    pos_ += 2;
                    const unsigned low = parse_hex4();
                    if (low < 0xDC00 || low > 0xDFFF) fail("invalid surrogate pair");
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    fail("unpaired surrogate");
                }
                append_utf8(out, code);
                break;
            }
            default: fail("invalid escape");
            }
        }
    }

    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    Value parse_number() {
        const std::size_t start = pos_;
        bool is_integer = true;
        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;
        } else if (is_digit(peek())) {
            while (is_digit(peek())) ++pos_;
        } else {
            fail("invalid value");
        }
        if (peek() == '.') {
            is_integer = false;
            ++pos_;
            if (!is_digit(peek())) fail("invalid number");
            while (is_digit(peek())) ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            is_integer = false;
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!is_digit(peek())) fail("invalid number");
            while (is_digit(peek())) ++pos_;
        }
        const std::string token = text_.substr(start, pos_ - start);
        if (is_integer) {
            errno = 0;
            const long long number = std::strtoll(token.c_str(), nullptr, 10);
            if (errno != ERANGE) return Value(static_cast<std::int64_t>(number));
        }
        const double number = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(number)) fail("number out of range");
        return Value(number);
    }
};

void write_string(std::string& out, const std::string& text) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0xF];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

std::string format_double(double number) {
    if (!std::isfinite(number))
        throw LanguageError("json.stringify cannot represent NaN or infinity");
    for (int precision = 15; precision <= 17; ++precision) {
        std::ostringstream stream;
        stream << std::setprecision(precision) << number;
        std::string text = stream.str();
        if (precision == 17 || std::strtod(text.c_str(), nullptr) == number) {
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";
            return text;
        }
    }
    return {};
}

void write_value(std::string& out, const Value& value, bool pretty, int level) {
    const auto newline = [&](int depth) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
    };
    if (level > kMaxDepth) throw LanguageError("json.stringify: nesting is too deep");

    if (std::holds_alternative<std::monostate>(value.data)) {
        out += "null";
    } else if (const auto* item = std::get_if<bool>(&value.data)) {
        out += *item ? "true" : "false";
    } else if (const auto* item = std::get_if<std::int64_t>(&value.data)) {
        out += std::to_string(*item);
    } else if (const auto* item = std::get_if<double>(&value.data)) {
        out += format_double(*item);
    } else if (const auto* item = std::get_if<std::string>(&value.data)) {
        write_string(out, *item);
    } else if (const auto* item = std::get_if<std::shared_ptr<EnumValue>>(&value.data)) {
        write_string(out, (*item)->member);
    } else if (const auto* item = std::get_if<std::shared_ptr<ListValue>>(&value.data)) {
        const auto& elements = (*item)->elements;
        if (elements.empty()) { out += "[]"; return; }
        out += '[';
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (i) out += ',';
            newline(level + 1);
            write_value(out, elements[i], pretty, level + 1);
        }
        newline(level);
        out += ']';
    } else if (const auto* item = std::get_if<std::shared_ptr<MapValue>>(&value.data)) {
        const auto& entries = (*item)->entries;
        if (entries.empty()) { out += "{}"; return; }
        out += '{';
        bool first = true;
        for (const auto& entry : entries) {
            const auto* key = std::get_if<std::string>(&entry.first.data);
            if (!key) throw LanguageError("json.stringify: map keys must be strings");
            if (!first) out += ',';
            first = false;
            newline(level + 1);
            write_string(out, *key);
            out += pretty ? ": " : ":";
            write_value(out, entry.second, pretty, level + 1);
        }
        newline(level);
        out += '}';
    } else if (const auto* item = std::get_if<std::shared_ptr<StructValue>>(&value.data)) {
        const auto& fields = (*item)->fields;
        if (fields.empty()) { out += "{}"; return; }
        out += '{';
        bool first = true;
        for (const auto& field : fields) {
            if (!first) out += ',';
            first = false;
            newline(level + 1);
            write_string(out, field.first);
            out += pretty ? ": " : ":";
            write_value(out, field.second, pretty, level + 1);
        }
        newline(level);
        out += '}';
    } else {
        throw LanguageError("json.stringify cannot represent a " +
                            value_type_name(value) + " value");
    }
}

Value native(const std::string& name, std::size_t min, std::size_t max,
             std::function<Value(const std::vector<Value>&)> body) {
    return Value(std::make_shared<NativeFunction>(
        NativeFunction{name, min, max, std::move(body)}));
}

const std::string& require_text(const std::vector<Value>& args, const char* fn) {
    const auto* text = std::get_if<std::string>(&args[0].data);
    if (!text) throw LanguageError(std::string(fn) + " expects a string argument");
    return *text;
}

std::string& last_error() {
    static std::string error;
    return error;
}
} // namespace

std::string json_stringify(const Value& value, bool pretty) {
    std::string out;
    write_value(out, value, pretty, 0);
    return out;
}

void install_json(Runtime& runtime) {
    auto json = std::make_shared<StructValue>();
    json->type_name = "json";

    // json.parse(text) -> value, or null when the text is not valid JSON.
    json->fields["parse"] = native("json.parse", 1, 1, [](const std::vector<Value>& args) {
        const std::string& text = require_text(args, "json.parse");
        try {
            Value result = JsonParser(text).parse_document();
            last_error().clear();
            return result;
        } catch (const ParseError& error) {
            last_error() = error.message;
            return Value{};
        }
    });
    json->fields["valid"] = native("json.valid", 1, 1, [](const std::vector<Value>& args) {
        const std::string& text = require_text(args, "json.valid");
        try {
            JsonParser(text).parse_document();
            return Value(true);
        } catch (const ParseError&) {
            return Value(false);
        }
    });
    json->fields["lastError"] = native("json.lastError", 0, 0, [](const std::vector<Value>&) {
        return Value(last_error());
    });
    json->fields["stringify"] = native("json.stringify", 1, 2, [](const std::vector<Value>& args) {
        bool pretty = false;
        if (args.size() == 2) {
            const auto* flag = std::get_if<bool>(&args[1].data);
            if (!flag) throw LanguageError("json.stringify expects a bool for 'pretty'");
            pretty = *flag;
        }
        return Value(json_stringify(args[0], pretty));
    });

    // Lookup helpers that return null instead of failing on a missing key.
    json->fields["get"] = native("json.get", 2, 2, [](const std::vector<Value>& args) {
        if (const auto* object = std::get_if<std::shared_ptr<StructValue>>(&args[0].data)) {
            const auto* key = std::get_if<std::string>(&args[1].data);
            if (!key) throw LanguageError("json.get expects a string key for an object");
            auto found = (*object)->fields.find(*key);
            return found == (*object)->fields.end() ? Value{} : found->second;
        }
        if (const auto* list = std::get_if<std::shared_ptr<ListValue>>(&args[0].data)) {
            const auto* index = std::get_if<std::int64_t>(&args[1].data);
            if (!index) throw LanguageError("json.get expects an int index for a list");
            std::int64_t i = *index;
            const auto size = static_cast<std::int64_t>((*list)->elements.size());
            if (i < 0) i += size;
            return (i < 0 || i >= size) ? Value{}
                                        : (*list)->elements[static_cast<std::size_t>(i)];
        }
        return Value{};
    });
    json->fields["has"] = native("json.has", 2, 2, [](const std::vector<Value>& args) {
        const auto* object = std::get_if<std::shared_ptr<StructValue>>(&args[0].data);
        const auto* key = std::get_if<std::string>(&args[1].data);
        if (!key) throw LanguageError("json.has expects a string key");
        return Value(object && (*object)->fields.count(*key) > 0);
    });
    json->fields["keys"] = native("json.keys", 1, 1, [](const std::vector<Value>& args) {
        const auto* object = std::get_if<std::shared_ptr<StructValue>>(&args[0].data);
        if (!object) throw LanguageError("json.keys expects an object");
        auto keys = std::make_shared<ListValue>();
        for (const auto& field : (*object)->fields) keys->elements.emplace_back(field.first);
        return Value(keys);
    });

    runtime.globals->define_builtin("json", Value(json));
}
} // namespace dk
