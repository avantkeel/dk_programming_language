#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dk {
struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

struct LiteralExpr { std::variant<std::monostate, bool, std::int64_t, double, std::string> value; };
struct VariableExpr { std::string name; };
struct ListExpr { std::vector<ExprPtr> elements; };
struct UnaryExpr { std::string op; ExprPtr operand; };
struct BinaryExpr { ExprPtr left; std::string op; ExprPtr right; };
struct AssignmentExpr { ExprPtr target; std::string op; ExprPtr value; };
struct CallExpr { ExprPtr callee; std::vector<ExprPtr> arguments; };
struct PropertyExpr { ExprPtr object; std::string name; };
struct IndexExpr { ExprPtr object; ExprPtr index; };
struct NewExpr { std::string class_name; std::vector<ExprPtr> arguments; };
struct StructLiteralExpr { std::string type_name; std::map<std::string, ExprPtr> fields; };
struct RangeExpr { ExprPtr start; ExprPtr end; };
struct MapExpr { std::vector<std::pair<ExprPtr, ExprPtr>> entries; };

struct Expr {
    using Node = std::variant<LiteralExpr, VariableExpr, ListExpr, UnaryExpr,
                              BinaryExpr, AssignmentExpr, CallExpr, PropertyExpr,
                              IndexExpr, NewExpr, StructLiteralExpr, RangeExpr,
                              MapExpr>;
    Node node;
    template <typename T> explicit Expr(T value) : node(std::move(value)) {}
};

struct Parameter { std::string name; std::optional<std::string> type; };
struct VariableDecl { std::string name; std::optional<std::string> type; ExprPtr initializer; };
struct TypeAliasDecl { std::string name; std::string target; };
struct ImportDecl { std::vector<std::string> names; std::string path; };
struct StructDecl { std::string name; std::vector<VariableDecl> fields; };
struct EnumDecl { std::string name; std::vector<std::string> members; };
struct FunctionDecl {
    std::string name;
    std::vector<Parameter> parameters;
    std::optional<std::string> return_type;
    std::vector<StmtPtr> body;
    bool is_override = false;
};
struct ClassDecl {
    std::string name;
    std::optional<std::string> parent;
    std::vector<VariableDecl> fields;
    std::shared_ptr<FunctionDecl> constructor;
    std::vector<std::shared_ptr<FunctionDecl>> methods;
};
struct ExportStmt { StmtPtr declaration; };
struct BlockStmt { std::vector<StmtPtr> statements; };
struct IfStmt { ExprPtr condition; StmtPtr then_branch; StmtPtr else_branch; };
struct SwitchCase { ExprPtr value; std::vector<StmtPtr> body; };
struct SwitchStmt { ExprPtr value; std::vector<SwitchCase> cases; std::optional<std::vector<StmtPtr>> default_body; };
struct WhileStmt { ExprPtr condition; StmtPtr body; };
struct ForStmt { std::string name; ExprPtr iterable; StmtPtr body; };
struct ReturnStmt { ExprPtr value; };
struct BreakStmt {};
struct ExprStmt { ExprPtr expression; };

struct Stmt {
    using Node = std::variant<VariableDecl, TypeAliasDecl, ImportDecl, StructDecl,
                              EnumDecl, std::shared_ptr<FunctionDecl>, ClassDecl,
                              ExportStmt, BlockStmt, IfStmt, SwitchStmt, WhileStmt, ForStmt,
                              ReturnStmt, BreakStmt, ExprStmt>;
    Node node;
    template <typename T> explicit Stmt(T value) : node(std::move(value)) {}
};

} // namespace dk

