#pragma once

#include "condition.hpp"
#include "wme.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rete {

class JoinNode;
class NegativeNode;

// --------------------------------------------------------------------------
// AlphaMemory: stores WMEs that passed all constant tests for one condition
// pattern. Successor join/negative nodes are notified on add/remove.
// --------------------------------------------------------------------------
class AlphaMemory {
public:
    std::vector<WmePtr>         wmes;
    std::vector<JoinNode*>      join_successors;
    std::vector<NegativeNode*>  neg_successors;

    void activate(WmePtr wme);
    void remove(WmePtr wme);
};

using AlphaMemoryPtr = std::shared_ptr<AlphaMemory>;

// --------------------------------------------------------------------------
// ConstantTestNode: one level in the alpha discrimination trie.
// Tests a single (Field, Value) pair. Children test deeper fields.
// A leaf may have an output_memory.
// --------------------------------------------------------------------------
struct ConstantTestNode {
    Field field;
    Value test_value;

    std::vector<std::unique_ptr<ConstantTestNode>> children;
    AlphaMemoryPtr output_memory;

    void activate(const WmePtr& wme) {
        if (wme->field(field) != test_value) return;

        if (output_memory)
            output_memory->activate(wme);

        for (auto& child : children)
            child->activate(wme);
    }

    void remove(const WmePtr& wme) {
        if (wme->field(field) != test_value) return;

        if (output_memory)
            output_memory->remove(wme);

        for (auto& child : children)
            child->remove(wme);
    }
};

// --------------------------------------------------------------------------
// AlphaNetwork: root of the alpha discrimination trie plus a hash-based
// index mapping condition constant-test signatures to alpha memories
// (Doorenbos optimisation for O(1) alpha memory lookup at compile time).
// --------------------------------------------------------------------------
struct AlphaKey {
    std::optional<Value> id_test;
    std::optional<Value> attr_test;
    std::optional<Value> val_test;

    bool operator==(const AlphaKey& o) const {
        return id_test == o.id_test &&
               attr_test == o.attr_test &&
               val_test == o.val_test;
    }
};

struct AlphaKeyHash {
    ValueHash vh;
    std::size_t operator()(const AlphaKey& k) const {
        std::size_t h = 0;
        if (k.id_test)   h ^= vh(*k.id_test)   * 2654435761u;
        if (k.attr_test) h ^= vh(*k.attr_test)  * 40503u;
        if (k.val_test)  h ^= vh(*k.val_test)   * 12582917u;
        return h;
    }
};

class AlphaNetwork {
public:
    void add_wme(const WmePtr& wme) {
        for (auto& child : root_children_)
            child->activate(wme);
    }

    void remove_wme(const WmePtr& wme) {
        for (auto& child : root_children_)
            child->remove(wme);
    }

    AlphaMemoryPtr get_or_create_memory(const Condition& cond) {
        AlphaKey key = make_key(cond);
        auto it = memory_index_.find(key);
        if (it != memory_index_.end())
            return it->second;

        auto mem = std::make_shared<AlphaMemory>();
        memory_index_[key] = mem;

        ConstantTestNode* current = nullptr;
        auto attach = [&](Field f, const Value& v) {
            if (current == nullptr) {
                for (auto& c : root_children_) {
                    if (c->field == f && c->test_value == v) {
                        current = c.get();
                        return;
                    }
                }
                root_children_.push_back(std::make_unique<ConstantTestNode>());
                current = root_children_.back().get();
                current->field = f;
                current->test_value = v;
            } else {
                for (auto& c : current->children) {
                    if (c->field == f && c->test_value == v) {
                        current = c.get();
                        return;
                    }
                }
                current->children.push_back(std::make_unique<ConstantTestNode>());
                current = current->children.back().get();
                current->field = f;
                current->test_value = v;
            }
        };

        if (key.id_test)   attach(Field::Identifier, *key.id_test);
        if (key.attr_test) attach(Field::Attribute,   *key.attr_test);
        if (key.val_test)  attach(Field::Value,        *key.val_test);

        if (current)
            current->output_memory = mem;
        else {
            wildcard_memories_.push_back(mem);
        }

        return mem;
    }

    const auto& wildcard_memories() const { return wildcard_memories_; }

    void add_wme_to_wildcards(const WmePtr& wme) {
        for (auto& m : wildcard_memories_)
            m->activate(wme);
    }

    void remove_wme_from_wildcards(const WmePtr& wme) {
        for (auto& m : wildcard_memories_)
            m->remove(wme);
    }

private:
    static AlphaKey make_key(const Condition& cond) {
        AlphaKey k;
        auto extract = [](const FieldTest& ft) -> std::optional<Value> {
            if (auto* ct = std::get_if<ConstantTest>(&ft))
                return ct->value;
            return std::nullopt;
        };
        k.id_test   = extract(cond.id_test);
        k.attr_test = extract(cond.attr_test);
        k.val_test  = extract(cond.value_test);
        return k;
    }

    std::vector<std::unique_ptr<ConstantTestNode>> root_children_;
    std::unordered_map<AlphaKey, AlphaMemoryPtr, AlphaKeyHash> memory_index_;
    std::vector<AlphaMemoryPtr> wildcard_memories_;
};

} // namespace rete
