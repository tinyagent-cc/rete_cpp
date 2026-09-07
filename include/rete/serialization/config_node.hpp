#pragma once
// ---------------------------------------------------------------------------
// ConfigNode: the one document shape both parsers produce.
//
// JSON and YAML deserialize into this, and only this. Nothing downstream of
// here knows which format it came from, which is what makes "the same rule in
// either format produces the same Production" a structural guarantee rather
// than a promise.
//
// It is deliberately tiny: scalars reuse rete::Value, maps are a flat vector
// of pairs (rule documents have a handful of keys, so a vector beats a tree),
// and the whole thing is meant to be destroyed the moment rules are compiled.
// ---------------------------------------------------------------------------

#include "../types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace rete::config {

class ConfigNode;
using ConfigSeq = std::vector<ConfigNode>;
using ConfigMap = std::vector<std::pair<std::string, ConfigNode>>;

class ConfigNode {
public:
    enum class Kind { Null, Scalar, Sequence, Map };

    ConfigNode() = default;
    explicit ConfigNode(Value v) : kind_(Kind::Scalar), scalar_(std::move(v)) {}
    explicit ConfigNode(ConfigSeq s) : kind_(Kind::Sequence), seq_(std::move(s)) {}
    explicit ConfigNode(ConfigMap m) : kind_(Kind::Map), map_(std::move(m)) {}

    Kind kind() const { return kind_; }
    bool is_null()   const { return kind_ == Kind::Null; }
    bool is_scalar() const { return kind_ == Kind::Scalar; }
    bool is_seq()    const { return kind_ == Kind::Sequence; }
    bool is_map()    const { return kind_ == Kind::Map; }

    const Value&     scalar() const { return scalar_; }
    const ConfigSeq& seq()    const { return seq_; }
    const ConfigMap& map()    const { return map_; }

    ConfigSeq& seq_mut() { kind_ = Kind::Sequence; return seq_; }
    ConfigMap& map_mut() { kind_ = Kind::Map;      return map_; }

    // Map lookup. Returns nullptr when absent, so callers branch on presence
    // rather than on a thrown exception -- embedded builds have none.
    const ConfigNode* find(const std::string& key) const {
        if (kind_ != Kind::Map) return nullptr;
        for (const auto& kv : map_)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool has(const std::string& key) const { return find(key) != nullptr; }

    // Scalar accessors. `ok` reports whether the node really held that type;
    // no coercion happens silently, because a rule that says salience: "high"
    // must fail validation rather than quietly become 0.
    std::string as_string(bool* ok = nullptr) const {
        if (auto* s = std::get_if<std::string>(&scalar_)) { if (ok) *ok = kind_ == Kind::Scalar; return *s; }
        if (ok) *ok = false;
        return {};
    }
    int64_t as_int(bool* ok = nullptr) const {
        if (kind_ == Kind::Scalar) {
            if (auto* i = std::get_if<int64_t>(&scalar_)) { if (ok) *ok = true; return *i; }
            if (auto* d = std::get_if<double>(&scalar_)) {
                auto t = static_cast<int64_t>(*d);
                if (static_cast<double>(t) == *d) { if (ok) *ok = true; return t; }
            }
        }
        if (ok) *ok = false;
        return 0;
    }
    bool as_bool(bool* ok = nullptr) const {
        if (auto* b = std::get_if<bool>(&scalar_)) { if (ok) *ok = kind_ == Kind::Scalar; return *b; }
        if (ok) *ok = false;
        return false;
    }

    void clear() { kind_ = Kind::Null; scalar_ = Value{}; seq_.clear(); map_.clear();
                   seq_.shrink_to_fit(); map_.shrink_to_fit(); }

private:
    Kind      kind_ = Kind::Null;
    Value     scalar_{};
    ConfigSeq seq_;
    ConfigMap map_;
};

} // namespace rete::config
