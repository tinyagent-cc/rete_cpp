#pragma once

#include "condition.hpp"
#include "types.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rete {

class ReteEngine;

using Bindings = std::unordered_map<std::string, Value>;
using Action   = std::function<void(ReteEngine& engine, const Bindings& bindings)>;

struct Production {
    std::string            name;
    int                    salience = 0;
    std::vector<Condition> conditions;
    Action                 action;
};

} // namespace rete
