#pragma once
// ---------------------------------------------------------------------------
// A JSON reader that produces a ConfigNode, and nothing else.
//
// rete_cpp has no required dependencies, and the embedded targets (RP2040,
// ESP32) must not acquire one for the sake of a rule file with six keys in it.
// So this is hand-written rather than vendored: RFC 8259 in the small, sized to
// the schema it has to read, with every error located.
//
// All the JSON knowledge lives inside this file. Callers see parse_json() and
// a ConfigNode, which is what makes swapping in ArduinoJson later a one-file
// change instead of a refactor.
//
// Deliberately strict. A rule file is configuration for something that will act
// on the world, so the reader rejects the usual JSON-adjacent dialects instead
// of guessing: no trailing commas, no unquoted or single-quoted keys, no
// duplicate keys, no leading zeros, no trailing content.
// ---------------------------------------------------------------------------

#include "../config.hpp"

#if RETE_ENABLE_JSON

#include "config_node.hpp"
#include "parse_error.hpp"

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace rete::config {

// Bounds the recursion in parse_value(). This is a security property, not a
// style choice: rule documents may one day arrive over a network, and a few
// hundred bytes of "[[[[[..." must return an error rather than run the stack
// off the end. 64 is far past anything the rule schema can produce.
inline constexpr std::size_t kJsonMaxDepth = 64;

namespace detail {

class JsonReader {
public:
    JsonReader(const char* data, std::size_t len, ParseError& err)
        : begin_(data), p_(data), end_(data + len), err_(err) {}

    bool run(ConfigNode& out) {
        // A UTF-8 BOM is legal to reject, but "unexpected character" would send
        // the reader hunting for an invisible problem, so name it.
        if (end_ - p_ >= 3 && std::memcmp(p_, "\xEF\xBB\xBF", 3) == 0)
            return fail(p_, "unexpected UTF-8 byte order mark");
        if (!parse_value(out, 0)) return false;
        skip_ws();
        if (p_ != end_) return fail(p_, "trailing content after the top-level value");
        return true;
    }

private:
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    // What may legally follow a bare number or literal.
    static bool is_delim(char c) { return is_ws(c) || c == ',' || c == ']' || c == '}'; }

    void skip_ws() { while (p_ < end_ && is_ws(*p_)) ++p_; }

    // Line and column are computed only on the failure path, which keeps the
    // happy path free of bookkeeping and the counting impossible to get out of
    // step. Column counts bytes, not codepoints.
    bool fail(const char* pos, std::string msg) {
        std::size_t line = 1, col = 1;
        for (const char* q = begin_; q < pos && q < end_; ++q) {
            if (*q == '\n') { line += 1; col = 1; } else { col += 1; }
        }
        err_.message = std::move(msg);
        err_.line = line;
        err_.column = col;
        return false;
    }

    static void encode_utf8(std::uint32_t cp, std::string& out) {
        const auto b = [&out](std::uint32_t v) { out.push_back(static_cast<char>(v)); };
        if (cp < 0x80)     { b(cp); return; }
        if (cp < 0x800)    { b(0xC0 | (cp >> 6)); b(0x80 | (cp & 0x3F)); return; }
        if (cp < 0x10000)  { b(0xE0 | (cp >> 12)); b(0x80 | ((cp >> 6) & 0x3F));
                             b(0x80 | (cp & 0x3F)); return; }
        b(0xF0 | (cp >> 18));         b(0x80 | ((cp >> 12) & 0x3F));
        b(0x80 | ((cp >> 6) & 0x3F)); b(0x80 | (cp & 0x3F));
    }

    bool read_hex4(std::uint32_t& cp) {
        if (end_ - p_ < 4) return false;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = p_[i];
            std::uint32_t d;
            if (c >= '0' && c <= '9')      d = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
            v = (v << 4) | d;
        }
        p_ += 4;
        cp = v;
        return true;
    }

    // p_ sits on the opening quote on entry, one past the closing quote on exit.
    bool parse_string(std::string& out) {
        const char* open = p_;
        ++p_;
        // One scan to find the terminator. It gives an exact reserve, and lets
        // the common escape-free key or name copy in a single assign().
        const char* q = p_;
        bool escaped = false;
        while (q < end_) {
            const unsigned char c = static_cast<unsigned char>(*q);
            if (c == '"') break;
            if (c == '\\') { escaped = true; q += 2; continue; }
            // A newline inside a string is a control character, but the cause
            // is almost always a missing closing quote, so say that instead and
            // point at the quote that opened the run.
            if (c == '\n' || c == '\r')
                return fail(open, "unterminated string: a newline cannot appear inside a string");
            if (c < 0x20) return fail(q, "unescaped control character in string");
            ++q;
        }
        if (q >= end_) return fail(open, "unterminated string");

        if (!escaped) {
            out.assign(p_, static_cast<std::size_t>(q - p_));
            p_ = q + 1;
            return true;
        }

        out.clear();
        out.reserve(static_cast<std::size_t>(q - p_));
        while (p_ < end_) {
            const unsigned char c = static_cast<unsigned char>(*p_);
            if (c == '"') { ++p_; return true; }
            if (c == '\n' || c == '\r')
                return fail(open, "unterminated string: a newline cannot appear inside a string");
            if (c < 0x20) return fail(p_, "unescaped control character in string");
            if (c != '\\') { out.push_back(*p_++); continue; }
            const char* esc = p_;
            ++p_;
            if (p_ >= end_) return fail(open, "unterminated string");
            const char e = *p_++;
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!read_hex4(cp))
                        return fail(esc, "\\u escape needs four hexadecimal digits");
                    if (cp >= 0xDC00 && cp <= 0xDFFF)
                        return fail(esc, "lone low surrogate in \\u escape");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (end_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u')
                            return fail(esc, "lone high surrogate in \\u escape");
                        p_ += 2;
                        std::uint32_t low = 0;
                        if (!read_hex4(low))
                            return fail(esc, "\\u escape needs four hexadecimal digits");
                        if (low < 0xDC00 || low > 0xDFFF)
                            return fail(esc, "high surrogate not followed by a low surrogate");
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                    }
                    encode_utf8(cp, out);
                    break;
                }
                default:
                    return fail(esc, std::string("invalid escape sequence \\") + e);
            }
        }
        return fail(open, "unterminated string");
    }

    // strtod is correctly rounded where a hand-rolled conversion would not be,
    // so the only thing left to do by hand is honour the active locale's
    // decimal separator. strtod needs a terminator, hence the copy: a number
    // token in a rule file fits the short-string buffer, so it costs no
    // allocation, and it is a token rather than a copy of the document.
    static bool to_double(const char* b, const char* e, double& out) {
        std::string buf(b, e);
        const char point = std::localeconv()->decimal_point[0];
        if (point != '.' && point != '\0')
            for (char& c : buf)
                if (c == '.') c = point;
        errno = 0;
        char* stop = nullptr;
        const double v = std::strtod(buf.c_str(), &stop);
        if (stop != buf.c_str() + buf.size()) return false;
        if (errno == ERANGE && (v >= HUGE_VAL || v <= -HUGE_VAL)) return false;
        out = v;
        return true;
    }

    bool parse_number(ConfigNode& out) {
        const char* start = p_;
        if (p_ < end_ && *p_ == '-') ++p_;
        if (p_ >= end_ || !is_digit(*p_))
            return fail(start, "malformed number: expected a digit");
        const char* int_start = p_;
        const bool zero_first = (*p_ == '0');
        while (p_ < end_ && is_digit(*p_)) ++p_;
        if (zero_first && p_ - int_start > 1)
            return fail(int_start, "malformed number: leading zeros are not allowed");

        bool real = false;
        if (p_ < end_ && *p_ == '.') {
            real = true;
            ++p_;
            if (p_ >= end_ || !is_digit(*p_))
                return fail(p_, "malformed number: expected a digit after the decimal point");
            while (p_ < end_ && is_digit(*p_)) ++p_;
        }
        if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
            real = true;
            ++p_;
            if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
            if (p_ >= end_ || !is_digit(*p_))
                return fail(p_, "malformed number: expected a digit in the exponent");
            while (p_ < end_ && is_digit(*p_)) ++p_;
        }
        if (p_ < end_ && !is_delim(*p_))
            return fail(p_, "malformed number");

        // Only a token written without a fraction or an exponent can be an
        // integer, so 1 and 1.0 land in different Value alternatives on purpose.
        if (!real) {
            const bool neg = (*start == '-');
            const std::uint64_t limit = neg ? 9223372036854775808ULL : 9223372036854775807ULL;
            std::uint64_t mag = 0;
            bool overflow = false;
            for (const char* d = neg ? start + 1 : start; d < p_; ++d) {
                const std::uint64_t digit = static_cast<std::uint64_t>(*d - '0');
                if (mag > (limit - digit) / 10) { overflow = true; break; }
                mag = mag * 10 + digit;
            }
            if (!overflow) {
                std::int64_t v;
                if (!neg)                            v = static_cast<std::int64_t>(mag);
                else if (mag == 9223372036854775808ULL) v = std::numeric_limits<std::int64_t>::min();
                else                                 v = -static_cast<std::int64_t>(mag);
                out = ConfigNode(Value(v));
                return true;
            }
        }
        double dv = 0.0;
        if (!to_double(start, p_, dv))
            return fail(start, "number out of range");
        out = ConfigNode(Value(dv));
        return true;
    }

    bool parse_literal(ConfigNode& out) {
        // The delimiter check is what stops "truthy" from parsing as true
        // followed by junk the caller would have to diagnose instead.
        const auto match = [this](const char* text, std::size_t n) {
            if (static_cast<std::size_t>(end_ - p_) < n) return false;
            if (std::memcmp(p_, text, n) != 0) return false;
            if (p_ + n < end_ && !is_delim(p_[n])) return false;
            p_ += n;
            return true;
        };
        if (match("true", 4))  { out = ConfigNode(Value(true));  return true; }
        if (match("false", 5)) { out = ConfigNode(Value(false)); return true; }
        if (match("null", 4))  { out = ConfigNode();             return true; }
        return fail(p_, "invalid literal: expected true, false or null");
    }

    bool parse_array(ConfigNode& out, std::size_t depth) {
        const char* open = p_;
        ++p_;
        ConfigSeq seq;
        skip_ws();
        if (p_ < end_ && *p_ == ']') { ++p_; out = ConfigNode(std::move(seq)); return true; }
        for (;;) {
            skip_ws();
            if (p_ >= end_) return fail(open, "unterminated array");
            if (*p_ == ']') return fail(p_, "trailing comma is not allowed in an array");
            ConfigNode child;
            if (!parse_value(child, depth + 1)) return false;
            seq.push_back(std::move(child));
            skip_ws();
            if (p_ >= end_) return fail(open, "unterminated array");
            if (*p_ == ',') { ++p_; continue; }
            if (*p_ == ']') { ++p_; break; }
            return fail(p_, "expected ',' or ']' in array");
        }
        out = ConfigNode(std::move(seq));
        return true;
    }

    bool parse_object(ConfigNode& out, std::size_t depth) {
        const char* open = p_;
        ++p_;
        ConfigMap map;
        skip_ws();
        if (p_ < end_ && *p_ == '}') { ++p_; out = ConfigNode(std::move(map)); return true; }
        for (;;) {
            skip_ws();
            if (p_ >= end_) return fail(open, "unterminated object");
            if (*p_ == '}') return fail(p_, "trailing comma is not allowed in an object");
            if (*p_ == '\'')
                return fail(p_, "single-quoted strings are not JSON, use double quotes");
            if (*p_ != '"')
                return fail(p_, "object keys must be double-quoted strings");
            const char* key_pos = p_;
            std::string key;
            if (!parse_string(key)) return false;
            // Linear duplicate check. Rule objects have a handful of keys, so a
            // set would cost more than it saves, and insertion order is part of
            // the ConfigNode contract.
            for (const auto& kv : map)
                if (kv.first == key)
                    return fail(key_pos, "duplicate key \"" + key + "\"");
            skip_ws();
            if (p_ >= end_ || *p_ != ':') return fail(p_, "expected ':' after object key");
            ++p_;
            ConfigNode child;
            if (!parse_value(child, depth + 1)) return false;
            map.emplace_back(std::move(key), std::move(child));
            skip_ws();
            if (p_ >= end_) return fail(open, "unterminated object");
            if (*p_ == ',') { ++p_; continue; }
            if (*p_ == '}') { ++p_; break; }
            return fail(p_, "expected ',' or '}' in object");
        }
        out = ConfigNode(std::move(map));
        return true;
    }

    bool parse_value(ConfigNode& out, std::size_t depth) {
        skip_ws();
        if (p_ >= end_) return fail(p_, "unexpected end of input");
        switch (*p_) {
            case '{':
            case '[':
                if (depth >= kJsonMaxDepth)
                    return fail(p_, "maximum nesting depth of "
                                    + std::to_string(kJsonMaxDepth) + " exceeded");
                return *p_ == '{' ? parse_object(out, depth) : parse_array(out, depth);
            case '"': {
                std::string s;
                if (!parse_string(s)) return false;
                out = ConfigNode(Value(std::move(s)));
                return true;
            }
            case '\'':
                return fail(p_, "single-quoted strings are not JSON, use double quotes");
            case 't': case 'f': case 'n':
                return parse_literal(out);
            case '-':
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return parse_number(out);
            default:
                return fail(p_, std::string("unexpected character '") + *p_ + "'");
        }
    }

    const char* begin_;
    const char* p_;
    const char* end_;
    ParseError& err_;
};

} // namespace detail

// Parses `len` bytes into `out`. Returns false and fills `err` with a 1-based
// line and column on any malformed input. Never throws, never aborts.
inline bool parse_json(const char* data, std::size_t len, ConfigNode& out, ParseError& err) {
    out.clear();
    err.clear();
    if (data == nullptr || len == 0) {
        err.message = "empty input";
        err.line = err.column = 1;
        return false;
    }
    detail::JsonReader reader(data, len, err);
    if (reader.run(out)) return true;
    out.clear();
    return false;
}

inline bool parse_json(const std::string& text, ConfigNode& out, ParseError& err) {
    return parse_json(text.data(), text.size(), out, err);
}

} // namespace rete::config

#endif // RETE_ENABLE_JSON
