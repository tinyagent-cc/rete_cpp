#pragma once
// ---------------------------------------------------------------------------
// ActionRegistry: the only bridge from a serialized rule to executable code.
//
// A rule file names an action; it never carries one. There is no expression
// evaluator here, no script host, no dlopen, no shell. If a document names an
// action nobody registered, the rule fails validation and is not loaded --
// silently dropping it would leave a robot with a behaviour set it does not
// have.
// ---------------------------------------------------------------------------

#include "../production.hpp"
#include "../types.hpp"
#include "definition.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rete {
class ReteEngine;
}

namespace rete::rules {

// Typed read-only view of the parameters a rule attached to its action.
// Getters take a fallback rather than throwing: embedded builds have no
// exceptions, and a missing optional parameter is not an error.
class ActionParams {
public:
    ActionParams() = default;
    explicit ActionParams(const ParameterMap& p) : params_(&p) {}

    bool empty() const { return !params_ || params_->empty(); }
    std::size_t size() const { return params_ ? params_->size() : 0; }

    const Value* find(const std::string& key) const {
        if (!params_) return nullptr;
        for (const auto& kv : *params_)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool has(const std::string& key) const { return find(key) != nullptr; }

    std::string get_string(const std::string& k, const std::string& def = {}) const {
        if (auto* v = find(k)) if (auto* s = std::get_if<std::string>(v)) return *s;
        return def;
    }
    int64_t get_int(const std::string& k, int64_t def = 0) const {
        if (auto* v = find(k)) {
            if (auto* i = std::get_if<int64_t>(v)) return *i;
            if (auto* d = std::get_if<double>(v))  return static_cast<int64_t>(*d);
        }
        return def;
    }
    double get_double(const std::string& k, double def = 0.0) const {
        if (auto* v = find(k)) {
            if (auto* d = std::get_if<double>(v))  return *d;
            if (auto* i = std::get_if<int64_t>(v)) return static_cast<double>(*i);
        }
        return def;
    }
    bool get_bool(const std::string& k, bool def = false) const {
        if (auto* v = find(k)) if (auto* b = std::get_if<bool>(v)) return *b;
        return def;
    }

    const ParameterMap& all() const {
        static const ParameterMap kEmpty;
        return params_ ? *params_ : kEmpty;
    }

private:
    const ParameterMap* params_ = nullptr;
};

// What application code registers. `bindings` carries the variables the match
// bound; `params` carries what the rule file wrote.
using ActionHandler =
    std::function<void(ReteEngine& engine, const Bindings& bindings, const ActionParams& params)>;

class ActionRegistry {
public:
    // Returns false if the name was already taken and `overwrite` is false, so
    // a second registration cannot silently shadow the first.
    bool register_action(const std::string& id, ActionHandler handler, bool overwrite = false) {
        if (id.empty() || !handler) return false;
        auto it = handlers_.find(id);
        if (it != handlers_.end() && !overwrite) return false;
        handlers_[id] = std::move(handler);
        return true;
    }

    bool unregister_action(const std::string& id) { return handlers_.erase(id) > 0; }

    bool contains(const std::string& id) const { return handlers_.count(id) > 0; }

    const ActionHandler* find(const std::string& id) const {
        auto it = handlers_.find(id);
        return it == handlers_.end() ? nullptr : &it->second;
    }

    std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(handlers_.size());
        for (const auto& kv : handlers_) out.push_back(kv.first);
        return out;
    }

    std::size_t size() const { return handlers_.size(); }
    void clear() { handlers_.clear(); }

private:
    std::unordered_map<std::string, ActionHandler> handlers_;
};

} // namespace rete::rules
