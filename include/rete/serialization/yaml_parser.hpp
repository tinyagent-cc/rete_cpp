#pragma once
// ---------------------------------------------------------------------------
// YAML adapter: yaml-cpp in, ConfigNode out.
//
// This file is the only place in the project that names a yaml-cpp type. That
// is the point of it. YAML is a host-side convenience for hand-written rule
// files; the engine, the loader and the embedded profile must stay ignorant of
// it, so everything yaml-cpp offers -- its node handles, its exceptions, its
// YAML 1.1 style conversions -- stops at the boundary below.
//
// Two behaviours are worth calling out because they are the ones a rule author
// will notice:
//
//   Scalars are typed by the YAML 1.2 core schema, resolved here rather than
//   through yaml-cpp's converters. Its converters follow YAML 1.1, where `no`
//   is a boolean and a robot that reads `announce: no` gets false instead of
//   the string it was told to expect.
//
//   Quoting is honoured. `value: "20"` stays the string "20", so a rule that
//   compares an attribute against a string does not silently slip past a
//   numeric guard because YAML felt helpful.
// ---------------------------------------------------------------------------

#include "../config.hpp"

#if RETE_ENABLE_YAML

#if !RETE_HAS_EXCEPTIONS
#  error "RETE_ENABLE_YAML requires exceptions: yaml-cpp reports every failure \
by throwing. Targets built with -fno-exceptions load rules as JSON."
#endif

#include "../types.hpp"
#include "config_node.hpp"
#include "parse_error.hpp"

#include <yaml-cpp/yaml.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace rete::config {
namespace yaml_detail {

// Tags as yaml-cpp hands them back. "!" is the non-specific tag it gives every
// non-plain scalar: single quoted, double quoted, and both block forms. "?" is
// the one it gives a plain scalar, which is the only case that gets resolved.
inline constexpr const char* kTagNonSpecific = "!";
inline constexpr const char* kTagPlain       = "?";
inline constexpr const char* kTagStr         = "tag:yaml.org,2002:str";
inline constexpr const char* kTagInt         = "tag:yaml.org,2002:int";
inline constexpr const char* kTagBool        = "tag:yaml.org,2002:bool";
inline constexpr const char* kTagFloat       = "tag:yaml.org,2002:float";
inline constexpr const char* kTagNull        = "tag:yaml.org,2002:null";

// A rule document is a handful of keys deep. Anything past this is either a
// mistake or a recursive anchor, and both are better as an error than as a
// stack overflow on a device with a 4 KB stack.
inline constexpr int kMaxDepth = 64;

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// core schema: null | Null | NULL | ~ | (empty)
inline bool core_null(const std::string& s) {
    return s.empty() || s == "~" || s == "null" || s == "Null" || s == "NULL";
}

// core schema: true | True | TRUE | false | False | FALSE. Deliberately not
// yes/no/on/off, which are YAML 1.1 and a well known source of surprise.
inline bool core_bool(const std::string& s, bool& out) {
    if (s == "true" || s == "True" || s == "TRUE")    { out = true;  return true; }
    if (s == "false" || s == "False" || s == "FALSE") { out = false; return true; }
    return false;
}

// core schema: [-+]?[0-9]+ | 0o[0-7]+ | 0x[0-9a-fA-F]+
// The based forms carry no sign, exactly as the schema spells them.
inline bool core_int(const std::string& s, int64_t& out) {
    if (s.empty()) return false;

    int         base     = 10;
    std::size_t digits_at = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'o')) {
        base      = (s[1] == 'x') ? 16 : 8;
        digits_at = 2;
        for (std::size_t i = digits_at; i < s.size(); ++i) {
            const bool ok = (base == 16) ? is_hex(s[i]) : (s[i] >= '0' && s[i] <= '7');
            if (!ok) return false;
        }
    } else {
        const std::size_t first = (s[0] == '-' || s[0] == '+') ? 1u : 0u;
        if (first >= s.size()) return false;
        for (std::size_t i = first; i < s.size(); ++i)
            if (!is_digit(s[i])) return false;
    }

    errno = 0;
    char*           end   = nullptr;
    const char*     begin = s.c_str() + digits_at;
    const long long v     = std::strtoll(begin, &end, base);
    // Out of range falls through to the float rule below, which is lossy but
    // closer to the written value than dropping to a string would be.
    if (errno == ERANGE || end != s.c_str() + s.size()) return false;
    out = static_cast<int64_t>(v);
    return true;
}

// core schema: [-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)? plus the
// .inf and .nan spellings. Validated by hand because strtod also accepts
// "0x1p4", "inf" and "nan", none of which the core schema calls a float.
inline bool core_float(const std::string& s, double& out) {
    if (s.empty()) return false;

    const std::size_t first = (s[0] == '-' || s[0] == '+') ? 1u : 0u;
    const std::string body  = s.substr(first);
    if (body == ".inf" || body == ".Inf" || body == ".INF") {
        const double inf = std::numeric_limits<double>::infinity();
        out = (s[0] == '-') ? -inf : inf;
        return true;
    }
    if (s == ".nan" || s == ".NaN" || s == ".NAN") {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }

    std::size_t i             = first;
    bool        digits_before = false;
    bool        digits_after  = false;
    while (i < s.size() && is_digit(s[i])) { digits_before = true; ++i; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && is_digit(s[i])) { digits_after = true; ++i; }
    }
    if (!digits_before && !digits_after) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        bool exponent_digits = false;
        while (i < s.size() && is_digit(s[i])) { exponent_digits = true; ++i; }
        if (!exponent_digits) return false;
    }
    if (i != s.size()) return false;

    out = std::strtod(s.c_str(), nullptr);
    return true;
}

inline bool fail(ParseError& err, const YAML::Node& node, std::string message) {
    err.message = std::move(message);
    const YAML::Mark mark = node.Mark();
    if (!mark.is_null() && mark.line >= 0) {
        err.line = static_cast<std::size_t>(mark.line) + 1;   // yaml-cpp counts from 0
        if (mark.column >= 0) err.column = static_cast<std::size_t>(mark.column) + 1;
    }
    return false;
}

inline bool scalar_to_node(const YAML::Node& in, ConfigNode& out, ParseError& err) {
    const std::string& tag = in.Tag();
    const std::string& s   = in.Scalar();

    // Quoted, block, or explicitly !!str: a string, whatever it looks like.
    if (tag == kTagNonSpecific || tag == kTagStr) {
        out = ConfigNode(Value(s));
        return true;
    }
    if (tag == kTagNull) {
        out = ConfigNode();
        return true;
    }
    if (tag == kTagBool) {
        bool b = false;
        if (!core_bool(s, b)) return fail(err, in, "not a boolean: '" + s + "'");
        out = ConfigNode(Value(b));
        return true;
    }
    if (tag == kTagInt) {
        int64_t i = 0;
        if (!core_int(s, i)) return fail(err, in, "not an integer: '" + s + "'");
        out = ConfigNode(Value(i));
        return true;
    }
    if (tag == kTagFloat) {
        double d = 0.0;
        if (!core_float(s, d)) return fail(err, in, "not a float: '" + s + "'");
        out = ConfigNode(Value(d));
        return true;
    }
    // An application tag has nowhere to go in rete::Value, so the text
    // survives and validation upstream decides whether it is acceptable.
    if (tag != kTagPlain && !tag.empty()) {
        out = ConfigNode(Value(s));
        return true;
    }

    if (core_null(s)) { out = ConfigNode(); return true; }
    bool b = false;
    if (core_bool(s, b)) { out = ConfigNode(Value(b)); return true; }
    int64_t i = 0;
    if (core_int(s, i)) { out = ConfigNode(Value(i)); return true; }
    double d = 0.0;
    if (core_float(s, d)) { out = ConfigNode(Value(d)); return true; }
    out = ConfigNode(Value(s));
    return true;
}

inline bool convert(const YAML::Node& in, ConfigNode& out, int depth, ParseError& err) {
    if (depth > kMaxDepth)
        return fail(err, in, "document nests deeper than " + std::to_string(kMaxDepth) +
                             " levels, or an anchor refers to itself");

    switch (in.Type()) {
    case YAML::NodeType::Undefined:
    case YAML::NodeType::Null:
        out = ConfigNode();
        return true;

    case YAML::NodeType::Scalar:
        return scalar_to_node(in, out, err);

    case YAML::NodeType::Sequence: {
        ConfigSeq& seq = out.seq_mut();
        seq.reserve(in.size());
        for (const auto& item : in) {
            seq.emplace_back();
            if (!convert(item, seq.back(), depth + 1, err)) return false;
        }
        return true;
    }

    case YAML::NodeType::Map: {
        // Document order is preserved because ConfigMap is a vector and
        // yaml-cpp iterates a map in the order the file wrote it.
        ConfigMap& map = out.map_mut();
        map.reserve(in.size());
        for (const auto& kv : in) {
            if (!kv.first.IsScalar())
                return fail(err, kv.first, "map keys must be scalars");
            map.emplace_back(kv.first.Scalar(), ConfigNode{});
            if (!convert(kv.second, map.back().second, depth + 1, err)) return false;
        }
        return true;
    }
    }
    return fail(err, in, "unsupported YAML node type");
}

} // namespace yaml_detail

// Parses one YAML document into `out`. Returns false and fills `err` on any
// failure; an empty stream is a success producing a null node.
//
// Nothing thrown by yaml-cpp escapes this function. That is not politeness,
// it is the contract the rule loader is built on: it reports errors, it never
// throws, and it has to behave the same whichever parser fed it.
inline bool parse_yaml(const std::string& text, ConfigNode& out, ParseError& err) {
    out.clear();
    err.clear();

    try {
        // LoadAll rather than Load, so a stream that carries several documents
        // is refused instead of quietly reducing to its first one. A behaviour
        // file whose second half was ignored is worse than one that fails.
        const std::vector<YAML::Node> docs = YAML::LoadAll(text);

        if (docs.empty()) return true;

        if (docs.size() > 1) {
            err.message = "expected a single YAML document, found " +
                          std::to_string(docs.size()) +
                          "; remove the '---' separators or split the file";
            const YAML::Mark mark = docs[1].Mark();
            if (!mark.is_null() && mark.line >= 0)
                err.line = static_cast<std::size_t>(mark.line) + 1;
            return false;
        }

        if (!yaml_detail::convert(docs.front(), out, 0, err)) {
            out.clear();
            return false;
        }
        return true;
    } catch (const YAML::Exception& e) {
        // e.msg is the bare text; e.what() prefixes it with the location,
        // which ParseError::str() adds back itself.
        err.message = e.msg.empty() ? std::string(e.what()) : e.msg;
        if (!e.mark.is_null() && e.mark.line >= 0) {
            err.line = static_cast<std::size_t>(e.mark.line) + 1;
            if (e.mark.column >= 0) err.column = static_cast<std::size_t>(e.mark.column) + 1;
        }
    } catch (const std::exception& e) {
        err.message = e.what();
    } catch (...) {
        err.message = "unknown error while parsing YAML";
    }

    out.clear();
    if (err.message.empty()) err.message = "YAML parse failed";
    return false;
}

} // namespace rete::config

#endif // RETE_ENABLE_YAML
