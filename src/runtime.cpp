#include "runtime.hpp"

#include <cmath>
#include <iomanip>
#include <iterator>

namespace dk {
void Runtime::install_terminal() {
    auto terminal = std::make_shared<StructValue>();
    terminal->type_name = "terminal";
    terminal->fields["print"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"terminal.print", 1, 1, [this](const std::vector<Value>& args) {
            output << display_value(args[0]) << '\n';
            return Value{};
        }}));
    terminal->fields["read"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"terminal.read", 0, 0, [this](const std::vector<Value>&) {
            std::string line;
            if (!std::getline(input, line)) return Value{};
            return Value(std::move(line));
        }}));
    terminal->fields["readInt"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"terminal.readInt", 0, 0, [this](const std::vector<Value>&) {
            std::string line;
            if (!std::getline(input, line)) return Value{};
            std::istringstream parser(line);
            std::int64_t number;
            std::string extra;
            if (!(parser >> number) || (parser >> extra)) return Value{};
            return Value(number);
        }}));
    globals->define_builtin("terminal", Value(terminal));
}

void Runtime::run(const std::string& source, const std::filesystem::path& path) {
    if (!path.empty()) {
        std::error_code error;
        source_path = std::filesystem::absolute(path, error).lexically_normal();
        if (error) source_path = path.lexically_normal();
    }
    execute_program(parse(source));
}

void Runtime::execute_program(const std::vector<StmtPtr>& statements) {
    try {
        for (const auto& statement : statements) execute(statement);
    } catch (const ReturnSignal&) {
        throw LanguageError("return can only be used inside a function");
    } catch (const BreakSignal&) {
        throw LanguageError("break can only be used inside a loop");
    }
    auto main = globals->values.find("main");
    if (main != globals->values.end()) {
        auto function = std::get_if<std::shared_ptr<FunctionValue>>(&main->second.value.data);
        if (function == nullptr) throw LanguageError("'main' must be a function");
        (*function)->call({});
    }
}

void Runtime::execute(const StmtPtr& statement) {
    std::visit([this](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, VariableDecl>) {
            execute_variable(node);
        } else if constexpr (std::is_same_v<T, ImportDecl>) {
            import_module(node);
        } else if constexpr (std::is_same_v<T, ExportStmt>) {
            execute(node.declaration);
        } else if constexpr (std::is_same_v<T, TypeAliasDecl>) {
            register_type_alias(node);
        } else if constexpr (std::is_same_v<T, StructDecl>) {
            register_struct(node);
        } else if constexpr (std::is_same_v<T, EnumDecl>) {
            register_enum(node);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionDecl>>) {
            environment->define(node->name, Value(std::make_shared<FunctionValue>(
                FunctionValue{node, environment, this, {}, {}})));
        } else if constexpr (std::is_same_v<T, ClassDecl>) {
            register_class(node);
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
            execute_block(node.statements, std::make_shared<Environment>(environment));
        } else if constexpr (std::is_same_v<T, ExprStmt>) {
            evaluate(node.expression);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
            if (require_boolean(evaluate(node.condition))) execute(node.then_branch);
            else if (node.else_branch) execute(node.else_branch);
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            execute_while(node);
        } else if constexpr (std::is_same_v<T, ForStmt>) {
            execute_for(node);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            throw ReturnSignal{node.value ? evaluate(node.value) : Value{}};
        } else if constexpr (std::is_same_v<T, BreakStmt>) {
            throw BreakSignal{};
        }
    }, statement->node);
}

void Runtime::execute_block(const std::vector<StmtPtr>& statements,
                            std::shared_ptr<Environment> scope) {
    auto previous = environment;
    environment = std::move(scope);
    try {
        for (const auto& statement : statements) execute(statement);
    } catch (...) {
        environment = std::move(previous);
        throw;
    }
    environment = std::move(previous);
}

void Runtime::execute_variable(const VariableDecl& declaration) {
    Value value;
    if (declaration.initializer) {
        value = evaluate(declaration.initializer);
        check_type(value, declaration.type, "variable '" + declaration.name + "'");
    } else if (declaration.type) {
        value = default_value(declaration.type);
    }
    environment->define(declaration.name, std::move(value), declaration.type);
}

void Runtime::register_type_alias(const TypeAliasDecl& declaration) {
    if (type_aliases.count(declaration.name) || type_exists(declaration.name))
        throw LanguageError("Type '" + declaration.name + "' is already defined");
    type_aliases[declaration.name] = declaration.target;
    resolve_type(declaration.name);
}

void Runtime::register_struct(const StructDecl& declaration) {
    ensure_type_available(declaration.name);
    std::set<std::string> names;
    for (const auto& field : declaration.fields) {
        if (!names.insert(field.name).second)
            throw LanguageError("Struct '" + declaration.name + "' has duplicate fields");
    }
    struct_types[declaration.name] = StructType{declaration.name, declaration.fields, this};
}

void Runtime::register_enum(const EnumDecl& declaration) {
    ensure_type_available(declaration.name);
    auto type = std::make_shared<EnumTypeValue>();
    type->name = declaration.name;
    type->members.insert(declaration.members.begin(), declaration.members.end());
    if (type->members.size() != declaration.members.size())
        throw LanguageError("Enum '" + declaration.name + "' has duplicate members");
    enum_types[declaration.name] = type;
    globals->define(declaration.name, Value(type));
}

void Runtime::register_class(const ClassDecl& declaration) {
    ensure_type_available(declaration.name);
    std::shared_ptr<ClassValue> parent;
    if (declaration.parent) {
        auto found = classes.find(*declaration.parent);
        if (found == classes.end())
            throw LanguageError("Parent class '" + *declaration.parent + "' is not defined");
        parent = found->second;
    }
    std::set<std::string> parent_methods;
    for (auto current = parent; current; current = current->parent) {
        for (const auto& entry : current->methods) parent_methods.insert(entry.first);
    }
    std::set<std::string> names;
    for (const auto& method : declaration.methods) {
        if (!names.insert(method->name).second)
            throw LanguageError("Class '" + declaration.name + "' has duplicate functions");
        if (method->is_override && !parent_methods.count(method->name))
            throw LanguageError("Function '" + method->name +
                                "' is marked override but no parent function exists");
    }
    auto value = std::make_shared<ClassValue>();
    value->declaration = declaration;
    value->closure = environment;
    value->runtime = this;
    value->parent = parent;
    for (const auto& method : declaration.methods) {
        value->methods[method->name] =
            std::make_shared<FunctionValue>(FunctionValue{method, environment, this, value, {}});
    }
    if (declaration.constructor) {
        value->constructor = std::make_shared<FunctionValue>(
            FunctionValue{declaration.constructor, environment, this, value, {}});
    }
    classes[declaration.name] = value;
    globals->define(declaration.name, Value(value));
}

void Runtime::execute_while(const WhileStmt& statement) {
    while (require_boolean(evaluate(statement.condition))) {
        try {
            execute(statement.body);
        } catch (const BreakSignal&) {
            break;
        }
    }
}

void Runtime::execute_for(const ForStmt& statement) {
    const Value iterable = evaluate(statement.iterable);
    std::vector<Value> values;
    if (const auto* range = std::get_if<RangeValue>(&iterable.data)) {
        for (auto i = range->start; i < range->end; ++i) values.emplace_back(i);
    } else if (const auto* list =
                   std::get_if<std::shared_ptr<ListValue>>(&iterable.data)) {
        values = (*list)->elements;
    } else if (const auto* string = std::get_if<std::string>(&iterable.data)) {
        for (char c : *string) values.emplace_back(std::string(1, c));
    } else {
        throw LanguageError("A for loop needs a range, list, or string");
    }
    auto previous = environment;
    auto loop_scope = std::make_shared<Environment>(environment);
    environment = loop_scope;
    try {
        for (const auto& value : values) {
            loop_scope->define(statement.name, value);
            try {
                execute(statement.body);
            } catch (const BreakSignal&) {
                break;
            }
        }
    } catch (...) {
        environment = std::move(previous);
        throw;
    }
    environment = std::move(previous);
}

Value Runtime::evaluate(const ExprPtr& expression) {
    return std::visit([this](const auto& node) -> Value {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LiteralExpr>) {
            return std::visit([](const auto& item) -> Value {
                using V = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<V, std::monostate>) return Value{};
                else return Value(item);
            }, node.value);
        } else if constexpr (std::is_same_v<T, VariableExpr>) {
            return environment->find(node.name).value;
        } else if constexpr (std::is_same_v<T, ListExpr>) {
            auto list = std::make_shared<ListValue>();
            for (const auto& element : node.elements) list->elements.push_back(evaluate(element));
            return Value(list);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            return evaluate_unary(node);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            return evaluate_binary(node);
        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
            return evaluate_assignment(node);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            return evaluate_call(node);
        } else if constexpr (std::is_same_v<T, PropertyExpr>) {
            return get_property(evaluate(node.object), node.name);
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
            return get_index(node);
        } else if constexpr (std::is_same_v<T, NewExpr>) {
            auto found = classes.find(node.class_name);
            if (found == classes.end()) throw LanguageError("Unknown class '" + node.class_name + "'");
            std::vector<Value> arguments;
            for (const auto& argument : node.arguments) arguments.push_back(evaluate(argument));
            return Value(found->second->create(arguments));
        } else if constexpr (std::is_same_v<T, StructLiteralExpr>) {
            return create_struct(node);
        } else {
            const Value start = evaluate(node.start);
            const Value end = evaluate(node.end);
            const auto* first = std::get_if<std::int64_t>(&start.data);
            const auto* last = std::get_if<std::int64_t>(&end.data);
            if (!first || !last) throw LanguageError("Range endpoints must be integers");
            return Value(RangeValue{*first, *last});
        }
    }, expression->node);
}

Value Runtime::evaluate_unary(const UnaryExpr& expression) {
    const Value value = evaluate(expression.operand);
    if (expression.op == "!") return Value(!require_boolean(value));
    if (expression.op == "-") {
        if (const auto* integer = std::get_if<std::int64_t>(&value.data))
            return Value(-*integer);
        if (const auto* floating = std::get_if<double>(&value.data))
            return Value(-*floating);
        throw LanguageError("Unary '-' needs a number");
    }
    throw LanguageError("Unknown unary operator '" + expression.op + "'");
}

Value Runtime::evaluate_binary(const BinaryExpr& expression) {
    const Value left = evaluate(expression.left);
    if (expression.op == "??") return std::holds_alternative<std::monostate>(left.data)
                                         ? evaluate(expression.right) : left;
    if (expression.op == "&&") {
        return Value(require_boolean(left) && require_boolean(evaluate(expression.right)));
    }
    if (expression.op == "||") {
        return Value(require_boolean(left) || require_boolean(evaluate(expression.right)));
    }
    const Value right = evaluate(expression.right);
    if (expression.op == "+") {
        if (std::holds_alternative<std::string>(left.data) ||
            std::holds_alternative<std::string>(right.data))
            return Value(display_value(left) + display_value(right));
        return numeric_operation(left, right, expression.op);
    }
    if (expression.op == "-" || expression.op == "*" ||
        expression.op == "/" || expression.op == "%")
        return numeric_operation(left, right, expression.op);
    if (expression.op == "==" || expression.op == "!=") {
        const bool same = values_equal(left, right);
        return Value(expression.op == "==" ? same : !same);
    }
    if (expression.op == "<" || expression.op == "<=" ||
        expression.op == ">" || expression.op == ">=") {
        bool result;
        if (Runtime::is_number(left) && Runtime::is_number(right)) {
            const double a = std::holds_alternative<std::int64_t>(left.data)
                                 ? static_cast<double>(std::get<std::int64_t>(left.data))
                                 : std::get<double>(left.data);
            const double b = std::holds_alternative<std::int64_t>(right.data)
                                 ? static_cast<double>(std::get<std::int64_t>(right.data))
                                 : std::get<double>(right.data);
            if (expression.op == "<") result = a < b;
            else if (expression.op == "<=") result = a <= b;
            else if (expression.op == ">") result = a > b;
            else result = a >= b;
        } else if (const auto* a = std::get_if<std::string>(&left.data)) {
            const auto* b = std::get_if<std::string>(&right.data);
            if (!b) throw LanguageError("Cannot compare " + display_value(left) +
                                        " and " + display_value(right));
            if (expression.op == "<") result = *a < *b;
            else if (expression.op == "<=") result = *a <= *b;
            else if (expression.op == ">") result = *a > *b;
            else result = *a >= *b;
        } else {
            throw LanguageError("Cannot compare " + display_value(left) +
                                " and " + display_value(right));
        }
        return Value(result);
    }
    throw LanguageError("Unknown operator '" + expression.op + "'");
}

Value Runtime::evaluate_assignment(const AssignmentExpr& expression) {
    const auto& target = expression.target->node;
    if (const auto* variable = std::get_if<VariableExpr>(&target)) {
        auto& binding = environment->find(variable->name);
        if (binding.is_builtin)
            throw LanguageError("Cannot reassign built-in '" + variable->name + "'");
        Value current;
        if (expression.op != "=") current = binding.value;
        Value value = evaluate(expression.value);
        value = apply_compound(current, value, expression.op);
        check_type(value, binding.type, "variable '" + variable->name + "'");
        binding.value = value;
        return value;
    }
    if (const auto* property = std::get_if<PropertyExpr>(&target)) {
        const Value owner = evaluate(property->object);
        Value current;
        if (expression.op != "=") current = get_property(owner, property->name);
        Value value = apply_compound(current, evaluate(expression.value), expression.op);
        set_property(owner, property->name, value);
        return value;
    }
    if (const auto* index = std::get_if<IndexExpr>(&target)) {
        const Value owner = evaluate(index->object);
        const Value index_value = evaluate(index->index);
        auto* list = std::get_if<std::shared_ptr<ListValue>>(&owner.data);
        const auto* number = std::get_if<std::int64_t>(&index_value.data);
        if (!list || !number) throw LanguageError("List assignment needs an integer index");
        auto elements = (*list)->elements;
        std::int64_t i = *number;
        if (i < 0) i += static_cast<std::int64_t>(elements.size());
        if (i < 0 || static_cast<std::size_t>(i) >= elements.size())
            throw LanguageError("List index is out of range");
        Value current = expression.op == "=" ? Value{} : elements[static_cast<std::size_t>(i)];
        Value value = apply_compound(current, evaluate(expression.value), expression.op);
        (*list)->elements[static_cast<std::size_t>(i)] = value;
        return value;
    }
    throw LanguageError("Invalid assignment target");
}

Value Runtime::apply_compound(const Value& current, const Value& value,
                              const std::string& op) {
    if (op == "=") return value;
    const std::string binary_op = op.substr(0, 1);
    if (binary_op == "+" &&
        (std::holds_alternative<std::string>(current.data) ||
         std::holds_alternative<std::string>(value.data))) {
        return Value(display_value(current) + display_value(value));
    }
    return numeric_operation(current, value, binary_op);
}

Value Runtime::numeric_operation(const Value& left, const Value& right,
                                 const std::string& op) {
    if (!is_number(left) || !is_number(right))
        throw LanguageError("Operator '" + op + "' needs numeric values");
    const bool integers = std::holds_alternative<std::int64_t>(left.data) &&
                          std::holds_alternative<std::int64_t>(right.data);
    const double a = std::holds_alternative<std::int64_t>(left.data)
                         ? static_cast<double>(std::get<std::int64_t>(left.data))
                         : std::get<double>(left.data);
    const double b = std::holds_alternative<std::int64_t>(right.data)
                         ? static_cast<double>(std::get<std::int64_t>(right.data))
                         : std::get<double>(right.data);
    if (op == "/" || op == "%") {
        if (b == 0.0) throw LanguageError("Division by zero");
        if (op == "/") return Value(a / b);
        if (integers) {
            const auto x = std::get<std::int64_t>(left.data);
            const auto y = std::get<std::int64_t>(right.data);
            auto remainder = x % y;
            if (remainder != 0 && ((remainder < 0) != (y < 0))) remainder += y;
            return Value(remainder);
        }
        double remainder = std::fmod(a, b);
        if (remainder != 0.0 && ((remainder < 0.0) != (b < 0.0))) remainder += b;
        return Value(remainder);
    }
    if (op == "+" && integers) return Value(std::get<std::int64_t>(left.data) +
                                           std::get<std::int64_t>(right.data));
    if (op == "-" && integers) return Value(std::get<std::int64_t>(left.data) -
                                           std::get<std::int64_t>(right.data));
    if (op == "*" && integers) return Value(std::get<std::int64_t>(left.data) *
                                           std::get<std::int64_t>(right.data));
    if (op == "+") return Value(a + b);
    if (op == "-") return Value(a - b);
    if (op == "*") return Value(a * b);
    throw LanguageError("Unknown numeric operator '" + op + "'");
}

Value Runtime::evaluate_call(const CallExpr& expression) {
    const Value callee = evaluate(expression.callee);
    std::vector<Value> arguments;
    for (const auto& argument : expression.arguments) arguments.push_back(evaluate(argument));
    if (const auto* function =
            std::get_if<std::shared_ptr<FunctionValue>>(&callee.data))
        return (*function)->call(arguments);
    if (const auto* native =
            std::get_if<std::shared_ptr<NativeFunction>>(&callee.data))
        return (*native)->call(arguments);
    if (const auto* super =
            std::get_if<std::shared_ptr<SuperReference>>(&callee.data))
        return (*super)->call(arguments);
    if (const auto* class_value =
            std::get_if<std::shared_ptr<ClassValue>>(&callee.data))
        return Value((*class_value)->create(arguments));
    throw LanguageError("This value cannot be called");
}

Value Runtime::get_property(const Value& owner, const std::string& name) {
    if (const auto* instance =
            std::get_if<std::shared_ptr<ClassInstance>>(&owner.data))
        return (*instance)->get(name);
    if (const auto* structure =
            std::get_if<std::shared_ptr<StructValue>>(&owner.data)) {
        auto found = (*structure)->fields.find(name);
        if (found == (*structure)->fields.end())
            throw LanguageError("Struct '" + (*structure)->type_name +
                                "' has no field '" + name + "'");
        return found->second;
    }
    if (const auto* enumeration =
            std::get_if<std::shared_ptr<EnumTypeValue>>(&owner.data)) {
        if (!(*enumeration)->members.count(name))
            throw LanguageError("Enum '" + (*enumeration)->name +
                                "' has no member '" + name + "'");
        return Value(std::make_shared<EnumValue>(
            EnumValue{(*enumeration)->name, name}));
    }
    if (const auto* parent =
            std::get_if<std::shared_ptr<SuperReference>>(&owner.data))
        return (*parent)->get(name);
    if (const auto* list =
            std::get_if<std::shared_ptr<ListValue>>(&owner.data)) {
        if (name == "length")
            return Value(static_cast<std::int64_t>((*list)->elements.size()));
    }
    if (const auto* string = std::get_if<std::string>(&owner.data)) {
        if (name == "length") return Value(static_cast<std::int64_t>(string->size()));
    }
    throw LanguageError("Cannot read property '" + name + "' from this value");
}

void Runtime::set_property(const Value& owner, const std::string& name, Value value) {
    if (const auto* instance =
            std::get_if<std::shared_ptr<ClassInstance>>(&owner.data)) {
        (*instance)->set(name, std::move(value));
        return;
    }
    if (const auto* structure =
            std::get_if<std::shared_ptr<StructValue>>(&owner.data)) {
        auto type = struct_types.find((*structure)->type_name);
        if (type == struct_types.end())
            throw LanguageError("Only object fields can be assigned");
        auto field = std::find_if(type->second.fields.begin(), type->second.fields.end(),
                                  [&name](const VariableDecl& item) { return item.name == name; });
        if (field == type->second.fields.end())
            throw LanguageError("Struct '" + (*structure)->type_name +
                                "' has no field '" + name + "'");
        type->second.runtime->check_type(value, field->type,
                                         "field '" + name + "'");
        (*structure)->fields[name] = std::move(value);
        return;
    }
    throw LanguageError("Only object fields can be assigned");
}

Value Runtime::get_index(const IndexExpr& expression) {
    const Value owner = evaluate(expression.object);
    const Value index = evaluate(expression.index);
    const auto* integer = std::get_if<std::int64_t>(&index.data);
    if (!integer) throw LanguageError("Indexing needs a list or string and an integer");
    if (const auto* list = std::get_if<std::shared_ptr<ListValue>>(&owner.data)) {
        std::int64_t i = *integer;
        if (i < 0) i += static_cast<std::int64_t>((*list)->elements.size());
        if (i < 0 || static_cast<std::size_t>(i) >= (*list)->elements.size())
            throw LanguageError("Index is out of range");
        return (*list)->elements[static_cast<std::size_t>(i)];
    }
    if (const auto* string = std::get_if<std::string>(&owner.data)) {
        std::int64_t i = *integer;
        if (i < 0) i += static_cast<std::int64_t>(string->size());
        if (i < 0 || static_cast<std::size_t>(i) >= string->size())
            throw LanguageError("Index is out of range");
        return Value(std::string(1, (*string)[static_cast<std::size_t>(i)]));
    }
    throw LanguageError("Indexing needs a list or string and an integer");
}

Value Runtime::create_struct(const StructLiteralExpr& expression) {
    auto type = struct_types.find(expression.type_name);
    if (type == struct_types.end())
        throw LanguageError("Unknown struct '" + expression.type_name + "'");
    std::map<std::string, const VariableDecl*> declared;
    for (const auto& field : type->second.fields) declared[field.name] = &field;
    for (const auto& field : expression.fields) {
        if (!declared.count(field.first))
            throw LanguageError("Struct '" + expression.type_name +
                                "' has no field '" + field.first + "'");
    }
    for (const auto& field : declared) {
        if (!expression.fields.count(field.first))
            throw LanguageError("Struct literal is missing field '" + field.first + "'");
    }
    auto value = std::make_shared<StructValue>();
    value->type_name = expression.type_name;
    for (const auto& field : declared) {
        Value item = evaluate(expression.fields.at(field.first));
        type->second.runtime->check_type(item, field.second->type,
                                         "field '" + field.first + "'");
        value->fields[field.first] = std::move(item);
    }
    return Value(value);
}

// Standard libraries that must be imported before use. They are looked up by a
// reserved "std:" path prefix so they can never be confused with a .dk file.
void Runtime::import_stdlib(const ImportDecl& declaration) {
    const std::string library = declaration.path.substr(4);
    if (library != "http")
        throw LanguageError("Unknown standard library '" + declaration.path + "'");
    for (const auto& name : declaration.names) {
        if (name != "http")
            throw LanguageError("Module '" + declaration.path +
                                "' does not export '" + name + "'");
        if (globals->values.count(name) || type_exists(name))
            throw LanguageError("Imported name '" + name + "' is already defined");
        install_http(*this);
    }
}

void Runtime::import_module(const ImportDecl& declaration) {
    if (declaration.path.rfind("std:", 0) == 0) {
        import_stdlib(declaration);
        return;
    }
    std::filesystem::path module_path(declaration.path);
    if (module_path.extension() != ".dk") module_path += ".dk";
    if (module_path.is_relative()) {
        const auto base = source_path.empty() ? std::filesystem::current_path()
                                              : source_path.parent_path();
        module_path = base / module_path;
    }
    std::error_code error;
    module_path = std::filesystem::weakly_canonical(module_path, error);
    if (error) module_path = std::filesystem::absolute(module_path).lexically_normal();

    auto found = module_cache->find(module_path);
    std::shared_ptr<Runtime> module;
    if (found != module_cache->end()) {
        module = found->second;
    } else {
        if (shared_loading->count(module_path))
            throw LanguageError("Circular module import involving '" +
                                declaration.path + "'");
        std::ifstream file(module_path, std::ios::binary);
        if (!file) throw LanguageError("Cannot read module '" + module_path.string() + "'");
        const std::string source((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        shared_loading->insert(module_path);
        try {
            module = std::make_shared<Runtime>(output, input, *module_cache,
                                               *shared_loading);
            module->source_path = module_path;
            const auto statements = parse(source);
            for (const auto& statement : statements) {
                const auto* exported = std::get_if<ExportStmt>(&statement->node);
                if (!exported) continue;
                std::string name;
                const auto& node = exported->declaration->node;
                if (const auto* item = std::get_if<TypeAliasDecl>(&node)) name = item->name;
                else if (const auto* item = std::get_if<StructDecl>(&node)) name = item->name;
                else if (const auto* item = std::get_if<EnumDecl>(&node)) name = item->name;
                else if (const auto* item = std::get_if<ClassDecl>(&node)) name = item->name;
                else if (const auto* item = std::get_if<VariableDecl>(&node)) name = item->name;
                else if (const auto* item = std::get_if<std::shared_ptr<FunctionDecl>>(&node)) name = (*item)->name;
                if (name.empty()) throw LanguageError("Only named declarations can be exported");
                if (!module->module_exports.emplace(name, exported->declaration).second)
                    throw LanguageError("Module exports '" + name + "' more than once");
            }
            for (const auto& statement : statements) module->execute(statement);
            module_cache->emplace(module_path, module);
        } catch (...) {
            shared_loading->erase(module_path);
            throw;
        }
        shared_loading->erase(module_path);
    }

    for (const auto& name : declaration.names) {
        auto exported = module->module_exports.find(name);
        if (exported == module->module_exports.end())
            throw LanguageError("Module '" + declaration.path +
                                "' does not export '" + name + "'");
        if (globals->values.count(name) || type_exists(name))
            throw LanguageError("Imported name '" + name + "' is already defined");
        const auto& node = exported->second->node;
        if (const auto* item = std::get_if<TypeAliasDecl>(&node)) {
            type_aliases[name] = module->resolve_type(item->target);
        } else if (std::holds_alternative<StructDecl>(node)) {
            struct_types[name] = module->struct_types.at(name);
        } else if (std::holds_alternative<EnumDecl>(node)) {
            enum_types[name] = module->enum_types.at(name);
            globals->define(name, module->globals->find(name).value);
        } else if (std::holds_alternative<ClassDecl>(node)) {
            classes[name] = module->classes.at(name);
            globals->define(name, module->globals->find(name).value);
        } else {
            globals->define(name, module->globals->find(name).value,
                            module->globals->find(name).type);
        }
    }
}

void Runtime::initialize_fields(const std::shared_ptr<ClassInstance>& instance,
                                const std::shared_ptr<ClassValue>& class_value) {
    std::vector<std::shared_ptr<ClassValue>> hierarchy;
    for (auto current = class_value; current; current = current->parent)
        hierarchy.push_back(current);
    std::reverse(hierarchy.begin(), hierarchy.end());
    for (const auto& owner : hierarchy) {
        for (const auto& field : owner->declaration.fields) {
            instance->fields[field.name] =
                Binding{owner->runtime->default_value(field.type), field.type};
        }
    }
    for (const auto& owner : hierarchy) {
        for (const auto& field : owner->declaration.fields) {
            if (!field.initializer) continue;
            Value value = owner->runtime->evaluate_field_initializer(instance, owner, field);
            owner->runtime->check_type(value, field.type, "field '" + field.name + "'");
            instance->fields[field.name].value = std::move(value);
        }
    }
}

Value Runtime::evaluate_field_initializer(const std::shared_ptr<ClassInstance>& instance,
                                          const std::shared_ptr<ClassValue>& owner,
                                          const VariableDecl& field) {
    auto scope = std::make_shared<Environment>(owner->closure);
    scope->define("this", Value(instance));
    if (owner->parent)
        scope->define("super", Value(std::make_shared<SuperReference>(
                                   SuperReference{instance, owner->parent})));
    auto previous = environment;
    environment = std::move(scope);
    try {
        Value result = evaluate(field.initializer);
        environment = std::move(previous);
        return result;
    } catch (...) {
        environment = std::move(previous);
        throw;
    }
}

void Runtime::check_type(const Value& value, const std::optional<std::string>& type,
                         const std::string& description) {
    if (!type) return;
    const auto resolved = resolve_type(*type);
    if (matches_type(value, resolved)) return;
    throw LanguageError(description + " must be '" + *type + "', got '" +
                        value_type_name(value) + "'");
}

bool Runtime::matches_type(const Value& value, const std::string& type) {
    if (type.size() >= 2 && type.front() == '[' && type.back() == ']') {
        const auto* list = std::get_if<std::shared_ptr<ListValue>>(&value.data);
        if (!list) return false;
        const auto item_type = type.substr(1, type.size() - 2);
        return std::all_of((*list)->elements.begin(), (*list)->elements.end(),
                           [this, &item_type](const Value& item) {
                               return matches_type(item, item_type);
                           });
    }
    if (type == "int") return std::holds_alternative<std::int64_t>(value.data);
    if (type == "float") return is_number(value);
    if (type == "string") return std::holds_alternative<std::string>(value.data);
    if (type == "bool") return std::holds_alternative<bool>(value.data);
    if (type == "void") return std::holds_alternative<std::monostate>(value.data);
    if (const auto* instance = std::get_if<std::shared_ptr<ClassInstance>>(&value.data))
        return (*instance)->is_instance_of(type);
    if (const auto* structure = std::get_if<std::shared_ptr<StructValue>>(&value.data))
        return (*structure)->type_name == type;
    if (const auto* enumeration = std::get_if<std::shared_ptr<EnumValue>>(&value.data))
        return (*enumeration)->type_name == type;
    return false;
}

std::string Runtime::resolve_type(const std::string& type) {
    if (type.size() >= 2 && type.front() == '[' && type.back() == ']')
        return "[" + resolve_type(type.substr(1, type.size() - 2)) + "]";
    std::set<std::string> visited;
    std::string current = type;
    while (type_aliases.count(current)) {
        if (!visited.insert(current).second)
            throw LanguageError("Circular type alias involving '" + current + "'");
        current = type_aliases.at(current);
    }
    return current;
}

Value Runtime::default_value(const std::optional<std::string>& type) {
    if (!type) return Value{};
    const auto resolved = resolve_type(*type);
    if (resolved == "int") return Value(std::int64_t{0});
    if (resolved == "float") return Value(0.0);
    if (resolved == "string") return Value(std::string{});
    if (resolved == "bool") return Value(false);
    if (resolved.size() >= 2 && resolved.front() == '[' && resolved.back() == ']')
        return Value(std::make_shared<ListValue>());
    return Value{};
}

bool Runtime::type_exists(const std::string& name) const {
    return name == "int" || name == "float" || name == "string" ||
           name == "bool" || name == "void" || type_aliases.count(name) ||
           struct_types.count(name) || enum_types.count(name) || classes.count(name);
}

void Runtime::ensure_type_available(const std::string& name) const {
    if (type_exists(name)) throw LanguageError("Type '" + name + "' is already defined");
}

bool Runtime::require_boolean(const Value& value) {
    const auto* result = std::get_if<bool>(&value.data);
    if (!result) throw LanguageError("A condition must be a bool");
    return *result;
}

bool Runtime::is_number(const Value& value) {
    return std::holds_alternative<std::int64_t>(value.data) ||
           std::holds_alternative<double>(value.data);
}

Value FunctionValue::call(const std::vector<Value>& arguments) {
    const auto& parameters = declaration->parameters;
    if (arguments.size() != parameters.size())
        throw LanguageError("Function '" + declaration->name + "' expects " +
                            std::to_string(parameters.size()) + " argument(s), got " +
                            std::to_string(arguments.size()));
    auto local = std::make_shared<Environment>(closure);
    if (receiver) {
        local->define("this", Value(receiver));
        if (owner && owner->parent)
            local->define("super", Value(std::make_shared<SuperReference>(
                                      SuperReference{receiver, owner->parent})));
    }
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        runtime->check_type(arguments[i], parameters[i].type,
                            "parameter '" + parameters[i].name + "'");
        local->define(parameters[i].name, arguments[i], parameters[i].type);
    }
    try {
        runtime->execute_block(declaration->body, local);
    } catch (const ReturnSignal& result) {
        runtime->check_type(result.value, declaration->return_type,
                            "return value of '" + declaration->name + "'");
        return result.value;
    } catch (const BreakSignal&) {
        throw LanguageError("'break' cannot cross a function boundary");
    }
    runtime->check_type(Value{}, declaration->return_type,
                        "return value of '" + declaration->name + "'");
    return Value{};
}

std::shared_ptr<FunctionValue> FunctionValue::bind(
    const std::shared_ptr<ClassInstance>& instance) {
    return std::make_shared<FunctionValue>(
        FunctionValue{declaration, closure, runtime, owner, instance});
}

std::shared_ptr<FunctionValue> ClassValue::find_method(const std::string& method) const {
    auto found = methods.find(method);
    if (found != methods.end()) return found->second;
    return parent ? parent->find_method(method) : nullptr;
}

std::shared_ptr<ClassInstance> ClassValue::create(const std::vector<Value>& arguments) {
    auto instance = std::make_shared<ClassInstance>();
    instance->class_value = shared_from_this();
    runtime->initialize_fields(instance, instance->class_value);
    if (!constructor) {
        if (!arguments.empty())
            throw LanguageError("Class '" + name() +
                                "' has no constructor accepting arguments");
        if (parent && parent->constructor) parent->constructor->bind(instance)->call({});
    } else {
        constructor->bind(instance)->call(arguments);
    }
    return instance;
}

Value ClassInstance::get(const std::string& name) {
    auto field = fields.find(name);
    if (field != fields.end()) return field->second.value;
    auto method = class_value->find_method(name);
    if (method) return Value(method->bind(shared_from_this()));
    throw LanguageError("Class '" + class_value->name() +
                        "' has no field or function named '" + name + "'");
}

void ClassInstance::set(const std::string& name, Value value) {
    auto field = fields.find(name);
    if (field == fields.end())
        throw LanguageError("Class '" + class_value->name() +
                            "' has no field named '" + name + "'");
    class_value->runtime->check_type(value, field->second.type, "field '" + name + "'");
    field->second.value = std::move(value);
}

bool ClassInstance::is_instance_of(const std::string& name) const {
    for (auto current = class_value; current; current = current->parent)
        if (current->name() == name) return true;
    return false;
}

Value SuperReference::call(const std::vector<Value>& arguments) {
    if (!parent->constructor) {
        if (!arguments.empty())
            throw LanguageError("Parent class '" + parent->name() +
                                "' has no constructor");
        return Value{};
    }
    return parent->constructor->bind(instance)->call(arguments);
}

Value SuperReference::get(const std::string& name) {
    if (name == "constructor") {
        if (!parent->constructor)
            throw LanguageError("Parent class '" + parent->name() +
                                "' has no constructor");
        return Value(parent->constructor->bind(instance));
    }
    auto method = parent->find_method(name);
    if (!method)
        throw LanguageError("Parent class '" + parent->name() +
                            "' has no function named '" + name + "'");
    return Value(method->bind(instance));
}

std::string value_type_name(const Value& value) {
    if (std::holds_alternative<std::monostate>(value.data)) return "null";
    if (std::holds_alternative<bool>(value.data)) return "bool";
    if (std::holds_alternative<std::int64_t>(value.data)) return "int";
    if (std::holds_alternative<double>(value.data)) return "float";
    if (std::holds_alternative<std::string>(value.data)) return "string";
    if (std::holds_alternative<std::shared_ptr<ListValue>>(value.data)) return "list";
    if (const auto* item = std::get_if<std::shared_ptr<ClassInstance>>(&value.data))
        return (*item)->class_value->name();
    if (const auto* item = std::get_if<std::shared_ptr<StructValue>>(&value.data))
        return (*item)->type_name;
    if (const auto* item = std::get_if<std::shared_ptr<EnumValue>>(&value.data))
        return (*item)->type_name;
    return "value";
}

std::string display_value(const Value& value) {
    if (std::holds_alternative<std::monostate>(value.data)) return "null";
    if (const auto* item = std::get_if<bool>(&value.data)) return *item ? "true" : "false";
    if (const auto* item = std::get_if<std::int64_t>(&value.data)) return std::to_string(*item);
    if (const auto* item = std::get_if<double>(&value.data)) {
        std::ostringstream stream;
        stream << std::setprecision(15) << *item;
        auto result = stream.str();
        if (result.find('.') == std::string::npos && result.find('e') == std::string::npos &&
            result.find('E') == std::string::npos)
            result += ".0";
        return result;
    }
    if (const auto* item = std::get_if<std::string>(&value.data)) return *item;
    if (const auto* item = std::get_if<std::shared_ptr<EnumValue>>(&value.data))
        return (*item)->type_name + "." + (*item)->member;
    if (const auto* item = std::get_if<std::shared_ptr<ListValue>>(&value.data)) {
        std::string result = "[";
        for (std::size_t i = 0; i < (*item)->elements.size(); ++i) {
            if (i) result += ", ";
            result += display_value((*item)->elements[i]);
        }
        return result + "]";
    }
    if (const auto* item = std::get_if<std::shared_ptr<StructValue>>(&value.data)) {
        std::string result = (*item)->type_name + " { ";
        bool first = true;
        for (const auto& field : (*item)->fields) {
            if (!first) result += ", ";
            first = false;
            result += field.first + ": " + display_value(field.second);
        }
        return result + " }";
    }
    return "<value>";
}

bool values_equal(const Value& left, const Value& right) {
    if (Runtime::is_number(left) && Runtime::is_number(right)) {
        const double a = std::holds_alternative<std::int64_t>(left.data)
                             ? static_cast<double>(std::get<std::int64_t>(left.data))
                             : std::get<double>(left.data);
        const double b = std::holds_alternative<std::int64_t>(right.data)
                             ? static_cast<double>(std::get<std::int64_t>(right.data))
                             : std::get<double>(right.data);
        return a == b;
    }
    if (left.data.index() != right.data.index()) return false;
    if (std::holds_alternative<std::monostate>(left.data)) return true;
    if (const auto* a = std::get_if<bool>(&left.data)) return *a == std::get<bool>(right.data);
    if (const auto* a = std::get_if<std::string>(&left.data)) return *a == std::get<std::string>(right.data);
    if (const auto* a = std::get_if<std::shared_ptr<ListValue>>(&left.data)) {
        const auto& b = std::get<std::shared_ptr<ListValue>>(right.data)->elements;
        if ((*a)->elements.size() != b.size()) return false;
        for (std::size_t i = 0; i < b.size(); ++i)
            if (!values_equal((*a)->elements[i], b[i])) return false;
        return true;
    }
    if (const auto* a = std::get_if<std::shared_ptr<StructValue>>(&left.data)) {
        const auto& b = std::get<std::shared_ptr<StructValue>>(right.data);
        if ((*a)->type_name != b->type_name || (*a)->fields.size() != b->fields.size()) return false;
        for (const auto& field : (*a)->fields) {
            auto other = b->fields.find(field.first);
            if (other == b->fields.end() || !values_equal(field.second, other->second)) return false;
        }
        return true;
    }
    if (const auto* a = std::get_if<std::shared_ptr<EnumValue>>(&left.data)) {
        const auto& b = std::get<std::shared_ptr<EnumValue>>(right.data);
        return (*a)->type_name == b->type_name && (*a)->member == b->member;
    }
    if (const auto* a = std::get_if<std::shared_ptr<ClassInstance>>(&left.data))
        return *a == std::get<std::shared_ptr<ClassInstance>>(right.data);
    return false;
}

} // namespace dk

