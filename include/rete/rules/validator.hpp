#pragma once
// ---------------------------------------------------------------------------
// Reading and checking rule documents.
//
// Two jobs live here, in the order they happen:
//
//   read_rule_document()  ConfigNode -> RuleDefinition, plus the errors for
//                         anything the definition cannot represent (a rule that
//                         is not a map, a `when` that is not a sequence, a
//                         salience that is not a number). Nothing is coerced:
//                         `salience: "high"` is an error, never a silent 0,
//                         because a robot whose priority quietly became 0 is
//                         worse off than one that refused to load the file.
//
//   RuleValidator         RuleDefinition -> is this rule loadable at all? Name,
//                         uniqueness, operator/value agreement, action ids.
//
// Both append `RuleError` values and neither throws; embedded builds have no
// exceptions, so every failure here is a return value.
//
// On line numbers: ConfigNode carries no source position, so an error reports
// `RuleDefinition::line`, which is 0 unless the caller filled it in. The number
// is propagated everywhere it is known rather than being dropped, so a parser
// that starts recording positions needs no change here.
// ---------------------------------------------------------------------------

#include "../config.hpp"
#include "../serialization/config_node.hpp"
#include "../types.hpp"
#include "action_registry.hpp"
#include "definition.hpp"
#include "error.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace rete::rules {

// ---------------------------------------------------------------------------
// Generated variables
//
// Wildcards and guards need variables the document did not write. Rather than
// hunting for a name that is provably unused, the loader reserves a prefix and
// the validator refuses any document variable that uses it. Collision then
// becomes impossible by construction instead of unlikely.
// ---------------------------------------------------------------------------
inline bool is_generated_variable(const std::string& s) {
    return s.size() >= 2 && s[0] == '?' && s[1] == '$';
}

inline std::string generated_variable(char kind, std::size_t index) {
    std::string s = "?$";
    s += kind;
    s += std::to_string(index);
    return s;
}

namespace detail {

inline void add_error(std::vector<RuleError>& errors, ErrorCode code,
                      std::string rule, std::string field, std::string message,
                      std::size_t line = 0) {
    RuleError e;
    e.code    = code;
    e.rule    = std::move(rule);
    e.field   = std::move(field);
    e.message = std::move(message);
    e.line    = line;
    errors.push_back(std::move(e));
}

inline std::string field_join(const std::string& base, const std::string& leaf) {
    if (base.empty()) return leaf;
    return base + "." + leaf;
}

inline std::string field_index(const std::string& base, const char* leaf, std::size_t i) {
    return field_join(base, std::string(leaf) + "[" + std::to_string(i) + "]");
}

inline const char* kind_name(config::ConfigNode::Kind k) {
    switch (k) {
    case config::ConfigNode::Kind::Null:     return "nothing";
    case config::ConfigNode::Kind::Scalar:   return "a single value";
    case config::ConfigNode::Kind::Sequence: return "a list";
    case config::ConfigNode::Kind::Map:      return "a map";
    }
    return "something else";
}

// Locale-free, no <regex>: a hand scan is a dozen lines and behaves the same on
// every target.
inline bool looks_numeric(const std::string& s) {
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    bool digits = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; digits = true; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; digits = true; }
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        bool exp_digits = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; exp_digits = true; }
        if (!exp_digits) return false;
    }
    return i == s.size();
}

inline bool has_value(const Value& v) {
    return !std::holds_alternative<std::monostate>(v);
}

// An ordering guard needs an operand it can actually be ordered against.
// A bool has no ordering the author can have meant, and a non-numeric string
// compared with a numeric fact makes guard_compare fail closed forever: the
// rule would never fire and nothing would say why. A numeric-looking string is
// let through because it is the number the author meant, and two string values
// still compare lexicographically.
inline bool is_orderable_operand(const Value& v) {
    if (std::holds_alternative<int64_t>(v)) return true;
    if (std::holds_alternative<double>(v))  return true;
    if (auto* s = std::get_if<std::string>(&v)) return looks_numeric(*s);
    return false;
}

inline bool is_ordering_operator(Operator op) {
    switch (op) {
    case Operator::GreaterThan:
    case Operator::GreaterOrEqual:
    case Operator::LessThan:
    case Operator::LessOrEqual: return true;
    default: return false;
    }
}

// ---------------------------------------------------------------------------
// Document reading
// ---------------------------------------------------------------------------

inline bool read_condition(const config::ConfigNode& node, const std::string& path,
                           const std::string& rule, ConditionDefinition& out,
                           std::vector<RuleError>& errors) {
    if (!node.is_map()) {
        add_error(errors, ErrorCode::NotAMap, rule, path,
                  std::string("a when entry must be a map of fact/attribute/op, found ") +
                      kind_name(node.kind()));
        return false;
    }

    bool ok_condition = true;

    if (const auto* n = node.find("fact")) {
        bool ok = false;
        std::string s = n->as_string(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidValueType, rule, field_join(path, "fact"),
                      "fact must be a string naming the fact identifier");
            ok_condition = false;
        } else {
            out.fact = std::move(s);
        }
    }

    if (const auto* n = node.find("attribute")) {
        bool ok = false;
        std::string s = n->as_string(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidValueType, rule, field_join(path, "attribute"),
                      "attribute must be a string");
            ok_condition = false;
        } else {
            out.attribute = std::move(s);
        }
    }

    // `operator` is accepted as well as `op`: silently defaulting a misspelled
    // key to equals would turn a comparison into an equality test.
    const char* op_key = "op";
    const config::ConfigNode* op_node = node.find("op");
    if (!op_node) {
        op_node = node.find("operator");
        if (op_node) op_key = "operator";
    }
    if (op_node) {
        bool ok = false;
        std::string s = op_node->as_string(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::UnknownOperator, rule, field_join(path, op_key),
                      "op must be a string naming an operator");
            ok_condition = false;
        } else if (!parse_operator(s, out.op)) {
            add_error(errors, ErrorCode::UnknownOperator, rule, field_join(path, op_key),
                      "'" + s + "' is not an operator; use equals, not_equals, exists, "
                      "not_exists, bind, greater_than, greater_or_equal, less_than or less_or_equal");
            ok_condition = false;
        }
    }

    if (const auto* n = node.find("value")) {
        if (n->is_scalar()) {
            out.value = n->scalar();
        } else if (!n->is_null()) {
            // A null value means the same as no value at all; the validator
            // decides whether this operator needed one.
            add_error(errors, ErrorCode::InvalidValueType, rule, field_join(path, "value"),
                      std::string("value must be a single value, found ") + kind_name(n->kind()));
            ok_condition = false;
        }
    }

    if (const auto* n = node.find("bind")) {
        bool ok = false;
        std::string s = n->as_string(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidValueType, rule, field_join(path, "bind"),
                      "bind must be a string like \"?level\"");
            ok_condition = false;
        } else {
            out.bind = std::move(s);
        }
    }

    return ok_condition;
}

inline bool read_action(const config::ConfigNode& node, const std::string& path,
                        const std::string& rule, ActionDefinition& out,
                        std::vector<RuleError>& errors) {
    if (!node.is_map()) {
        add_error(errors, ErrorCode::NotAMap, rule, path,
                  std::string("a then entry must be a map with an action id, found ") +
                      kind_name(node.kind()));
        return false;
    }

    bool ok_action = true;

    if (const auto* n = node.find("action")) {
        bool ok = false;
        std::string s = n->as_string(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidValueType, rule, field_join(path, "action"),
                      "action must be a string naming a registered action");
            ok_action = false;
        } else {
            out.id = std::move(s);
        }
    }

    if (const auto* n = node.find("params")) {
        if (!n->is_map()) {
            add_error(errors, ErrorCode::NotAMap, rule, field_join(path, "params"),
                      std::string("params must be a map of names to single values, found ") +
                          kind_name(n->kind()));
            ok_action = false;
        } else {
            for (const auto& kv : n->map()) {
                if (kv.second.is_scalar()) {
                    out.parameters.emplace_back(kv.first, kv.second.scalar());
                } else {
                    add_error(errors, ErrorCode::InvalidValueType, rule,
                              field_join(field_join(path, "params"), kv.first),
                              "a parameter must be a string, number or boolean");
                    ok_action = false;
                }
            }
        }
    }

    return ok_action;
}

inline bool read_rule(const config::ConfigNode& node, const std::string& path,
                      RuleDefinition& out, std::vector<RuleError>& errors) {
    if (!node.is_map()) {
        add_error(errors, ErrorCode::NotAMap, "", path,
                  std::string("a rule must be a map, found ") + kind_name(node.kind()));
        return false;
    }

    bool ok_rule = true;

    // Name first: every error after this one wants it for context.
    if (const auto* n = node.find("name")) {
        bool ok = false;
        std::string s = n->as_string(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidValueType, "", field_join(path, "name"),
                      "name must be a string");
            ok_rule = false;
        } else {
            out.name = std::move(s);
        }
    }
    const std::string& rule = out.name;

    if (const auto* n = node.find("salience")) {
        bool ok = false;
        const int64_t v = n->as_int(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidSalience, rule, field_join(path, "salience"),
                      "salience must be a whole number");
            ok_rule = false;
        } else if (v < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
                   v > static_cast<int64_t>(std::numeric_limits<int>::max())) {
            add_error(errors, ErrorCode::InvalidSalience, rule, field_join(path, "salience"),
                      "salience " + std::to_string(v) + " does not fit in an int");
            ok_rule = false;
        } else {
            out.salience = static_cast<int>(v);
        }
    }

    if (const auto* n = node.find("enabled")) {
        bool ok = false;
        const bool v = n->as_bool(&ok);
        if (!ok) {
            add_error(errors, ErrorCode::InvalidValueType, rule, field_join(path, "enabled"),
                      "enabled must be true or false");
            ok_rule = false;
        } else {
            out.enabled = v;
        }
    }

    // A missing `when` is left to the validator, so `when: []` and no `when` at
    // all report the same thing.
    if (const auto* n = node.find("when")) {
        if (!n->is_seq()) {
            add_error(errors, ErrorCode::NotAMap, rule, field_join(path, "when"),
                      std::string("when must be a list of conditions, found ") + kind_name(n->kind()));
            ok_rule = false;
        } else {
            const auto& seq = n->seq();
            for (std::size_t i = 0; i < seq.size(); ++i) {
                ConditionDefinition c;
                if (read_condition(seq[i], field_index(path, "when", i), rule, c, errors))
                    out.conditions.push_back(std::move(c));
                else
                    ok_rule = false;
            }
        }
    }

    if (const auto* n = node.find("then")) {
        if (!n->is_seq()) {
            add_error(errors, ErrorCode::NotAMap, rule, field_join(path, "then"),
                      std::string("then must be a list of actions, found ") + kind_name(n->kind()));
            ok_rule = false;
        } else {
            const auto& seq = n->seq();
            for (std::size_t i = 0; i < seq.size(); ++i) {
                ActionDefinition a;
                if (read_action(seq[i], field_index(path, "then", i), rule, a, errors))
                    out.actions.push_back(std::move(a));
                else
                    ok_rule = false;
            }
        }
    }

    return ok_rule;
}

inline void read_rule_sequence(const config::ConfigSeq& seq, const char* base,
                               std::vector<RuleDefinition>& out,
                               std::vector<RuleError>& errors) {
    for (std::size_t i = 0; i < seq.size(); ++i) {
        RuleDefinition def;
        if (read_rule(seq[i], field_index(std::string(), base, i), def, errors))
            out.push_back(std::move(def));
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// Document reader
//
// Accepts the three shapes a rule file is written in: a map with `rules:`, a
// bare list of rules, and a single bare rule map. A rule that could not be read
// contributes an error and no definition, so one malformed entry never costs
// the rules around it.
// ---------------------------------------------------------------------------
inline bool read_rule_document(const config::ConfigNode& root,
                               std::vector<RuleDefinition>& out,
                               std::vector<RuleError>& errors) {
    const std::size_t before = errors.size();

    if (root.is_seq()) {
        detail::read_rule_sequence(root.seq(), "rules", out, errors);
    } else if (root.is_map()) {
        if (const auto* rules = root.find("rules")) {
            if (rules->is_seq()) {
                detail::read_rule_sequence(rules->seq(), "rules", out, errors);
            } else {
                detail::add_error(errors, ErrorCode::NotAMap, "", "rules",
                                  std::string("rules must be a list of rule maps, found ") +
                                      detail::kind_name(rules->kind()));
            }
        } else if (root.has("name") || root.has("when") || root.has("then")) {
            RuleDefinition def;
            if (detail::read_rule(root, std::string(), def, errors))
                out.push_back(std::move(def));
        } else {
            detail::add_error(errors, ErrorCode::MissingField, "", "rules",
                              "the document has no 'rules' key and does not look like a single rule");
        }
    } else {
        detail::add_error(errors, ErrorCode::NotAMap, "", "",
                          std::string("a rule document must be a map with 'rules', a list of "
                                      "rules, or a single rule map, found ") +
                              detail::kind_name(root.kind()));
    }

    return errors.size() == before;
}

// ---------------------------------------------------------------------------
// RuleValidator
//
// Everything the compiler would otherwise have to guess about. A definition
// that clears this can be turned into a Production with no failure path, which
// is why the compile step in loader.hpp has none.
//
// Duplicate names are checked twice over: against the rules seen so far in this
// document, and against the rules the engine is already running. Both matter.
// The first would give a file two rules with one name; the second would leave
// an operator convinced they replaced a behaviour when they added a second one.
// ---------------------------------------------------------------------------
class RuleValidator {
public:
    explicit RuleValidator(const ActionRegistry& registry) : registry_(registry) {}

    const ActionRegistry& registry() const { return registry_; }

    // Names the engine already holds. The loader fills this from
    // ReteEngine::rule_names() before validating a batch.
    void set_existing_names(const std::vector<std::string>& names) {
        existing_.clear();
        existing_.insert(names.begin(), names.end());
    }
    void add_existing_name(const std::string& name) { existing_.insert(name); }
    void clear_existing_names() { existing_.clear(); }

    // With replace_existing, a name the engine already has is a replacement
    // rather than a clash.
    void set_allow_replace(bool allow) { allow_replace_ = allow; }
    bool allow_replace() const { return allow_replace_; }

    // Forget the names seen so far. Call between documents that are allowed to
    // redefine each other.
    void reset_seen() { seen_.clear(); }

    bool validate(const RuleDefinition& def, std::vector<RuleError>& errors) {
        const std::size_t before = errors.size();
        const std::size_t line   = def.line;

        if (def.name.empty()) {
            detail::add_error(errors, ErrorCode::EmptyRuleName, "", "name",
                              "a rule needs a non-empty name; it is how the engine, the agenda "
                              "and remove_rule() refer to it", line);
        } else if (!seen_.insert(def.name).second) {
            detail::add_error(errors, ErrorCode::DuplicateRuleName, def.name, "name",
                              "this document already defines a rule called '" + def.name + "'", line);
        } else if (!allow_replace_ && existing_.count(def.name) > 0) {
            detail::add_error(errors, ErrorCode::DuplicateRuleName, def.name, "name",
                              "the engine already has a rule called '" + def.name +
                                  "'; load with replace_existing to overwrite it", line);
        }

        if (def.conditions.empty()) {
            detail::add_error(errors, ErrorCode::EmptyConditions, def.name, "when",
                              "a rule needs at least one when clause; a rule that matches "
                              "nothing would fire on every cycle", line);
        }
        if (def.actions.empty()) {
            detail::add_error(errors, ErrorCode::MissingField, def.name, "then",
                              "a rule needs at least one then action", line);
        }

        for (std::size_t i = 0; i < def.conditions.size(); ++i)
            validate_condition(def, def.conditions[i], i, errors);
        for (std::size_t i = 0; i < def.actions.size(); ++i)
            validate_action(def, def.actions[i], i, errors);

        return errors.size() == before;
    }

    // Validates the whole batch. Every rule is checked even when an earlier one
    // failed, so one run of the loader reports everything wrong with a file.
    bool validate_all(const std::vector<RuleDefinition>& defs, std::vector<RuleError>& errors) {
        bool all_ok = true;
        for (const auto& d : defs)
            if (!validate(d, errors)) all_ok = false;
        return all_ok;
    }

private:
    void validate_condition(const RuleDefinition& def, const ConditionDefinition& c,
                            std::size_t index, std::vector<RuleError>& errors) const {
        const std::string base = detail::field_index(std::string(), "when", index);
        const std::size_t line = def.line;
        const std::string& rule = def.name;

        if (c.fact.empty()) {
            detail::add_error(errors, ErrorCode::MissingField, rule, detail::field_join(base, "fact"),
                              "every condition needs a fact identifier", line);
        }
        if (c.attribute.empty()) {
            detail::add_error(errors, ErrorCode::MissingField, rule,
                              detail::field_join(base, "attribute"),
                              "every condition needs an attribute", line);
        }

        if (!c.bind.empty()) {
            if (!is_variable(c.bind)) {
                detail::add_error(errors, ErrorCode::InvalidCondition, rule,
                                  detail::field_join(base, "bind"),
                                  "bind must name a variable, like \"?level\"; without the '?' the "
                                  "engine would read it as a constant", line);
            } else if (is_generated_variable(c.bind)) {
                detail::add_error(errors, ErrorCode::InvalidCondition, rule,
                                  detail::field_join(base, "bind"),
                                  "the '?$' prefix is reserved for variables the loader generates", line);
            }
        }
        const std::string* value_text = std::get_if<std::string>(&c.value);
        if (value_text && is_variable(*value_text) && is_generated_variable(*value_text)) {
            detail::add_error(errors, ErrorCode::InvalidCondition, rule,
                              detail::field_join(base, "value"),
                              "the '?$' prefix is reserved for variables the loader generates", line);
        }

        const bool have_value = detail::has_value(c.value);
        const bool value_var  = value_is_variable(c.value);

        switch (c.op) {
        case Operator::Equals:
            if (!have_value) {
                detail::add_error(errors, ErrorCode::MissingField, rule,
                                  detail::field_join(base, "value"),
                                  "equals needs a value to test against; use exists to match any value",
                                  line);
            } else {
                check_bind_against_value(def, c, base, errors);
            }
            break;

        case Operator::Bind:
            if (c.bind.empty() && !value_var) {
                detail::add_error(errors, ErrorCode::MissingField, rule,
                                  detail::field_join(base, "bind"),
                                  "bind needs either bind: \"?name\" or value: \"?name\"", line);
            } else {
                check_bind_against_value(def, c, base, errors);
            }
            break;

        case Operator::Exists:
            // A value is allowed and means "some fact with exactly this value";
            // without one the loader binds a generated variable, which is this
            // engine's wildcard.
            check_bind_against_value(def, c, base, errors);
            break;

        case Operator::NotExists:
            if (!c.bind.empty()) {
                detail::add_error(errors, ErrorCode::UnsupportedFeature, rule,
                                  detail::field_join(base, "bind"),
                                  "not_exists matches the absence of a fact, so there is no value to "
                                  "bind; this build cannot honour bind on a negated condition", line);
            }
            break;

        default:  // the guard operators
            if (!have_value) {
                detail::add_error(errors, ErrorCode::MissingField, rule,
                                  detail::field_join(base, "value"),
                                  std::string(to_string(c.op)) + " needs a value to compare against",
                                  line);
            } else if (value_var) {
                detail::add_error(errors, ErrorCode::InvalidCondition, rule,
                                  detail::field_join(base, "value"),
                                  std::string(to_string(c.op)) +
                                      " compares the matched value against a literal, not against "
                                      "another variable", line);
            } else if (detail::is_ordering_operator(c.op) && !detail::is_orderable_operand(c.value)) {
                detail::add_error(errors, ErrorCode::InvalidValueType, rule,
                                  detail::field_join(base, "value"),
                                  "'" + value_to_string(c.value) + "' cannot be ordered by " +
                                      to_string(c.op) + "; use a number, or equals and not_equals "
                                      "for booleans and free text", line);
            }
            break;
        }
    }

    // One field holds one test. A condition that pins the value to a constant
    // and also asks to bind it is asking for two, and the engine can only do
    // the first, so it would silently drop the binding.
    void check_bind_against_value(const RuleDefinition& def, const ConditionDefinition& c,
                                  const std::string& base, std::vector<RuleError>& errors) const {
        if (c.bind.empty()) return;
        if (!detail::has_value(c.value)) return;

        const std::string* text = std::get_if<std::string>(&c.value);
        if (!text || !is_variable(*text)) {
            detail::add_error(errors, ErrorCode::InvalidCondition, def.name,
                              detail::field_join(base, "bind"),
                              "a condition can test a constant value or bind a variable, not both; "
                              "drop bind, or drop value and use op: bind", def.line);
        } else if (*text != c.bind) {
            detail::add_error(errors, ErrorCode::InvalidCondition, def.name,
                              detail::field_join(base, "bind"),
                              "value binds " + *text + " while bind says " + c.bind +
                                  "; name the variable once", def.line);
        }
    }

    void validate_action(const RuleDefinition& def, const ActionDefinition& a,
                         std::size_t index, std::vector<RuleError>& errors) const {
        const std::string base = detail::field_index(std::string(), "then", index);

        if (a.id.empty()) {
            detail::add_error(errors, ErrorCode::MissingField, def.name,
                              detail::field_join(base, "action"),
                              "every then entry needs an action id", def.line);
            return;
        }
        if (!registry_.contains(a.id)) {
            detail::add_error(errors, ErrorCode::UnknownAction, def.name,
                              detail::field_join(base, "action"),
                              "no action called '" + a.id + "' is registered; a rule file names "
                              "actions, it never carries them, so register it before loading",
                              def.line);
        }
    }

    const ActionRegistry&           registry_;
    std::unordered_set<std::string> existing_;
    std::unordered_set<std::string> seen_;
    bool                            allow_replace_ = false;
};

} // namespace rete::rules
