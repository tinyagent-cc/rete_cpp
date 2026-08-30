#pragma once

#include "condition.hpp"
#include "types.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rete {

class ReteEngine;

using Bindings = std::unordered_map<std::string, Value>;
using Action   = std::function<void(ReteEngine& engine, const Bindings& bindings)>;

// ---------------------------------------------------------------------------
// Guards
//
// The alpha network discriminates on equality only, which is what makes it
// fast. Comparisons (`battery.level < 20`) cannot live there without turning
// the trie into an interval tree. So a comparison compiles into two pieces: a
// plain variable binding in the network, and a predicate checked once, on the
// bindings, at the moment the activation is about to fire.
//
// Cost is one comparison per activation of that rule, not per fact. Rules
// without guards pay nothing: the vector is empty and the loop is skipped.
// ---------------------------------------------------------------------------
enum class GuardOp { GreaterThan, GreaterOrEqual, LessThan, LessOrEqual, NotEqual };

struct Guard {
    std::string variable;   // the "?name" bound by the condition
    GuardOp     op = GuardOp::GreaterThan;
    Value       operand;
};

// Ordering across the numeric types; strings compare lexicographically; bools
// compare as 0/1. Anything else (a type mismatch, an unbound variable) yields
// no ordering, and the guard fails closed -- a rule never fires on a
// comparison the values could not actually answer.
inline bool guard_compare(const Value& lhs, const Value& rhs, GuardOp op, bool& comparable) {
    comparable = true;

    auto as_number = [](const Value& v, double& out) {
        if (auto* i = std::get_if<int64_t>(&v)) { out = static_cast<double>(*i); return true; }
        if (auto* d = std::get_if<double>(&v))  { out = *d;                      return true; }
        if (auto* b = std::get_if<bool>(&v))    { out = *b ? 1.0 : 0.0;          return true; }
        return false;
    };

    if (op == GuardOp::NotEqual) return lhs != rhs;

    double a = 0.0, b = 0.0;
    if (as_number(lhs, a) && as_number(rhs, b)) {
        switch (op) {
        case GuardOp::GreaterThan:    return a >  b;
        case GuardOp::GreaterOrEqual: return a >= b;
        case GuardOp::LessThan:       return a <  b;
        case GuardOp::LessOrEqual:    return a <= b;
        case GuardOp::NotEqual:       break;
        }
    }

    const auto* ls = std::get_if<std::string>(&lhs);
    const auto* rs = std::get_if<std::string>(&rhs);
    if (ls && rs) {
        switch (op) {
        case GuardOp::GreaterThan:    return *ls >  *rs;
        case GuardOp::GreaterOrEqual: return *ls >= *rs;
        case GuardOp::LessThan:       return *ls <  *rs;
        case GuardOp::LessOrEqual:    return *ls <= *rs;
        case GuardOp::NotEqual:       break;
        }
    }

    comparable = false;
    return false;
}

inline bool evaluate_guard(const Guard& g, const Bindings& bindings) {
    auto it = bindings.find(g.variable);
    if (it == bindings.end()) return false;
    bool comparable = false;
    const bool result = guard_compare(it->second, g.operand, g.op, comparable);
    return comparable && result;
}

inline bool evaluate_guards(const std::vector<Guard>& guards, const Bindings& bindings) {
    for (const auto& g : guards)
        if (!evaluate_guard(g, bindings)) return false;
    return true;
}

struct Production {
    std::string            name;
    int                    salience = 0;
    std::vector<Condition> conditions;
    Action                 action;
    std::vector<Guard>     guards;   // empty for every hand-written rule
};

} // namespace rete
