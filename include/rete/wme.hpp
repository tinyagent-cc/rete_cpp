#pragma once

#include "types.hpp"
#include <memory>

namespace rete {

using WmeId = uint64_t;

struct WME {
    WmeId    id;
    Value    identifier;
    Value    attribute;
    Value    value;
    uint64_t timetag;

    Value field(Field f) const {
        switch (f) {
        case Field::Identifier: return identifier;
        case Field::Attribute:  return attribute;
        case Field::Value:      return value;
        }
        return {};
    }

    bool operator==(const WME& o) const {
        return identifier == o.identifier &&
               attribute  == o.attribute  &&
               value      == o.value;
    }

    std::string to_string() const {
        return "(" + value_to_string(identifier) + " ^" +
               value_to_string(attribute) + " " +
               value_to_string(value) + ")";
    }
};

using WmePtr = std::shared_ptr<WME>;

} // namespace rete
