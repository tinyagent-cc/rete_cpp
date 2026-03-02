#pragma once

#include "types.hpp"
#include <string>
#include <variant>
#include <vector>

namespace rete {

struct ConstantTest {
    Value value;
};

struct VariableBinding {
    std::string name;
};

using FieldTest = std::variant<std::monostate, ConstantTest, VariableBinding>;

struct Condition {
    FieldTest id_test;
    FieldTest attr_test;
    FieldTest value_test;
    bool      negated = false;

    FieldTest test_for(Field f) const {
        switch (f) {
        case Field::Identifier: return id_test;
        case Field::Attribute:  return attr_test;
        case Field::Value:      return value_test;
        }
        return {};
    }
};

struct JoinTest {
    Field alpha_field;
    int   condition_index;
    Field condition_field;
};

inline Condition make_condition(const Value& id, const Value& attr, const Value& val,
                                bool negated = false) {
    Condition c;
    c.negated = negated;

    auto to_field_test = [](const Value& v) -> FieldTest {
        if (auto* s = std::get_if<std::string>(&v)) {
            if (is_variable(*s))
                return VariableBinding{*s};
            return ConstantTest{v};
        }
        if (std::holds_alternative<std::monostate>(v))
            return std::monostate{};
        return ConstantTest{v};
    };

    c.id_test    = to_field_test(id);
    c.attr_test  = to_field_test(attr);
    c.value_test = to_field_test(val);
    return c;
}

} // namespace rete
