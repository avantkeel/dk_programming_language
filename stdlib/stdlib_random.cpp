#include "dk/stdlib_random.hpp"

#include "dk/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>

namespace dk {
namespace {
std::mt19937_64& random_engine() {
    static std::mt19937_64 engine = [] {
        std::random_device device;
        const auto steady_ticks = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto high_resolution_ticks = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seed{
            static_cast<std::uint32_t>(device()),
            static_cast<std::uint32_t>(device()),
            static_cast<std::uint32_t>(steady_ticks),
            static_cast<std::uint32_t>(steady_ticks >> 32),
            static_cast<std::uint32_t>(high_resolution_ticks),
            static_cast<std::uint32_t>(high_resolution_ticks >> 32)};
        return std::mt19937_64(seed);
    }();
    return engine;
}
} // namespace

void install_random(Runtime& runtime) {
    auto random = std::make_shared<StructValue>();
    random->type_name = "random";
    random->fields["seed"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"random.seed", 1, 1, [](const std::vector<Value>& args) {
            const auto* seed = std::get_if<std::int64_t>(&args[0].data);
            if (!seed) throw LanguageError("random.seed expects an int argument");
            random_engine().seed(static_cast<std::mt19937_64::result_type>(*seed));
            return Value{};
        }}));
    random->fields["int"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"random.int", 2, 2, [](const std::vector<Value>& args) {
            const auto* minimum = std::get_if<std::int64_t>(&args[0].data);
            const auto* maximum = std::get_if<std::int64_t>(&args[1].data);
            if (!minimum || !maximum)
                throw LanguageError("random.int expects int arguments");
            if (*minimum > *maximum)
                throw LanguageError("random.int minimum must not exceed maximum");
            std::uniform_int_distribution<std::int64_t> distribution(*minimum, *maximum);
            return Value(distribution(random_engine()));
        }}));
    random->fields["float"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"random.float", 0, 0, [](const std::vector<Value>&) {
            std::uniform_real_distribution<double> distribution(0.0, 1.0);
            return Value(distribution(random_engine()));
        }}));
    random->fields["bool"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"random.bool", 0, 0, [](const std::vector<Value>&) {
            std::uniform_int_distribution<int> distribution(0, 1);
            return Value(distribution(random_engine()) == 1);
        }}));
    random->fields["choice"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"random.choice", 1, 1, [](const std::vector<Value>& args) {
            const auto* list =
                std::get_if<std::shared_ptr<ListValue>>(&args[0].data);
            if (!list) throw LanguageError("random.choice expects a list argument");
            if ((*list)->elements.empty())
                throw LanguageError("random.choice expects a non-empty list");
            std::uniform_int_distribution<std::size_t> distribution(
                0, (*list)->elements.size() - 1);
            return (*list)->elements[distribution(random_engine())];
        }}));
    random->fields["shuffle"] = Value(std::make_shared<NativeFunction>(
        NativeFunction{"random.shuffle", 1, 1, [](const std::vector<Value>& args) {
            const auto* list =
                std::get_if<std::shared_ptr<ListValue>>(&args[0].data);
            if (!list) throw LanguageError("random.shuffle expects a list argument");
            auto shuffled = std::make_shared<ListValue>();
            shuffled->elements = (*list)->elements;
            std::shuffle(shuffled->elements.begin(), shuffled->elements.end(),
                         random_engine());
            return Value(shuffled);
        }}));
    runtime.globals->define_builtin("random", Value(random));
}
} // namespace dk
