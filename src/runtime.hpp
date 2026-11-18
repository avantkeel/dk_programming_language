#pragma once

#include "parser.hpp"

#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <fstream>
#include <set>
#include <type_traits>
#include <cstdlib>

namespace dk {
struct Value;
struct ListValue;
struct StructValue;
struct EnumTypeValue;
struct EnumValue;
struct ClassValue;
struct ClassInstance;
struct FunctionValue;
struct NativeFunction;
struct SuperReference;
struct Runtime;

struct RangeValue { std::int64_t start; std::int64_t end; };
using ValueList = std::vector<Value>;

struct Value {
    using Data = std::variant<std::monostate, bool, std::int64_t, double,
                              std::string, std::shared_ptr<ListValue>,
                              std::shared_ptr<StructValue>,
                              std::shared_ptr<EnumValue>,
                              std::shared_ptr<ClassInstance>,
                              std::shared_ptr<FunctionValue>,
                              std::shared_ptr<NativeFunction>,
                              std::shared_ptr<ClassValue>,
                              std::shared_ptr<EnumTypeValue>,
                              std::shared_ptr<SuperReference>, RangeValue>;
    Data data;
    Value() = default;
    template <typename T> explicit Value(T item) : data(std::move(item)) {}
};

struct ListValue { ValueList elements; };
struct Binding { Value value; std::optional<std::string> type; };

struct Environment : std::enable_shared_from_this<Environment> {
    std::shared_ptr<Environment> parent;
    std::map<std::string, Binding> values;

    explicit Environment(std::shared_ptr<Environment> enclosing = {})
        : parent(std::move(enclosing)) {}
    void define(const std::string& name, Value value,
                std::optional<std::string> type = {}) {
        values[name] = Binding{std::move(value), std::move(type)};
    }
    Binding& find(const std::string& name) {
        auto found = values.find(name);
        if (found != values.end()) return found->second;
        if (parent) return parent->find(name);
        throw LanguageError("Undefined name '" + name + "'");
    }
    const Binding& find(const std::string& name) const {
        auto found = values.find(name);
        if (found != values.end()) return found->second;
        if (parent) return parent->find(name);
        throw LanguageError("Undefined name '" + name + "'");
    }
};

struct StructType { std::string name; std::vector<VariableDecl> fields; Runtime* runtime; };
struct EnumTypeValue { std::string name; std::set<std::string> members; };
struct EnumValue { std::string type_name; std::string member; };
struct StructValue { std::string type_name; std::map<std::string, Value> fields; };

struct FunctionValue {
    std::shared_ptr<FunctionDecl> declaration;
    std::shared_ptr<Environment> closure;
    Runtime* runtime;
    std::shared_ptr<ClassValue> owner;
    std::shared_ptr<ClassInstance> receiver;

    Value call(const std::vector<Value>& arguments);
    std::shared_ptr<FunctionValue> bind(const std::shared_ptr<ClassInstance>& instance);
};

struct NativeFunction {
    std::string name;
    std::size_t minimum_arguments;
    std::optional<std::size_t> maximum_arguments;
    std::function<Value(const std::vector<Value>&)> implementation;

    Value call(const std::vector<Value>& arguments) const {
        if (arguments.size() < minimum_arguments)
            throw LanguageError(name + " expects at least " +
                                std::to_string(minimum_arguments) + " argument(s)");
        if (maximum_arguments && arguments.size() > *maximum_arguments)
            throw LanguageError(name + " expects at most " +
                                std::to_string(*maximum_arguments) + " argument(s)");
        return implementation(arguments);
    }
};

struct ClassValue : std::enable_shared_from_this<ClassValue> {
    ClassDecl declaration;
    std::shared_ptr<Environment> closure;
    Runtime* runtime;
    std::shared_ptr<ClassValue> parent;
    std::map<std::string, std::shared_ptr<FunctionValue>> methods;
    std::shared_ptr<FunctionValue> constructor;

    std::string name() const { return declaration.name; }
    std::shared_ptr<FunctionValue> find_method(const std::string& method) const;
    std::shared_ptr<ClassInstance> create(const std::vector<Value>& arguments);
};

struct ClassInstance : std::enable_shared_from_this<ClassInstance> {
    std::shared_ptr<ClassValue> class_value;
    std::map<std::string, Binding> fields;

    Value get(const std::string& name);
    void set(const std::string& name, Value value);
    bool is_instance_of(const std::string& name) const;
};

struct SuperReference {
    std::shared_ptr<ClassInstance> instance;
    std::shared_ptr<ClassValue> parent;

    Value call(const std::vector<Value>& arguments);
    Value get(const std::string& name);
};

struct ReturnSignal { Value value; };
struct BreakSignal {};

std::string value_type_name(const Value& value);
std::string display_value(const Value& value);
bool values_equal(const Value& left, const Value& right);

struct Runtime {
    std::ostream& output;
    std::istream& input;
    std::shared_ptr<Environment> globals = std::make_shared<Environment>();
    std::shared_ptr<Environment> environment = globals;
    std::map<std::string, std::string> type_aliases;
    std::map<std::string, StructType> struct_types;
    std::map<std::string, std::shared_ptr<EnumTypeValue>> enum_types;
    std::map<std::string, std::shared_ptr<ClassValue>> classes;
    std::filesystem::path source_path;
    std::map<std::string, StmtPtr> module_exports;
    std::map<std::filesystem::path, std::shared_ptr<Runtime>> owned_modules;
    std::map<std::filesystem::path, std::shared_ptr<Runtime>>* module_cache;
    std::set<std::filesystem::path> loading_modules;
    std::set<std::filesystem::path>* shared_loading;

    Runtime(std::ostream& out, std::istream& in)
        : output(out), input(in), module_cache(&owned_modules),
          shared_loading(&loading_modules) {
        install_terminal();
    }
    Runtime(std::ostream& out, std::istream& in,
            std::map<std::filesystem::path, std::shared_ptr<Runtime>>& cache,
            std::set<std::filesystem::path>& loading)
        : output(out), input(in), module_cache(&cache), shared_loading(&loading) {
        install_terminal();
    }

    void install_terminal();
    void run(const std::string& source, const std::filesystem::path& path);
    void execute_program(const std::vector<StmtPtr>& statements);
    void execute(const StmtPtr& statement);
    void execute_block(const std::vector<StmtPtr>& statements,
                       std::shared_ptr<Environment> scope);
    void execute_variable(const VariableDecl& declaration);
    void register_type_alias(const TypeAliasDecl& declaration);
    void register_struct(const StructDecl& declaration);
    void register_enum(const EnumDecl& declaration);
    void register_class(const ClassDecl& declaration);
    void execute_while(const WhileStmt& statement);
    void execute_for(const ForStmt& statement);
    Value evaluate(const ExprPtr& expression);
    Value evaluate_unary(const UnaryExpr& expression);
    Value evaluate_binary(const BinaryExpr& expression);
    Value evaluate_assignment(const AssignmentExpr& expression);
    Value apply_compound(const Value& current, const Value& value,
                         const std::string& op);
    Value evaluate_call(const CallExpr& expression);
    Value get_property(const Value& owner, const std::string& name);
    void set_property(const Value& owner, const std::string& name, Value value);
    Value get_index(const IndexExpr& expression);
    Value create_struct(const StructLiteralExpr& expression);
    void import_module(const ImportDecl& declaration);
    void initialize_fields(const std::shared_ptr<ClassInstance>& instance,
                           const std::shared_ptr<ClassValue>& class_value);
    Value evaluate_field_initializer(const std::shared_ptr<ClassInstance>& instance,
                                     const std::shared_ptr<ClassValue>& owner,
                                     const VariableDecl& field);
    void check_type(const Value& value, const std::optional<std::string>& type,
                    const std::string& description);
    bool matches_type(const Value& value, const std::string& type);
    std::string resolve_type(const std::string& type);
    Value default_value(const std::optional<std::string>& type);
    bool type_exists(const std::string& name) const;
    void ensure_type_available(const std::string& name) const;
    static bool require_boolean(const Value& value);
    static bool is_number(const Value& value);
    Value numeric_operation(const Value& left, const Value& right,
                            const std::string& op);
};

} // namespace dk

