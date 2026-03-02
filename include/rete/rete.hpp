#pragma once

#include "agenda.hpp"
#include "alpha_network.hpp"
#include "beta_network.hpp"
#include "condition.hpp"
#include "production.hpp"
#include "token.hpp"
#include "types.hpp"
#include "wme.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef RETE_HAS_BOOST
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>
#endif

namespace rete {

// Forward declaration
class RuleBuilder;

// ==========================================================================
// ReteEngine -- the main entry point
// ==========================================================================
class ReteEngine {
public:
    ReteEngine() {
        dummy_top_ = std::make_unique<BetaMemory>();
        auto dummy_tok = Token::create(nullptr, nullptr);
        static_cast<BetaMemory*>(dummy_top_.get())->tokens.push_back(dummy_tok);
    }

    // ---- Rule definition -------------------------------------------------

    RuleBuilder add_rule(const std::string& name);

    void add_production(Production prod) {
        auto p = std::make_unique<Production>(std::move(prod));
        compile_production(*p);
        productions_.push_back(std::move(p));
    }

    void remove_rule(const std::string& name) {
        auto it = std::find_if(productions_.begin(), productions_.end(),
            [&](const auto& p) { return p->name == name; });
        if (it != productions_.end()) {
            auto* pnode = production_nodes_[it->get()];
            if (pnode) {
                for (auto& tok : pnode->tokens) {
                    auto ids = wme_ids_from_token(tok);
                    agenda_.remove_for_production(it->get(), tok);
                }
                pnode->tokens.clear();
                pnode->production = nullptr;
            }
            production_nodes_.erase(it->get());
            productions_.erase(it);
        }
    }

    // ---- Working memory operations ---------------------------------------

    WmePtr assert_fact(Value id, Value attr, Value val) {
        auto wme     = std::make_shared<WME>();
        wme->id      = next_wme_id_++;
        wme->identifier = std::move(id);
        wme->attribute  = std::move(attr);
        wme->value      = std::move(val);
        wme->timetag    = timetag_++;

        working_memory_.push_back(wme);
        alpha_network_.add_wme(wme);
        alpha_network_.add_wme_to_wildcards(wme);
        return wme;
    }

    void retract_fact(const WmePtr& wme) {
        if (!wme) return;
        alpha_network_.remove_wme(wme);
        alpha_network_.remove_wme_from_wildcards(wme);

        for (auto& node : beta_nodes_)
            node->left_remove(wme->id);

        agenda_.remove_wme(wme->id);

        working_memory_.erase(
            std::remove(working_memory_.begin(), working_memory_.end(), wme),
            working_memory_.end());
    }

    void modify_fact(WmePtr& wme, Value id, Value attr, Value val) {
        retract_fact(wme);
        wme = assert_fact(std::move(id), std::move(attr), std::move(val));
    }

    // ---- Execution -------------------------------------------------------

    void run(int max_cycles = -1) {
        halted_ = false;
        rebuild_agenda_from_current_matches();
        int cycles = 0;
        while (!halted_ && !agenda_.empty()) {
            if (max_cycles >= 0 && cycles >= max_cycles) break;

            Activation act = agenda_.pop();
            auto ids = act.wme_ids();

            if (agenda_.has_fired(act.production, ids))
                continue;

            agenda_.mark_fired(act.production, ids);

            if (act.production && act.production->action) {
                Bindings bindings = act.compute_bindings();
                act.production->action(*this, bindings);
            }
            ++cycles;
        }
    }

    void halt() { halted_ = true; }

    // ---- Query -----------------------------------------------------------

    std::vector<WmePtr> facts() const { return working_memory_; }

    std::vector<WmePtr> query(const std::optional<Value>& id,
                              const std::optional<Value>& attr,
                              const std::optional<Value>& val) const {
        std::vector<WmePtr> result;
        for (auto& w : working_memory_) {
            if (id   && w->identifier != *id)  continue;
            if (attr && w->attribute  != *attr) continue;
            if (val  && w->value      != *val)  continue;
            result.push_back(w);
        }
        return result;
    }

    // ---- Configuration ---------------------------------------------------

    void set_conflict_strategy(ConflictStrategy s) { agenda_.set_strategy(s); }
    void clear_refraction() { agenda_.clear_refraction(); }

    // ---- Statistics ------------------------------------------------------

    size_t wme_count()        const { return working_memory_.size(); }
    size_t rule_count()       const { return productions_.size(); }
    size_t activation_count() const { return agenda_.size(); }

    // ---- Optional Boost BGL DOT export -----------------------------------
#ifdef RETE_HAS_BOOST
    void export_to_dot(std::ostream& os) const {
        using Graph = boost::adjacency_list<
            boost::vecS, boost::vecS, boost::directedS,
            boost::property<boost::vertex_name_t, std::string>>;
        Graph g;

        auto root = boost::add_vertex(std::string("AlphaRoot"), g);
        auto dummy = boost::add_vertex(std::string("DummyTop"), g);

        std::unordered_map<const void*, typename boost::graph_traits<Graph>::vertex_descriptor> node_map;
        node_map[dummy_top_.get()] = dummy;

        for (auto& p : productions_) {
            auto v = boost::add_vertex(std::string("P:") + p->name, g);
            node_map[production_nodes_.count(p.get()) ? production_nodes_.at(p.get()) : nullptr] = v;
        }

        boost::write_graphviz(os, g,
            boost::make_label_writer(boost::get(boost::vertex_name, g)));
    }
#endif

private:
    void rebuild_agenda_from_current_matches() {
        agenda_.clear_pending();
        for (auto& entry : production_nodes_) {
            Production* prod = entry.first;
            ProductionNode* pnode = entry.second;
            if (!prod || !pnode) continue;

            for (auto& tok : pnode->tokens) {
                Activation act;
                act.production = prod;
                act.token = tok;
                act.timetag = token_timetag(tok);
                agenda_.add(std::move(act));
            }
        }
    }

    static uint64_t token_timetag(const TokenPtr& tok) {
        uint64_t tt = 0;
        if (!tok) return tt;
        auto ws = tok->wmes();
        for (auto& w : ws) {
            if (w && w->timetag > tt) tt = w->timetag;
        }
        return tt;
    }

    // ---- Network compilation ---------------------------------------------

    void compile_production(Production& prod) {
        BetaNode* current_beta = dummy_top_.get();

        for (int i = 0; i < static_cast<int>(prod.conditions.size()); ++i) {
            const auto& cond = prod.conditions[static_cast<size_t>(i)];
            AlphaMemoryPtr am = alpha_network_.get_or_create_memory(cond);

            auto join_tests = compute_join_tests(cond, i, prod.conditions);

            bool is_last = (i == static_cast<int>(prod.conditions.size()) - 1);

            if (cond.negated) {
                auto neg = std::make_unique<NegativeNode>();
                neg->alpha_memory = am.get();
                neg->parent       = current_beta;
                neg->tests        = std::move(join_tests);

                am->neg_successors.push_back(neg.get());

                auto* bm = dynamic_cast<BetaMemory*>(current_beta);
                if (bm) bm->children.push_back(neg.get());

                if (is_last) {
                    auto pnode = std::make_unique<ProductionNode>();
                    pnode->production = &prod;
                    pnode->agenda     = &agenda_;
                    neg->children.push_back(pnode.get());
                    production_nodes_[&prod] = pnode.get();

                    activate_new_node_from_above(neg.get(), current_beta);
                    beta_nodes_.push_back(std::move(pnode));
                } else {
                    auto new_bm = std::make_unique<BetaMemory>();
                    neg->children.push_back(new_bm.get());

                    activate_new_node_from_above(neg.get(), current_beta);
                    current_beta = new_bm.get();
                    beta_nodes_.push_back(std::move(new_bm));
                }

                beta_nodes_.push_back(std::move(neg));
            } else {
                auto join = std::make_unique<JoinNode>();
                join->alpha_memory = am.get();
                join->parent       = current_beta;
                join->tests        = std::move(join_tests);

                am->join_successors.push_back(join.get());

                auto* bm = dynamic_cast<BetaMemory*>(current_beta);
                if (bm) bm->children.push_back(join.get());

                if (is_last) {
                    auto pnode = std::make_unique<ProductionNode>();
                    pnode->production = &prod;
                    pnode->agenda     = &agenda_;
                    join->children.push_back(pnode.get());
                    production_nodes_[&prod] = pnode.get();

                    activate_existing_wmes(join.get(), current_beta, am.get());
                    beta_nodes_.push_back(std::move(pnode));
                } else {
                    auto new_bm = std::make_unique<BetaMemory>();
                    join->children.push_back(new_bm.get());

                    activate_existing_wmes(join.get(), current_beta, am.get());
                    current_beta = new_bm.get();
                    beta_nodes_.push_back(std::move(new_bm));
                }

                beta_nodes_.push_back(std::move(join));
            }
        }
    }

    void activate_existing_wmes(JoinNode* join, BetaNode* parent_beta,
                                AlphaMemory* am) {
        auto* bm = dynamic_cast<BetaMemory*>(parent_beta);
        if (!bm) return;

        for (auto& tok : bm->tokens) {
            for (auto& wme : am->wmes) {
                if (JoinNode::perform_join_tests(join->tests, tok, wme)) {
                    for (auto* child : join->children)
                        child->left_activate(tok, wme);
                }
            }
        }
    }

    void activate_new_node_from_above(NegativeNode* neg, BetaNode* parent_beta) {
        auto* bm = dynamic_cast<BetaMemory*>(parent_beta);
        if (!bm) return;

        for (auto& tok : bm->tokens) {
            neg->left_activate(tok, nullptr);
        }
    }

    std::vector<JoinTest> compute_join_tests(const Condition& cond, int cond_index,
                                              const std::vector<Condition>& all_conds) {
        std::vector<JoinTest> result;

        auto check_field = [&](const FieldTest& ft, Field alpha_field) {
            auto* vb = std::get_if<VariableBinding>(&ft);
            if (!vb) return;

            for (int i = cond_index - 1; i >= 0; --i) {
                const auto& earlier = all_conds[static_cast<size_t>(i)];
                auto check_earlier = [&](const FieldTest& eft, Field ef) {
                    auto* evb = std::get_if<VariableBinding>(&eft);
                    if (evb && evb->name == vb->name) {
                        JoinTest jt;
                        jt.alpha_field    = alpha_field;
                        jt.condition_index = i;
                        jt.condition_field = ef;
                        result.push_back(jt);
                    }
                };
                check_earlier(earlier.id_test,    Field::Identifier);
                check_earlier(earlier.attr_test,  Field::Attribute);
                check_earlier(earlier.value_test, Field::Value);
            }
        };

        check_field(cond.id_test,    Field::Identifier);
        check_field(cond.attr_test,  Field::Attribute);
        check_field(cond.value_test, Field::Value);
        return result;
    }

    static std::vector<WmeId> wme_ids_from_token(const TokenPtr& tok) {
        std::vector<WmeId> ids;
        if (!tok) return ids;
        auto ws = tok->wmes();
        for (auto& w : ws)
            if (w) ids.push_back(w->id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // ---- Data members ----------------------------------------------------

    AlphaNetwork                            alpha_network_;
    std::unique_ptr<BetaNode>               dummy_top_;
    std::vector<std::unique_ptr<BetaNode>>  beta_nodes_;
    std::vector<std::unique_ptr<Production>> productions_;
    std::unordered_map<Production*, ProductionNode*> production_nodes_;
    std::vector<WmePtr>                     working_memory_;
    Agenda                                  agenda_;

    uint64_t next_wme_id_ = 0;
    uint64_t timetag_     = 0;
    bool     halted_      = false;
};

// ==========================================================================
// ProductionNode deferred implementations (needs Agenda to be complete)
// ==========================================================================
inline void ProductionNode::left_activate(TokenPtr parent, WmePtr wme) {
    auto tok = Token::create(std::move(parent), std::move(wme));
    tokens.push_back(tok);

    if (agenda && production) {
        Activation act;
        act.production = production;
        act.token      = tok;
        uint64_t tt = 0;
        for (auto& w : tok->wmes()) {
            if (w && w->timetag > tt) tt = w->timetag;
        }
        act.timetag = tt;
        agenda->add(std::move(act));
    }
}

inline void ProductionNode::left_remove(WmeId wme_id) {
    std::vector<TokenPtr> to_remove;
    tokens.erase(
        std::remove_if(tokens.begin(), tokens.end(),
            [&](const TokenPtr& t) {
                if (t->contains_wme(wme_id)) {
                    to_remove.push_back(t);
                    return true;
                }
                return false;
            }),
        tokens.end());

    if (agenda) {
        for (auto& tok : to_remove)
            agenda->remove_for_production(production, tok);
    }
}

// ==========================================================================
// RuleBuilder -- fluent API for defining rules
// ==========================================================================
class RuleBuilder {
public:
    RuleBuilder(ReteEngine& engine, std::string name)
        : engine_(engine) {
        prod_.name = std::move(name);
    }

    RuleBuilder& salience(int s) {
        prod_.salience = s;
        return *this;
    }

    RuleBuilder& when(Value id, Value attr, Value val) {
        prod_.conditions.push_back(make_condition(id, attr, val, false));
        return *this;
    }

    RuleBuilder& when(const Condition& cond) {
        prod_.conditions.push_back(cond);
        return *this;
    }

    RuleBuilder& when_not(Value id, Value attr, Value val) {
        prod_.conditions.push_back(make_condition(id, attr, val, true));
        return *this;
    }

    RuleBuilder& then(Action action) {
        prod_.action = std::move(action);
        return *this;
    }

    void build() {
        engine_.add_production(std::move(prod_));
    }

private:
    ReteEngine& engine_;
    Production  prod_;
};

inline RuleBuilder ReteEngine::add_rule(const std::string& name) {
    return RuleBuilder(*this, name);
}

} // namespace rete
