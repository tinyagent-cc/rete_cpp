#pragma once
// Shared by every parser adapter so the loader can report a location without
// knowing which format produced it.

#include <cstddef>
#include <string>

namespace rete::config {

struct ParseError {
    std::string message;
    std::size_t line   = 0;   // 1-based; 0 when the parser cannot say
    std::size_t column = 0;

    bool ok() const { return message.empty(); }
    void clear() { message.clear(); line = 0; column = 0; }

    std::string str() const {
        if (message.empty()) return {};
        std::string s;
        if (line) {
            s += "line " + std::to_string(line);
            if (column) s += ", column " + std::to_string(column);
            s += ": ";
        }
        return s + message;
    }
};

} // namespace rete::config
