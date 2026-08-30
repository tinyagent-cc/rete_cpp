#pragma once
// ---------------------------------------------------------------------------
// Turning a rule document into live productions.
//
// The order is fixed and it matters: read the whole document, validate every
// rule in it, then add the ones that passed. Validating first is what lets a
// duplicate name be caught against the entire file rather than only against the
// rules ahead of it, and it keeps the engine from being half-loaded when the
// last rule turns out to be broken.
//
// Loading is all-or-nothing per rule. A rule with any error is not added and
// its healthy neighbours still are, because a robot that drops one behaviour is
// in better shape than one that boots with none. LoadResult carries both counts
// so the caller can tell which happened.
//
// A document never carries code. Actions are looked up in the ActionRegistry
// once, here, and the handler and its parameters are copied into the Production
// by value; nothing in the engine points back at the ConfigNode, which is free
// to be destroyed the moment loading returns.
// ---------------------------------------------------------------------------

#include "../config.hpp"
#include "../condition.hpp"
#include "../production.hpp"
#include "../rete.hpp"
#include "../serialization/config_node.hpp"
#include "../serialization/parse_error.hpp"
#include "../types.hpp"
#include "action_registry.hpp"
#include "definition.hpp"
#include "error.hpp"
#include "validator.hpp"

#if RETE_ENABLE_JSON
#include "../serialization/json_parser.hpp"
#endif
#if RETE_ENABLE_YAML
#include "../serialization/yaml_parser.hpp"
#endif

#if !RETE_NO_IOSTREAM
#include <fstream>
#include <iterator>
#endif

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rete::rules {

enum class Format { Auto, Json, Yaml };

struct LoadOptions {
    // Replace a rule the engine already runs instead of refusing the name.
    bool replace_existing = false;
    // Give up at the first error rather than reporting everything wrong with
    // the document. Nothing is added when this stops a load.
    bool stop_on_first_error = false;
};

// ---------------------------------------------------------------------------
// Compilation
// ---------------------------------------------------------------------------

inline bool to_guard_op(Operator op, GuardOp& out) {
    switch (op) {
    case Operator::NotEquals:      out = GuardOp::NotEqual;       return true;
    case Operator::GreaterThan:    out = GuardOp::GreaterThan;    return true;
    case Operator::GreaterOrEqual: out = GuardOp::GreaterOrEqual; return true;
    case Operator::LessThan:       out = GuardOp::LessThan;       return true;
    case Operator::LessOrEqual:    out = GuardOp::LessOrEqual;    return true;
    default: return false;
    }
}

// Builds the Production for a definition the validator has already accepted.
// There is no failure path on purpose: every case that could not be expressed
// was rejected earlier, where the error could name the field it came from.
inline Production compile_rule(const RuleDefinition& def, const ActionRegistry& registry) {
    Production prod;
    prod.name     = def.name;
    prod.salience = def.salience;
    prod.conditions.reserve(def.conditions.size());

    for (std::size_t i = 0; i < def.conditions.size(); ++i) {
        const ConditionDefinition& c = def.conditions[i];
        const Value id{c.fact};
        const Value attr{c.attribute};

        switch (c.op) {
        case Operator::Equals:
            // make_condition already turns a "?"-prefixed value into a binding,
            // so `value: "?x"` and `op: bind` land on the same node.
            prod.conditions.push_back(make_condition(id, attr, c.value, false));
            break;

        case Operator::Bind: {
            std::string var = c.bind;
            if (var.empty()) {
                if (const auto* s = std::get_if<std::string>(&c.value)) var = *s;
                else var = generated_variable('b', i);
            }
            prod.conditions.push_back(make_condition(id, attr, Value{var}, false));
            break;
        }

        case Operator::Exists: {
            // The engine has no wildcard field test, so a binding is the
            // wildcard: it matches any value and costs one entry in the
            // bindings the action receives.
            Value slot = c.value;
            if (!detail::has_value(slot))
                slot = Value{c.bind.empty() ? generated_variable('x', i) : c.bind};
            prod.conditions.push_back(make_condition(id, attr, slot, false));
            break;
        }

        case Operator::NotExists:
            // A negated match contributes an empty slot to the token, so there
            // is nothing to bind here and the value field stays a plain
            // wildcard unless the document named a value to be absent.
            prod.conditions.push_back(make_condition(id, attr, c.value, true));
            break;

        default: {
            // A comparison is two things: a binding the network can do, and a
            // predicate checked once when the activation is about to fire.
            const std::string var = c.bind.empty() ? generated_variable('g', i) : c.bind;
            prod.conditions.push_back(make_condition(id, attr, Value{var}, false));

            Guard g;
            g.variable = var;
            g.operand  = c.value;
            if (to_guard_op(c.op, g.op))
                prod.guards.push_back(std::move(g));
            break;
        }
        }
    }

    // The handler is resolved once, at load time, and copied in with its
    // parameters. Looking it up per firing would cost a hash lookup on every
    // activation and would let a later unregister_action() empty the rule out
    // from under the engine.
    struct BoundAction {
        ActionHandler handler;
        ParameterMap  params;
    };
    std::vector<BoundAction> bound;
    bound.reserve(def.actions.size());
    for (const auto& a : def.actions) {
        if (const ActionHandler* h = registry.find(a.id))
            bound.push_back(BoundAction{*h, a.parameters});
    }

    if (!bound.empty()) {
        prod.action = [actions = std::move(bound)](ReteEngine& engine, const Bindings& bindings) {
            for (const auto& a : actions) {
                if (a.handler)
                    a.handler(engine, bindings, ActionParams(a.params));
            }
        };
    }

    return prod;
}

// ---------------------------------------------------------------------------
// Format resolution
// ---------------------------------------------------------------------------

namespace detail {

inline bool ends_with_ci(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    if (s.size() < suf.size()) return false;
    const std::size_t off = s.size() - suf.size();
    for (std::size_t i = 0; i < suf.size(); ++i) {
        char c = s[off + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != suf[i]) return false;
    }
    return true;
}

} // namespace detail

// Extension only. Auto when the name says nothing, so the caller can fall back
// to looking at the content.
inline Format format_from_path(const std::string& path) {
    if (detail::ends_with_ci(path, ".json")) return Format::Json;
    if (detail::ends_with_ci(path, ".yaml")) return Format::Yaml;
    if (detail::ends_with_ci(path, ".yml"))  return Format::Yaml;
    return Format::Auto;
}

// A JSON document starts with '{' or '[' after whitespace. Anything else that
// is meant as configuration is YAML, and saying so is better than handing a
// YAML file to the JSON reader and reporting a syntax error at line 1.
inline Format format_from_text(const std::string& text) {
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return (c == '{' || c == '[') ? Format::Json : Format::Yaml;
    }
    return Format::Json;
}

// ---------------------------------------------------------------------------
// RuleLoader
// ---------------------------------------------------------------------------
class RuleLoader {
public:
    RuleLoader(ReteEngine& engine, const ActionRegistry& registry)
        : engine_(engine), registry_(registry) {}

    LoadResult load_node(const config::ConfigNode& root, const LoadOptions& opts = {}) {
        LoadResult result;
        disabled_.clear();

        std::vector<RuleDefinition> defs;
        read_rule_document(root, defs, result.errors);
        if (opts.stop_on_first_error && !result.errors.empty())
            return result;

        install(defs, opts, result);
        return result;
    }

    LoadResult load_definitions(const std::vector<RuleDefinition>& defs,
                               const LoadOptions& opts = {}) {
        LoadResult result;
        disabled_.clear();
        install(defs, opts, result);
        return result;
    }

    LoadResult load_from_string(const std::string& text, Format format,
                                const LoadOptions& opts = {}) {
        LoadResult result;
        disabled_.clear();

        Format resolved = (format == Format::Auto) ? format_from_text(text) : format;

        if (resolved == Format::Yaml) {
#if RETE_ENABLE_YAML
            config::ConfigNode root;
            config::ParseError perr;
            if (!config::parse_yaml(text, root, perr)) {
                add_parse_failure(result, perr);
                return result;
            }
            result = load_node(root, opts);
            return result;
#else
            detail::add_error(result.errors, ErrorCode::UnsupportedFeature, "", "",
                              "this document is YAML and YAML loading is not compiled in; rebuild "
                              "with RETE_ENABLE_YAML=1 (host builds only) or write the rules as JSON");
            return result;
#endif
        }

#if RETE_ENABLE_JSON
        config::ConfigNode root;
        config::ParseError perr;
        if (!config::parse_json(text, root, perr)) {
            add_parse_failure(result, perr);
            return result;
        }
        result = load_node(root, opts);
        // The tree has done its job. Releasing it here rather than at scope end
        // keeps the peak allocation of a load down on small targets.
        root.clear();
        return result;
#else
        (void)opts;  // no parser is compiled in, so nothing reads the options
        detail::add_error(result.errors, ErrorCode::UnsupportedFeature, "", "",
                          "JSON loading is not compiled in; rebuild with RETE_ENABLE_JSON=1");
        return result;
#endif
    }

#if !RETE_NO_IOSTREAM
    LoadResult load_from_file(const std::string& path, Format format = Format::Auto,
                              const LoadOptions& opts = {}) {
        LoadResult result;
        disabled_.clear();

        std::string text;
        if (!read_file(path, text)) {
            detail::add_error(result.errors, ErrorCode::FileNotFound, "", path,
                              "cannot open '" + path + "' for reading");
            return result;
        }

        Format resolved = format;
        if (resolved == Format::Auto) {
            resolved = format_from_path(path);
            if (resolved == Format::Auto) resolved = format_from_text(text);
        }
        return load_from_string(text, resolved, opts);
    }

    // Files are loaded in order and share one namespace: the second file's copy
    // of a rule the first file already loaded is a duplicate, not a silent
    // shadow, unless replace_existing says otherwise.
    LoadResult load_from_files(const std::vector<std::string>& paths,
                               const LoadOptions& opts = {}) {
        LoadResult total;
        std::vector<std::string> disabled;

        for (const auto& p : paths) {
            LoadResult one = load_from_file(p, Format::Auto, opts);
            total.loaded += one.loaded;
            total.rule_names.insert(total.rule_names.end(),
                                    one.rule_names.begin(), one.rule_names.end());
            total.errors.insert(total.errors.end(), one.errors.begin(), one.errors.end());
            disabled.insert(disabled.end(), disabled_.begin(), disabled_.end());
            if (opts.stop_on_first_error && !one.errors.empty()) break;
        }

        disabled_ = std::move(disabled);
        return total;
    }
#endif // !RETE_NO_IOSTREAM

    // Rules the last load parsed and validated but left out because the
    // document disabled them. They are not errors and they are not loaded, and
    // LoadResult has nowhere to put that third case, so it lives here.
    const std::vector<std::string>& disabled_rules() const { return disabled_; }

    ReteEngine&           engine()   { return engine_; }
    const ActionRegistry& registry() const { return registry_; }

private:
    void install(const std::vector<RuleDefinition>& defs, const LoadOptions& opts,
                 LoadResult& result) {
        RuleValidator validator(registry_);
        validator.set_existing_names(engine_.rule_names());
        validator.set_allow_replace(opts.replace_existing);

        std::vector<bool> accepted(defs.size(), false);
        for (std::size_t i = 0; i < defs.size(); ++i) {
            accepted[i] = validator.validate(defs[i], result.errors);
            if (!accepted[i] && opts.stop_on_first_error)
                return;
        }

        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (!accepted[i]) continue;
            const RuleDefinition& def = defs[i];

            // Removal comes first, and applies to a disabled rule too.
            // Otherwise an operator who sets enabled: false and reloads gets
            // a successful result and a robot that still does the thing,
            // because the previous version of the rule is untouched.
            if (opts.replace_existing && engine_.has_rule(def.name))
                engine_.remove_rule(def.name);

            if (!def.enabled) {
                disabled_.push_back(def.name);
                continue;
            }

            engine_.add_production(compile_rule(def, registry_));
            result.rule_names.push_back(def.name);
            ++result.loaded;
        }
    }

    static void add_parse_failure(LoadResult& result, const config::ParseError& perr) {
        detail::add_error(result.errors, ErrorCode::ParseFailure, "", "",
                          perr.message.empty() ? "the document could not be parsed" : perr.message,
                          perr.line);
    }

#if !RETE_NO_IOSTREAM
    static bool read_file(const std::string& path, std::string& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad();
    }
#endif

    ReteEngine&              engine_;
    const ActionRegistry&    registry_;
    std::vector<std::string> disabled_;
};

// ---------------------------------------------------------------------------
// Convenience wrappers for the common case of loading once and not caring
// which rules the document had disabled.
// ---------------------------------------------------------------------------

inline LoadResult load_rules(ReteEngine& engine, const ActionRegistry& registry,
                             const config::ConfigNode& root, const LoadOptions& opts = {}) {
    return RuleLoader(engine, registry).load_node(root, opts);
}

inline LoadResult load_rules(ReteEngine& engine, const ActionRegistry& registry,
                             const std::vector<RuleDefinition>& defs,
                             const LoadOptions& opts = {}) {
    return RuleLoader(engine, registry).load_definitions(defs, opts);
}

inline LoadResult load_rules_from_string(ReteEngine& engine, const ActionRegistry& registry,
                                         const std::string& text, Format format = Format::Auto,
                                         const LoadOptions& opts = {}) {
    return RuleLoader(engine, registry).load_from_string(text, format, opts);
}

#if !RETE_NO_IOSTREAM
inline LoadResult load_rules_from_file(ReteEngine& engine, const ActionRegistry& registry,
                                       const std::string& path, Format format = Format::Auto,
                                       const LoadOptions& opts = {}) {
    return RuleLoader(engine, registry).load_from_file(path, format, opts);
}
#endif

} // namespace rete::rules
