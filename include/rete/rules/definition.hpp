#pragma once
// ---------------------------------------------------------------------------
// Format-independent rule representation.
//
// Both parsers build these; the loader turns them into rete::Production. No
// part of this file knows what JSON or YAML is.
// ---------------------------------------------------------------------------

#include "../types.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rete::rules {

// Operators fall into two groups, and the difference is not cosmetic.
//
//   Network operators compile into the RETE discrimination network itself.
//   The engine's alpha trie tests equality, so these are what it executes
//   natively and what costs nothing at match time.
//
//   Guard operators cannot be expressed in that trie. `not_equals` is one of
//   them on purpose: as a negated condition it would also match when the fact
//   is absent entirely, so a robot with a dead sensor would fire rules meant
//   for a live one. As a guard it means what people read it to mean -- the
//   fact is there and its value differs. `not_exists` covers absence.
//
//   The rest cannot be expressed in the trie either. They compile to a
//   variable binding plus a predicate checked once when the activation is
//   about to fire. They are the honest way to write `battery.level < 20`
//   without rewriting the alpha network.
enum class Operator {
    Equals,          // network: constant test
    NotEquals,       // guard: fact exists and its value differs
    Exists,          // network: wildcard on value
    NotExists,       // network: negated wildcard
    Bind,            // network: variable binding
    GreaterThan,     // guard
    GreaterOrEqual,  // guard
    LessThan,        // guard
    LessOrEqual,     // guard
};

inline bool is_guard_operator(Operator op) {
    switch (op) {
    case Operator::NotEquals:
    case Operator::GreaterThan:
    case Operator::GreaterOrEqual:
    case Operator::LessThan:
    case Operator::LessOrEqual: return true;
    default: return false;
    }
}

inline const char* to_string(Operator op) {
    switch (op) {
    case Operator::Equals:         return "equals";
    case Operator::NotEquals:      return "not_equals";
    case Operator::Exists:         return "exists";
    case Operator::NotExists:      return "not_exists";
    case Operator::Bind:           return "bind";
    case Operator::GreaterThan:    return "greater_than";
    case Operator::GreaterOrEqual: return "greater_or_equal";
    case Operator::LessThan:       return "less_than";
    case Operator::LessOrEqual:    return "less_or_equal";
    }
    return "?";
}

// Accepts the canonical spelling plus the symbol most people reach for first.
inline bool parse_operator(const std::string& s, Operator& out) {
    struct Row { const char* name; Operator op; };
    static const Row table[] = {
        {"equals", Operator::Equals}, {"eq", Operator::Equals}, {"==", Operator::Equals},
        {"not_equals", Operator::NotEquals}, {"ne", Operator::NotEquals}, {"!=", Operator::NotEquals},
        {"exists", Operator::Exists}, {"not_exists", Operator::NotExists},
        {"bind", Operator::Bind},
        {"greater_than", Operator::GreaterThan}, {"gt", Operator::GreaterThan}, {">", Operator::GreaterThan},
        {"greater_or_equal", Operator::GreaterOrEqual}, {"gte", Operator::GreaterOrEqual}, {">=", Operator::GreaterOrEqual},
        {"less_than", Operator::LessThan}, {"lt", Operator::LessThan}, {"<", Operator::LessThan},
        {"less_or_equal", Operator::LessOrEqual}, {"lte", Operator::LessOrEqual}, {"<=", Operator::LessOrEqual},
    };
    for (const auto& r : table)
        if (s == r.name) { out = r.op; return true; }
    return false;
}

// One `when:` entry. `fact` is the WME identifier, `attribute` the attribute,
// and `value` the constant or bound variable, mirroring the engine's triple.
struct ConditionDefinition {
    std::string fact;
    std::string attribute;
    Operator    op = Operator::Equals;
    Value       value{};
    std::string bind;   // optional "?name" to bind the matched value to
};

using ParameterMap = std::vector<std::pair<std::string, Value>>;

struct ActionDefinition {
    std::string  id;         // must exist in the ActionRegistry
    ParameterMap parameters; // typed, reusing rete::Value
};

struct RuleDefinition {
    std::string                      name;
    int                              salience = 0;
    bool                             enabled  = true;
    std::vector<ConditionDefinition> conditions;
    std::vector<ActionDefinition>    actions;
    std::size_t                      line = 0;  // for error reporting
};

} // namespace rete::rules
