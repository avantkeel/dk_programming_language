#pragma once

#include <cctype>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace dk {

class LanguageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Token {
    std::string kind;
    std::string value;
    int line;
    int column;
};

class Lexer {
public:
    explicit Lexer(const std::string& source) : source_(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (!at_end()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) {
                advance();
            } else if (c == '/' && peek_next() == '/') {
                while (!at_end() && peek() != '\n') advance();
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokens.push_back(identifier());
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(number());
            } else if (c == '"') {
                tokens.push_back(string_token());
            } else {
                tokens.push_back(punctuation());
            }
        }
        tokens.push_back({"EOF", "", line_, column_});
        return tokens;
    }

private:
    const std::string& source_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;

    bool at_end() const { return position_ >= source_.size(); }
    char peek() const { return source_[position_]; }
    char peek_next() const {
        return position_ + 1 < source_.size() ? source_[position_ + 1] : '\0';
    }
    char advance() {
        const char c = source_[position_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    Token identifier() {
        const int line = line_, column = column_;
        const auto start = position_;
        while (!at_end() &&
               (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }
        std::string value = source_.substr(start, position_ - start);
        static const std::set<std::string> keywords{
            "break", "class", "constructor", "else", "enum", "extends",
            "export", "false", "for", "from", "function", "if", "import",
            "in", "new", "null", "override", "return", "struct", "super",
            "this", "true", "type", "variable", "while"};
        return {keywords.count(value) ? value : "IDENTIFIER", value, line, column};
    }

    Token number() {
        const int line = line_, column = column_;
        const auto start = position_;
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        std::string kind = "INTEGER";
        if (!at_end() && peek() == '.' && peek_next() != '.' &&
            std::isdigit(static_cast<unsigned char>(peek_next()))) {
            kind = "FLOAT";
            advance();
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
        return {kind, source_.substr(start, position_ - start), line, column};
    }

    Token string_token() {
        const int line = line_, column = column_;
        advance();
        std::string value;
        while (!at_end() && peek() != '"') {
            char c = advance();
            if (c == '\n') {
                throw LanguageError("Unterminated string at line " +
                                    std::to_string(line) + ", column " +
                                    std::to_string(column));
            }
            if (c == '\\') {
                if (at_end()) break;
                const char escaped = advance();
                switch (escaped) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default:
                    throw LanguageError("Unknown escape '\\\\" +
                                        std::string(1, escaped) + "' at line " +
                                        std::to_string(line_) + ", column " +
                                        std::to_string(column_ - 1));
                }
            }
            value.push_back(c);
        }
        if (at_end()) {
            throw LanguageError("Unterminated string at line " +
                                std::to_string(line) + ", column " +
                                std::to_string(column));
        }
        advance();
        return {"STRING", value, line, column};
    }

    Token punctuation() {
        const int line = line_, column = column_;
        static const std::vector<std::string> operators{
            "->", "==", "!=", "<=", ">=", "&&", "||", "??",
            "+=", "-=", "*=", "/=", ".."};
        for (const auto& op : operators) {
            if (source_.compare(position_, op.size(), op) == 0) {
                for (std::size_t i = 0; i < op.size(); ++i) advance();
                return {op, op, line, column};
            }
        }
        const char c = advance();
        const std::string singles = "{}()[];,:.+-*/%<>=!";
        if (singles.find(c) != std::string::npos) {
            return {std::string(1, c), std::string(1, c), line, column};
        }
        throw LanguageError("Unexpected character '" + std::string(1, c) +
                            "' at line " + std::to_string(line) + ", column " +
                            std::to_string(column));
    }
};

} // namespace dk

