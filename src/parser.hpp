#pragma once

#include "lexer.hpp"
#include "ast.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>

namespace dk {
class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::vector<StmtPtr> parse() {
        std::vector<StmtPtr> statements;
        while (!check("EOF")) statements.push_back(declaration());
        return statements;
    }

private:
    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    const std::map<std::string, int> precedence_{
        {"=", 1}, {"+=", 1}, {"-=", 1}, {"*=", 1}, {"/=", 1},
        {"??", 2}, {"||", 3}, {"&&", 4}, {"==", 5}, {"!=", 5},
        {"<", 6}, {"<=", 6}, {">", 6}, {">=", 6}, {"..", 7},
        {"+", 8}, {"-", 8}, {"*", 9}, {"/", 9}, {"%", 9}};

    const Token& peek() const { return tokens_[position_]; }
    const Token& previous() const { return tokens_[position_ - 1]; }
    const Token& peek_next() const {
        return tokens_[std::min(position_ + 1, tokens_.size() - 1)];
    }
    bool check(const std::string& kind) const { return peek().kind == kind; }
    bool match(const std::string& kind) {
        if (!check(kind)) return false;
        advance();
        return true;
    }
    const Token& advance() {
        if (!check("EOF")) ++position_;
        return previous();
    }
    const Token& consume(const std::string& kind, const std::string& message) {
        if (check(kind)) return advance();
        fail(peek(), message);
    }
    [[noreturn]] static void fail(const Token& token, const std::string& message) {
        throw LanguageError(message + " at line " + std::to_string(token.line) +
                            ", column " + std::to_string(token.column));
    }
    static ExprPtr expr(Expr::Node node) { return std::make_shared<Expr>(std::visit(
        [](auto&& item) -> Expr::Node { return std::forward<decltype(item)>(item); },
        std::move(node))); }
    static StmtPtr stmt(Stmt::Node node) {
        return std::make_shared<Stmt>(std::visit(
            [](auto&& item) -> Stmt::Node { return std::forward<decltype(item)>(item); },
            std::move(node)));
    }

    StmtPtr declaration() {
        if (match("import")) return import_declaration();
        if (match("export")) {
            auto inner = declaration();
            if (std::holds_alternative<ImportDecl>(inner->node) ||
                std::holds_alternative<ExportStmt>(inner->node) ||
                std::holds_alternative<BlockStmt>(inner->node) ||
                std::holds_alternative<IfStmt>(inner->node) ||
                std::holds_alternative<SwitchStmt>(inner->node) ||
                std::holds_alternative<WhileStmt>(inner->node) ||
                std::holds_alternative<ForStmt>(inner->node) ||
                std::holds_alternative<ReturnStmt>(inner->node) ||
                std::holds_alternative<BreakStmt>(inner->node) ||
                std::holds_alternative<ExprStmt>(inner->node)) {
                fail(previous(), "Export must precede a declaration");
            }
            return stmt(ExportStmt{inner});
        }
        if (match("type")) return type_alias();
        if (match("struct")) return struct_declaration();
        if (match("enum")) return enum_declaration();
        if (match("class")) return class_declaration();
        if (match("function")) return stmt(function_declaration(false));
        if (match("variable")) return variable_declaration(true);
        return statement();
    }

    StmtPtr import_declaration() {
        consume("{", "Expected '{' after import");
        std::vector<std::string> names{
            consume("IDENTIFIER", "Expected an imported name").value};
        while (match(",")) names.push_back(
            consume("IDENTIFIER", "Expected an imported name").value);
        std::set<std::string> unique(names.begin(), names.end());
        if (unique.size() != names.size()) fail(previous(), "An import cannot contain duplicate names");
        consume("}", "Expected '}' after imported names");
        consume("from", "Expected 'from' after imported names");
        const std::string path = consume("STRING", "Expected a module path string").value;
        consume(";", "Expected ';' after import");
        return stmt(ImportDecl{std::move(names), path});
    }

    StmtPtr type_alias() {
        const auto name = consume("IDENTIFIER", "Expected a type name").value;
        consume("=", "Expected '=' after type name");
        const auto target = type_name();
        consume(";", "Expected ';' after type alias");
        return stmt(TypeAliasDecl{name, target});
    }

    StmtPtr struct_declaration() {
        const auto name = consume("IDENTIFIER", "Expected a struct name").value;
        consume("{", "Expected '{' after struct name");
        std::vector<VariableDecl> fields;
        while (!check("}") && !check("EOF")) {
            consume("variable", "Struct members must be variable declarations");
            fields.push_back(std::get<VariableDecl>(variable_declaration(true)->node));
        }
        consume("}", "Expected '}' after struct fields");
        return stmt(StructDecl{name, std::move(fields)});
    }

    StmtPtr enum_declaration() {
        const auto name = consume("IDENTIFIER", "Expected an enum name").value;
        consume("{", "Expected '{' after enum name");
        std::vector<std::string> members;
        if (!check("}")) {
            do {
                members.push_back(consume("IDENTIFIER", "Expected an enum member").value);
            } while (match(",") && !check("}"));
        }
        consume("}", "Expected '}' after enum members");
        return stmt(EnumDecl{name, std::move(members)});
    }

    StmtPtr class_declaration() {
        const auto name = consume("IDENTIFIER", "Expected a class name").value;
        std::optional<std::string> parent;
        if (match("extends")) parent = consume("IDENTIFIER", "Expected a parent class name").value;
        consume("{", "Expected '{' after class name");
        ClassDecl result;
        result.name = name;
        result.parent = parent;
        while (!check("}") && !check("EOF")) {
            if (match("variable")) {
                result.fields.push_back(std::get<VariableDecl>(variable_declaration(true)->node));
            } else if (match("constructor")) {
                if (result.constructor) fail(previous(), "A class can have only one constructor");
                result.constructor = constructor_declaration();
            } else {
                const bool is_override = match("override");
                consume("function", "Expected a field, constructor, or function");
                result.methods.push_back(function_declaration(is_override));
            }
        }
        consume("}", "Expected '}' after class body");
        return stmt(std::move(result));
    }

    std::shared_ptr<FunctionDecl> constructor_declaration() {
        auto result = std::make_shared<FunctionDecl>();
        result->name = "constructor";
        result->parameters = parameters();
        result->body = block_contents();
        return result;
    }

    std::shared_ptr<FunctionDecl> function_declaration(bool is_override) {
        auto result = std::make_shared<FunctionDecl>();
        result->name = consume("IDENTIFIER", "Expected a function name").value;
        result->parameters = parameters();
        if (match("->")) result->return_type = type_name();
        result->body = block_contents();
        result->is_override = is_override;
        return result;
    }

    std::vector<Parameter> parameters() {
        consume("(", "Expected '(' before parameters");
        std::vector<Parameter> result;
        if (!check(")")) {
            do {
                Parameter parameter;
                parameter.name = consume("IDENTIFIER", "Expected a parameter name").value;
                if (match(":")) parameter.type = type_name();
                result.push_back(std::move(parameter));
            } while (match(","));
        }
        consume(")", "Expected ')' after parameters");
        return result;
    }

    StmtPtr variable_declaration(bool require_semicolon) {
        VariableDecl declaration;
        declaration.name = consume("IDENTIFIER", "Expected a variable name").value;
        if (match(":")) declaration.type = type_name();
        if (match("=")) declaration.initializer = expression();
        if (!declaration.type && !declaration.initializer) {
            fail(previous(), "A variable needs a type or an initial value");
        }
        if (require_semicolon) consume(";", "Expected ';' after variable declaration");
        return stmt(std::move(declaration));
    }

    std::string type_name() {
        if (match("[")) {
            const auto inner = type_name();
            consume("]", "Expected ']' after list element type");
            return "[" + inner + "]";
        }
        return consume("IDENTIFIER", "Expected a type name").value;
    }

    StmtPtr statement() {
        if (match("{")) return stmt(BlockStmt{block_contents()});
        if (match("if")) return if_statement();
        if (match("switch")) return switch_statement();
        if (match("while")) return while_statement();
        if (match("for")) return for_statement();
        if (match("return")) {
            ExprPtr value;
            if (!check(";")) value = expression();
            consume(";", "Expected ';' after return");
            return stmt(ReturnStmt{value});
        }
        if (match("break")) {
            consume(";", "Expected ';' after break");
            return stmt(BreakStmt{});
        }
        auto value = expression();
        consume(";", "Expected ';' after expression");
        return stmt(ExprStmt{value});
    }

    std::vector<StmtPtr> block_contents() {
        if (previous().kind != "{") consume("{", "Expected '{'");
        std::vector<StmtPtr> statements;
        while (!check("}") && !check("EOF")) statements.push_back(declaration());
        consume("}", "Expected '}' after block");
        return statements;
    }

    StmtPtr if_statement() {
        auto condition = expression();
        auto then_branch = statement();
        StmtPtr otherwise;
        if (match("else")) {
            otherwise = match("if") ? if_statement() : statement();
        }
        return stmt(IfStmt{condition, then_branch, otherwise});
    }
    StmtPtr switch_statement() {
        auto value = expression();
        consume("{", "Expected '{' after switch value");

        std::vector<SwitchCase> cases;
        std::optional<std::vector<StmtPtr>> default_body;
        while (!check("}") && !check("EOF")) {
            if (match("case")) {
                auto case_value = expression();
                consume("{", "Expected '{' after case value");
                cases.push_back(SwitchCase{case_value, block_contents()});
            } else if (match("default")) {
                if (default_body) fail(previous(), "A switch can have only one default case");
                consume("{", "Expected '{' after default");
                default_body = block_contents();
            } else {
                fail(peek(), "Expected 'case' or 'default' inside switch");
            }
        }
        consume("}", "Expected '}' after switch cases");
        return stmt(SwitchStmt{value, std::move(cases), std::move(default_body)});
    }
    StmtPtr while_statement() {
        auto condition = expression();
        return stmt(WhileStmt{condition, statement()});
    }
    StmtPtr for_statement() {
        auto name = consume("IDENTIFIER", "Expected a loop variable").value;
        consume("in", "Expected 'in' after loop variable");
        auto iterable = expression();
        return stmt(ForStmt{name, iterable, statement()});
    }

    ExprPtr expression(int minimum = 1) {
        auto left = postfix(prefix());
        while (true) {
            const auto found = precedence_.find(peek().kind);
            if (found == precedence_.end() || found->second < minimum) break;
            const auto op = advance().kind;
            const int next = (op == "=" || op == "+=" || op == "-=" ||
                              op == "*=" || op == "/=" || op == "??")
                                 ? found->second : found->second + 1;
            auto right = expression(next);
            if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=")
                left = expr(AssignmentExpr{left, op, right});
            else if (op == "..")
                left = expr(RangeExpr{left, right});
            else
                left = expr(BinaryExpr{left, op, right});
        }
        return left;
    }

    ExprPtr prefix() {
        if (match("!") || match("-")) return expr(UnaryExpr{previous().kind, expression(10)});
        if (match("true")) return expr(LiteralExpr{true});
        if (match("false")) return expr(LiteralExpr{false});
        if (match("null")) return expr(LiteralExpr{std::monostate{}});
        if (match("INTEGER")) return expr(LiteralExpr{static_cast<std::int64_t>(std::stoll(previous().value))});
        if (match("FLOAT")) return expr(LiteralExpr{std::stod(previous().value)});
        if (match("STRING")) return expr(LiteralExpr{previous().value});
        if (match("this")) return expr(VariableExpr{"this"});
        if (match("super")) return expr(VariableExpr{"super"});
        if (match("new")) {
            const auto name = consume("IDENTIFIER", "Expected a class name after new").value;
            return expr(NewExpr{name, arguments()});
        }
        if (match("IDENTIFIER")) return expr(VariableExpr{previous().value});
        if (match("(")) {
            auto value = expression();
            consume(")", "Expected ')' after expression");
            return value;
        }
        if (match("[")) {
            std::vector<ExprPtr> elements;
            if (!check("]")) {
                do { elements.push_back(expression()); } while (match(","));
            }
            consume("]", "Expected ']' after list");
            return expr(ListExpr{std::move(elements)});
        }
        fail(peek(), "Expected an expression");
    }

    ExprPtr postfix(ExprPtr value) {
        while (true) {
            if (match("(")) {
                value = expr(CallExpr{value, arguments_after_open()});
            } else if (match(".")) {
                value = expr(PropertyExpr{value, consume("IDENTIFIER", "Expected a property name").value});
            } else if (match("[")) {
                auto index = expression();
                consume("]", "Expected ']' after index");
                value = expr(IndexExpr{value, index});
            } else if (std::holds_alternative<VariableExpr>(value->node) &&
                       check("{") && peek_next().kind == "IDENTIFIER" &&
                       tokens_[std::min(position_ + 2, tokens_.size() - 1)].kind == ":") {
                advance();
                std::map<std::string, ExprPtr> fields;
                while (!check("}") && !check("EOF")) {
                    auto name = consume("IDENTIFIER", "Expected a field name").value;
                    consume(":", "Expected ':' after field name");
                    fields[name] = expression();
                    if (!match(",")) break;
                }
                consume("}", "Expected '}' after struct fields");
                value = expr(StructLiteralExpr{std::get<VariableExpr>(value->node).name,
                                               std::move(fields)});
            } else break;
        }
        return value;
    }

    std::vector<ExprPtr> arguments() {
        consume("(", "Expected '(' before arguments");
        return arguments_after_open();
    }
    std::vector<ExprPtr> arguments_after_open() {
        std::vector<ExprPtr> result;
        if (!check(")")) {
            do { result.push_back(expression()); } while (match(","));
        }
        consume(")", "Expected ')' after arguments");
        return result;
    }
};

inline std::vector<StmtPtr> parse(const std::string& source) {
    return Parser(Lexer(source).tokenize()).parse();
}

} // namespace dk

