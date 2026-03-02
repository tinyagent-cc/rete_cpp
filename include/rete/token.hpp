#pragma once

#include "wme.hpp"
#include <algorithm>
#include <memory>
#include <vector>

namespace rete {

struct Token;
using TokenPtr = std::shared_ptr<Token>;

struct Token {
    TokenPtr parent;
    WmePtr   wme;
    int      index;

    static TokenPtr create(TokenPtr parent, WmePtr wme) {
        auto t    = std::make_shared<Token>();
        t->parent = std::move(parent);
        t->wme    = std::move(wme);
        if (t->parent) {
            t->index = t->parent->index + 1;
        } else {
            // Dummy root token has no WME and lives at virtual index -1.
            t->index = t->wme ? 0 : -1;
        }
        return t;
    }

    WmePtr wme_at(int i) const {
        if (i < 0) return nullptr;
        if (i == index) return wme;
        if (parent) return parent->wme_at(i);
        return nullptr;
    }

    std::vector<WmePtr> wmes() const {
        if (index < 0) return {};
        std::vector<WmePtr> result(static_cast<size_t>(index) + 1);
        const Token* t = this;
        while (t) {
            if (t->index >= 0 && t->wme)
                result[static_cast<size_t>(t->index)] = t->wme;
            t = t->parent.get();
        }
        return result;
    }

    bool contains_wme(WmeId id) const {
        if (wme && wme->id == id) return true;
        if (parent) return parent->contains_wme(id);
        return false;
    }
};

} // namespace rete
