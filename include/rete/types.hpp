#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace rete {

using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

enum class Field : uint8_t {
    Identifier = 0,
    Attribute  = 1,
    Value      = 2
};

inline bool is_variable(const std::string& s) {
    return s.size() >= 2 && s[0] == '?';
}

inline bool value_is_variable(const Value& v) {
    if (auto* s = std::get_if<std::string>(&v))
        return is_variable(*s);
    return false;
}

inline std::string value_to_string(const Value& v) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>)
            return "<nil>";
        else if constexpr (std::is_same_v<T, bool>)
            return arg ? "true" : "false";
        else if constexpr (std::is_same_v<T, int64_t>)
            return std::to_string(arg);
        else if constexpr (std::is_same_v<T, double>)
            return std::to_string(arg);
        else if constexpr (std::is_same_v<T, std::string>)
            return arg;
        else
            return "<?>";
    }, v);
}

struct ValueHash {
    std::size_t operator()(const Value& v) const {
        return std::visit([](auto&& arg) -> std::size_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return 0;
            else
                return std::hash<T>{}(arg);
        }, v);
    }
};

} // namespace rete
