#pragma once

#include "alpha_network.hpp"
#include "condition.hpp"
#include "production.hpp"
#include "token.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace rete {

class Agenda;

// --------------------------------------------------------------------------
// BetaNode: abstract base for all beta-side nodes
// --------------------------------------------------------------------------
class BetaNode {
public:
    virtual ~BetaNode() = default;
    virtual void left_activate(TokenPtr token, WmePtr wme)  = 0;
    virtual void left_remove(WmeId wme_id)                  = 0;
};

// --------------------------------------------------------------------------
// BetaMemory: stores tokens (partial matches) and feeds successor join nodes
// --------------------------------------------------------------------------
class BetaMemory : public BetaNode {
public:
    std::vector<TokenPtr>   tokens;
    std::vector<BetaNode*>  children;

    void left_activate(TokenPtr parent, WmePtr wme) override {
        auto tok = Token::create(std::move(parent), std::move(wme));
        tokens.push_back(tok);
        for (auto* child : children)
            child->left_activate(tok, nullptr);
    }

    void left_remove(WmeId wme_id) override {
        std::vector<TokenPtr> removed;
        tokens.erase(
            std::remove_if(tokens.begin(), tokens.end(),
                [&](const TokenPtr& t) {
                    if (t->contains_wme(wme_id)) {
                        removed.push_back(t);
                        return true;
                    }
                    return false;
                }),
            tokens.end());

        for (auto* child : children)
            child->left_remove(wme_id);
    }
};

// --------------------------------------------------------------------------
// JoinNode: the heart of the beta network. Has an alpha memory (right input)
// and a beta parent (left input). Performs join tests on activation.
// --------------------------------------------------------------------------
class JoinNode : public BetaNode {
public:
    AlphaMemory*            alpha_memory = nullptr;
    BetaNode*               parent       = nullptr;
    std::vector<JoinTest>   tests;
    std::vector<BetaNode*>  children;

    static bool perform_join_tests(const std::vector<JoinTest>& tests,
                                   const TokenPtr& token,
                                   const WmePtr& wme) {
        for (auto& jt : tests) {
            Value alpha_val = wme->field(jt.alpha_field);
            WmePtr earlier  = token ? token->wme_at(jt.condition_index) : nullptr;
            if (!earlier) return false;
            Value cond_val = earlier->field(jt.condition_field);
            if (alpha_val != cond_val) return false;
        }
        return true;
    }

    void right_activate(WmePtr wme) {
        auto* bm = dynamic_cast<BetaMemory*>(parent);
        if (bm) {
            for (auto& tok : bm->tokens) {
                if (perform_join_tests(tests, tok, wme)) {
                    for (auto* child : children)
                        child->left_activate(tok, wme);
                }
            }
        } else {
            if (tests.empty() || !parent) {
                for (auto* child : children)
                    child->left_activate(nullptr, wme);
            }
        }
    }

    void left_activate(TokenPtr token, WmePtr /*unused*/) override {
        if (!alpha_memory) return;
        for (auto& wme : alpha_memory->wmes) {
            if (perform_join_tests(tests, token, wme)) {
                for (auto* child : children)
                    child->left_activate(token, wme);
            }
        }
    }

    void left_remove(WmeId wme_id) override {
        for (auto* child : children)
            child->left_remove(wme_id);
    }
};

// --------------------------------------------------------------------------
// NegativeNode: propagates tokens only when NO matching WME exists in the
// alpha memory. Maintains a count of matches per token.
// --------------------------------------------------------------------------
class NegativeNode : public BetaNode {
public:
    AlphaMemory*            alpha_memory = nullptr;
    BetaNode*               parent       = nullptr;
    std::vector<JoinTest>   tests;
    std::vector<BetaNode*>  children;

    struct NegEntry {
        TokenPtr            token;
        std::vector<WmePtr> join_results;
    };
    std::vector<NegEntry> entries;

    void left_activate(TokenPtr parent_tok, WmePtr /*wme*/) override {
        // For negation we keep the left token unchanged; the "negative condition"
        // position is represented by downstream memory/terminal extension.
        auto tok = std::move(parent_tok);
        NegEntry entry;
        entry.token = tok;

        if (alpha_memory) {
            for (auto& aw : alpha_memory->wmes) {
                if (JoinNode::perform_join_tests(tests, tok, aw))
                    entry.join_results.push_back(aw);
            }
        }

        entries.push_back(entry);

        if (entry.join_results.empty()) {
            for (auto* child : children)
                child->left_activate(tok, nullptr);
        }
    }

    void right_activate(WmePtr wme) {
        for (auto& entry : entries) {
            if (JoinNode::perform_join_tests(tests, entry.token, wme)) {
                bool was_empty = entry.join_results.empty();
                entry.join_results.push_back(wme);
                if (was_empty) {
                    auto ws = entry.token ? entry.token->wmes() : std::vector<WmePtr>{};
                    for (auto& tw : ws) {
                        if (!tw) continue;
                        for (auto* child : children)
                            child->left_remove(tw->id);
                    }
                }
            }
        }
    }

    void right_remove(WmePtr wme) {
        for (auto& entry : entries) {
            auto it = std::find(entry.join_results.begin(), entry.join_results.end(), wme);
            if (it != entry.join_results.end()) {
                entry.join_results.erase(it);
                if (entry.join_results.empty()) {
                    for (auto* child : children)
                        child->left_activate(entry.token, nullptr);
                }
            }
        }
    }

    void left_remove(WmeId wme_id) override {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [&](const NegEntry& e) {
                    return e.token->contains_wme(wme_id);
                }),
            entries.end());

        for (auto* child : children)
            child->left_remove(wme_id);
    }
};

// --------------------------------------------------------------------------
// ProductionNode (p-node): terminal node. Creates activations on the agenda.
// --------------------------------------------------------------------------
class ProductionNode : public BetaNode {
public:
    Production*          production = nullptr;
    Agenda*              agenda     = nullptr;
    std::vector<TokenPtr> tokens;

    void left_activate(TokenPtr parent, WmePtr wme) override;
    void left_remove(WmeId wme_id) override;
};

// Deferred implementations that depend on Agenda (defined after agenda.hpp)
// are in rete.hpp.

// --------------------------------------------------------------------------
// Deferred AlphaMemory implementations (depend on JoinNode/NegativeNode)
// --------------------------------------------------------------------------
inline void AlphaMemory::activate(WmePtr wme) {
    wmes.push_back(wme);
    for (auto* jn : join_successors)
        jn->right_activate(wme);
    for (auto* nn : neg_successors)
        nn->right_activate(wme);
}

inline void AlphaMemory::remove(WmePtr wme) {
    wmes.erase(std::remove(wmes.begin(), wmes.end(), wme), wmes.end());
    for (auto* nn : neg_successors)
        nn->right_remove(wme);
}

} // namespace rete
