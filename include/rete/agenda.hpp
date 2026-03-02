#pragma once

#include "production.hpp"
#include "token.hpp"
#include <algorithm>
#include <set>
#include <vector>

namespace rete {

struct Activation {
    Production* production = nullptr;
    TokenPtr    token;
    uint64_t    timetag = 0;

    Bindings compute_bindings() const {
        Bindings bindings;
        if (!production || !token) return bindings;

        auto all_wmes = token->wmes();
        for (int ci = 0; ci < static_cast<int>(production->conditions.size()); ++ci) {
            const auto& cond = production->conditions[static_cast<size_t>(ci)];
            WmePtr w = (ci < static_cast<int>(all_wmes.size())) ? all_wmes[static_cast<size_t>(ci)] : nullptr;
            if (!w) continue;

            auto bind_field = [&](const FieldTest& ft, Field f) {
                if (auto* vb = std::get_if<VariableBinding>(&ft))
                    bindings[vb->name] = w->field(f);
            };
            bind_field(cond.id_test,    Field::Identifier);
            bind_field(cond.attr_test,  Field::Attribute);
            bind_field(cond.value_test, Field::Value);
        }
        return bindings;
    }

    std::vector<WmeId> wme_ids() const {
        std::vector<WmeId> ids;
        if (!token) return ids;
        auto ws = token->wmes();
        for (auto& w : ws)
            if (w) ids.push_back(w->id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }
};

enum class ConflictStrategy {
    Priority,
    Recency,
    Specificity,
    FIFO
};

class Agenda {
public:
    void set_strategy(ConflictStrategy s) { strategy_ = s; }

    void add(Activation act) {
        activations_.push_back(std::move(act));
    }

    void remove_for_production(Production* prod, const TokenPtr& token) {
        activations_.erase(
            std::remove_if(activations_.begin(), activations_.end(),
                [&](const Activation& a) {
                    return a.production == prod && a.token == token;
                }),
            activations_.end());
    }

    void remove_wme(WmeId wme_id) {
        activations_.erase(
            std::remove_if(activations_.begin(), activations_.end(),
                [&](const Activation& a) {
                    return a.token && a.token->contains_wme(wme_id);
                }),
            activations_.end());
    }

    bool empty() const { return activations_.empty(); }
    size_t size() const { return activations_.size(); }
    void clear_pending() { activations_.clear(); }

    Activation pop() {
        sort_by_strategy();
        Activation best = std::move(activations_.back());
        activations_.pop_back();
        return best;
    }

    bool has_fired(Production* prod, const std::vector<WmeId>& ids) const {
        auto key = std::make_pair(prod, ids);
        return refraction_set_.count(key) > 0;
    }

    void mark_fired(Production* prod, std::vector<WmeId> ids) {
        refraction_set_.insert(std::make_pair(prod, std::move(ids)));
    }

    void clear_refraction() { refraction_set_.clear(); }

    const std::vector<Activation>& activations() const { return activations_; }

private:
    void sort_by_strategy() {
        std::stable_sort(activations_.begin(), activations_.end(),
            [this](const Activation& a, const Activation& b) {
                switch (strategy_) {
                case ConflictStrategy::Priority:
                    return a.production->salience < b.production->salience;
                case ConflictStrategy::Recency:
                    return a.timetag < b.timetag;
                case ConflictStrategy::Specificity:
                    return a.production->conditions.size() < b.production->conditions.size();
                case ConflictStrategy::FIFO:
                    return a.timetag > b.timetag;
                }
                return false;
            });
    }

    ConflictStrategy strategy_ = ConflictStrategy::Priority;
    std::vector<Activation> activations_;
    std::set<std::pair<Production*, std::vector<WmeId>>> refraction_set_;
};

} // namespace rete
