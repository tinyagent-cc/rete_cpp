#pragma once
// Structured, non-throwing errors. Embedded builds disable exceptions, so
// every failure path in this layer is a return value.

#include <cstddef>
#include <string>
#include <vector>

namespace rete::rules {

enum class ErrorCode {
    None = 0,
    ParseFailure,          // the document is not valid JSON/YAML
    NotAMap,               // a rule (or the root) has the wrong shape
    MissingField,
    EmptyRuleName,
    DuplicateRuleName,
    UnknownOperator,
    InvalidCondition,
    InvalidValueType,
    InvalidSalience,
    UnknownAction,
    EmptyConditions,
    UnsupportedFeature,    // valid schema, not available in this build
    FileNotFound,
};

const char* to_string(ErrorCode c);

struct RuleError {
    ErrorCode   code = ErrorCode::None;
    std::string rule;      // rule name when known, else empty
    std::string field;     // dotted path, e.g. "when[1].operator"
    std::string message;   // human sentence, already specific
    std::size_t line = 0;  // 0 when the format cannot report one

    std::string str() const {
        std::string s = to_string(code);
        if (!rule.empty())  { s += " in rule '" + rule + "'"; }
        if (!field.empty()) { s += " at " + field; }
        if (line)           { s += " (line " + std::to_string(line) + ")"; }
        if (!message.empty()) { s += ": " + message; }
        return s;
    }
};

inline const char* to_string(ErrorCode c) {
    switch (c) {
    case ErrorCode::None:               return "ok";
    case ErrorCode::ParseFailure:       return "parse error";
    case ErrorCode::NotAMap:            return "wrong node type";
    case ErrorCode::MissingField:       return "missing required field";
    case ErrorCode::EmptyRuleName:      return "empty rule name";
    case ErrorCode::DuplicateRuleName:  return "duplicate rule name";
    case ErrorCode::UnknownOperator:    return "unknown operator";
    case ErrorCode::InvalidCondition:   return "invalid condition";
    case ErrorCode::InvalidValueType:   return "invalid value type";
    case ErrorCode::InvalidSalience:    return "invalid salience";
    case ErrorCode::UnknownAction:      return "action not in registry";
    case ErrorCode::EmptyConditions:    return "rule has no conditions";
    case ErrorCode::UnsupportedFeature: return "unsupported in this build";
    case ErrorCode::FileNotFound:       return "file not found";
    }
    return "unknown error";
}

// Result of a load attempt. Loading is all-or-nothing per rule: a rule with
// any error is not registered, and the rules around it still are, so one bad
// rule in a robot's config does not silently disarm the whole behaviour set.
struct LoadResult {
    std::size_t            loaded = 0;
    std::vector<std::string> rule_names;
    std::vector<RuleError> errors;

    bool ok() const { return errors.empty(); }
    explicit operator bool() const { return ok(); }

    std::string summary() const {
        std::string s = "loaded " + std::to_string(loaded) + " rule(s)";
        if (!errors.empty()) {
            s += ", " + std::to_string(errors.size()) + " error(s):";
            for (const auto& e : errors) s += "\n  - " + e.str();
        }
        return s;
    }
};

} // namespace rete::rules
